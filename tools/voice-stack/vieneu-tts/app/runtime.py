from __future__ import annotations

import asyncio
import logging
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import Callable

from .engine import SynthesisInput, VieNeuEngine
from .settings import Settings


log = logging.getLogger("vimate.vieneu.runtime")


class ModelUnavailableError(RuntimeError):
    pass


class QueueFullError(RuntimeError):
    pass


class InferenceTimeoutError(RuntimeError):
    pass


@dataclass
class _Job:
    result: asyncio.Future
    request: SynthesisInput | None = None
    # Việc khác synth (enroll/xóa giọng) — chạy trên CÙNG model thread.
    fn: "Callable[[VieNeuEngine], object] | None" = None


class ModelRuntime:
    """Own one model thread and a bounded FIFO queue."""

    def __init__(
        self,
        settings: Settings,
        engine_factory: Callable[[Settings], VieNeuEngine] = VieNeuEngine,
    ):
        self.settings = settings
        self._engine_factory = engine_factory
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="vieneu-model")
        self._queue: asyncio.Queue[_Job] = asyncio.Queue(maxsize=settings.queue_size)
        self._engine: VieNeuEngine | None = None
        self._load_error: str | None = None
        self._started_at = time.monotonic()
        self._load_task: asyncio.Task[None] | None = None
        self._worker_task: asyncio.Task[None] | None = None

    async def start(self) -> None:
        self._worker_task = asyncio.create_task(self._worker(), name="vieneu-worker")
        self._load_task = asyncio.create_task(self._load(), name="vieneu-loader")

    async def stop(self) -> None:
        for task in (self._load_task, self._worker_task):
            if task is not None:
                task.cancel()
        await asyncio.gather(
            *(task for task in (self._load_task, self._worker_task) if task is not None),
            return_exceptions=True,
        )
        self._executor.shutdown(wait=False, cancel_futures=True)

    async def _load(self) -> None:
        loop = asyncio.get_running_loop()
        try:
            self._engine = await loop.run_in_executor(
                self._executor, self._engine_factory, self.settings
            )
            log.info("VieNeu model loaded and warm-up synthesis passed")
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # noqa: BLE001 - readiness exposes a bounded diagnosis.
            self._load_error = str(exc)[:500]
            log.exception("VieNeu model failed to load")

    async def _worker(self) -> None:
        loop = asyncio.get_running_loop()
        while True:
            job = await self._queue.get()
            try:
                if job.result.cancelled():
                    continue
                if self._engine is None:
                    raise ModelUnavailableError(self._load_error or "model is warming up")
                if job.fn is not None:
                    out = await loop.run_in_executor(self._executor, job.fn, self._engine)
                else:
                    out = await loop.run_in_executor(
                        self._executor, self._engine.synthesize, job.request
                    )
                if not job.result.done():
                    job.result.set_result(out)
            except asyncio.CancelledError:
                if not job.result.done():
                    job.result.cancel()
                raise
            except Exception as exc:  # noqa: BLE001 - forwarded to the internal caller.
                if not job.result.done():
                    job.result.set_exception(exc)
            finally:
                self._queue.task_done()

    async def submit(self, request: SynthesisInput) -> bytes:
        if self._engine is None:
            raise ModelUnavailableError(self._load_error or "model is warming up")
        loop = asyncio.get_running_loop()
        future: asyncio.Future[bytes] = loop.create_future()
        try:
            self._queue.put_nowait(_Job(request=request, result=future))
        except asyncio.QueueFull as exc:
            raise QueueFullError("inference queue is full") from exc
        try:
            return await asyncio.wait_for(
                asyncio.shield(future), timeout=self.settings.request_timeout_seconds
            )
        except TimeoutError as exc:
            future.cancel()
            raise InferenceTimeoutError("inference timed out") from exc

    async def submit_work(self, fn) -> object:
        """Chạy fn(engine) trên model thread — enroll/xóa custom voice."""
        if self._engine is None:
            raise ModelUnavailableError(self._load_error or "model is warming up")
        loop = asyncio.get_running_loop()
        future: asyncio.Future = loop.create_future()
        try:
            self._queue.put_nowait(_Job(result=future, fn=fn))
        except asyncio.QueueFull as exc:
            raise QueueFullError("inference queue is full") from exc
        try:
            return await asyncio.wait_for(
                asyncio.shield(future), timeout=self.settings.request_timeout_seconds
            )
        except TimeoutError as exc:
            future.cancel()
            raise InferenceTimeoutError("voice work timed out") from exc

    def health(self) -> dict[str, object]:
        state = "ready" if self._engine is not None else ("failed" if self._load_error else "warming")
        return {
            "status": state,
            "ready": self._engine is not None,
            "model": self.settings.model_id,
            "backend": "onnx-int8-cpu",
            "queueDepth": self._queue.qsize(),
            "queueCapacity": self.settings.queue_size,
            "uptimeSeconds": int(time.monotonic() - self._started_at),
            "error": self._load_error,
        }

    def voices(self) -> list[dict[str, object]]:
        if self._engine is None:
            raise ModelUnavailableError(self._load_error or "model is warming up")
        return self._engine.voices
