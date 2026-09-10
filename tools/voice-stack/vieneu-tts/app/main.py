from __future__ import annotations

import hmac
import logging
from contextlib import asynccontextmanager

import asyncio
import os
import re
import tempfile

import uvicorn
from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, Response, UploadFile, status
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field

from .engine import AudioTooLongError, SynthesisInput
from .runtime import (
    InferenceTimeoutError,
    ModelRuntime,
    ModelUnavailableError,
    QueueFullError,
)
from .settings import Settings


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)
log = logging.getLogger("vimate.vieneu.api")


class SpeechRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    model: str = "vieneu-v3-turbo"
    input: str = Field(min_length=1)
    voice: str | None = None
    style: str | None = None
    response_format: str = "pcm"
    sample_rate: int = 24_000


def create_app(settings: Settings | None = None, runtime: ModelRuntime | None = None) -> FastAPI:
    cfg = settings or Settings.from_env()
    model_runtime = runtime or ModelRuntime(cfg)

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        await model_runtime.start()
        try:
            yield
        finally:
            await model_runtime.stop()

    app = FastAPI(
        title="VIMATE VieNeu-TTS sidecar",
        version="1.0.0",
        docs_url=None,
        redoc_url=None,
        openapi_url=None,
        lifespan=lifespan,
    )

    def require_internal_auth(authorization: str | None = Header(default=None)) -> None:
        if not cfg.api_key:
            return
        expected = f"Bearer {cfg.api_key}"
        if authorization is None or not hmac.compare_digest(authorization, expected):
            raise HTTPException(
                status_code=status.HTTP_401_UNAUTHORIZED,
                detail="invalid bearer token",
                headers={"WWW-Authenticate": "Bearer"},
            )

    @app.get("/health/live")
    async def live() -> dict[str, object]:
        return {"status": "live"}

    @app.get("/health")
    @app.get("/health/ready")
    async def ready() -> JSONResponse:
        payload = model_runtime.health()
        code = status.HTTP_200_OK if payload["ready"] else status.HTTP_503_SERVICE_UNAVAILABLE
        return JSONResponse(status_code=code, content=payload)

    @app.get("/v1/models", dependencies=[Depends(require_internal_auth)])
    async def models() -> dict[str, object]:
        return {
            "object": "list",
            "data": [{"id": cfg.model_id, "object": "model", "owned_by": "pnnbao97"}],
        }

    @app.get("/v1/voices", dependencies=[Depends(require_internal_auth)])
    async def voices() -> dict[str, object]:
        try:
            return {"data": model_runtime.voices(), "defaultVoice": cfg.default_voice}
        except ModelUnavailableError as exc:
            raise HTTPException(status_code=503, detail=str(exc)) from exc

    _custom_name_re = re.compile(r"^cv\d{1,10}$")
    _max_sample_bytes = 25 * 1024 * 1024

    async def _to_ref_wav(raw: bytes, suffix: str, out_path: str) -> None:
        """Convert audio bất kỳ (m4a/mp3/wav...) → WAV mono 24kHz, cắt max_ref_seconds."""
        with tempfile.NamedTemporaryFile(suffix=suffix or ".bin", delete=False) as tmp:
            tmp.write(raw)
            src = tmp.name
        try:
            proc = await asyncio.create_subprocess_exec(
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                "-i", src, "-ac", "1", "-ar", "24000",
                "-t", str(cfg.max_ref_seconds), out_path,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.PIPE,
            )
            try:
                _, err = await asyncio.wait_for(proc.communicate(), timeout=60)
            except TimeoutError:
                proc.kill()
                raise HTTPException(status_code=504, detail="audio convert timed out")
            if proc.returncode != 0:
                raise HTTPException(
                    status_code=422,
                    detail="audio không đọc được: " + (err or b"")[:200].decode(errors="replace"),
                )
        finally:
            os.unlink(src)

    @app.post("/v1/voices", dependencies=[Depends(require_internal_auth)])
    async def enroll_voice(
        name: str = Form(...),
        file: UploadFile = File(...),
        description: str = Form(""),
    ) -> dict[str, object]:
        if not _custom_name_re.fullmatch(name):
            raise HTTPException(status_code=422, detail="name must match cv<digits>")
        raw = await file.read()
        if not raw:
            raise HTTPException(status_code=422, detail="empty audio file")
        if len(raw) > _max_sample_bytes:
            raise HTTPException(status_code=413, detail="audio sample > 25MB")
        suffix = os.path.splitext(file.filename or "")[1][:8]
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as out:
            wav_path = out.name
        try:
            await _to_ref_wav(raw, suffix, wav_path)
            try:
                result = await model_runtime.submit_work(
                    lambda eng: eng.add_custom_voice(name, wav_path, description)
                )
            except QueueFullError as exc:
                raise HTTPException(status_code=429, detail=str(exc), headers={"Retry-After": "1"}) from exc
            except ModelUnavailableError as exc:
                raise HTTPException(status_code=503, detail=str(exc)) from exc
            except InferenceTimeoutError as exc:
                raise HTTPException(status_code=504, detail=str(exc)) from exc
            except ValueError as exc:
                raise HTTPException(status_code=422, detail=str(exc)[:300]) from exc
            except Exception as exc:  # noqa: BLE001 - internal service, bounded diagnosis.
                log.exception("voice enroll failed")
                raise HTTPException(status_code=502, detail=str(exc)[:300]) from exc
        finally:
            try:
                os.unlink(wav_path)
            except OSError:
                pass
        log.info("enrolled custom voice %s", name)
        return result

    @app.delete("/v1/voices/{name}", dependencies=[Depends(require_internal_auth)])
    async def delete_voice(name: str) -> dict[str, object]:
        if not _custom_name_re.fullmatch(name):
            raise HTTPException(status_code=422, detail="name must match cv<digits>")
        try:
            return await model_runtime.submit_work(
                lambda eng: eng.remove_custom_voice(name)
            )
        except ModelUnavailableError as exc:
            raise HTTPException(status_code=503, detail=str(exc)) from exc
        except (QueueFullError, InferenceTimeoutError) as exc:
            raise HTTPException(status_code=503, detail=str(exc)) from exc

    @app.post("/v1/audio/speech", dependencies=[Depends(require_internal_auth)])
    async def speech(request: SpeechRequest) -> Response:
        text = request.input.strip()
        if not text:
            raise HTTPException(
                status_code=422,
                detail="input must contain non-whitespace text",
            )
        if len(text) > cfg.max_text_chars:
            raise HTTPException(
                status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                detail=f"input exceeds {cfg.max_text_chars} characters",
            )
        if request.model != cfg.model_id:
            raise HTTPException(status_code=404, detail="model not found")
        if request.response_format != "pcm" or request.sample_rate != 24_000:
            raise HTTPException(
                status_code=422,
                detail="only raw PCM16LE mono 24000 Hz is supported",
            )
        try:
            pcm = await model_runtime.submit(
                SynthesisInput(text=text, voice=request.voice, style=request.style)
            )
        except QueueFullError as exc:
            raise HTTPException(
                status_code=429, detail=str(exc), headers={"Retry-After": "1"}
            ) from exc
        except ModelUnavailableError as exc:
            raise HTTPException(status_code=503, detail=str(exc)) from exc
        except InferenceTimeoutError as exc:
            raise HTTPException(status_code=504, detail=str(exc)) from exc
        except AudioTooLongError as exc:
            raise HTTPException(status_code=413, detail=str(exc)) from exc
        except ValueError as exc:
            raise HTTPException(status_code=422, detail=str(exc)[:500]) from exc
        except Exception as exc:  # noqa: BLE001 - internal service, bounded diagnosis.
            log.exception("VieNeu synthesis failed")
            raise HTTPException(status_code=502, detail=str(exc)[:500]) from exc

        return Response(
            content=pcm,
            media_type="application/octet-stream",
            headers={
                "X-Audio-Format": "pcm_s16le",
                "X-Audio-Sample-Rate": "24000",
                "X-Audio-Channels": "1",
                "X-Model": cfg.model_id,
            },
        )

    return app


settings = Settings.from_env()
app = create_app(settings=settings)


if __name__ == "__main__":
    uvicorn.run(app, host=settings.host, port=settings.port, workers=1, access_log=False)
