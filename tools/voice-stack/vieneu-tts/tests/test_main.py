from __future__ import annotations

import asyncio
import threading
import unittest
from dataclasses import replace
from unittest.mock import patch

from fastapi.testclient import TestClient

from app.engine import SynthesisInput
from app.main import create_app
from app.runtime import ModelRuntime, QueueFullError
from app.settings import Settings


class FakeRuntime:
    def __init__(self) -> None:
        self.started = False
        self.requests: list[SynthesisInput] = []

    async def start(self) -> None:
        self.started = True

    async def stop(self) -> None:
        self.started = False

    def health(self) -> dict[str, object]:
        return {"status": "ready", "ready": True, "model": "vieneu-v3-turbo"}

    def voices(self) -> list[dict[str, object]]:
        return [{"id": "Phạm Tuyên", "name": "Phạm Tuyên", "default": True}]

    async def submit(self, request: SynthesisInput) -> bytes:
        self.requests.append(request)
        return b"\x01\x00" * 20


class APIContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.settings = replace(Settings.from_env(), api_key="internal-secret")
        self.runtime = FakeRuntime()
        self.client = TestClient(create_app(self.settings, self.runtime))

    def test_health_does_not_require_auth(self) -> None:
        response = self.client.get("/health/ready")
        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.json()["ready"])

    def test_speech_requires_bearer_and_returns_pcm_contract(self) -> None:
        payload = {
            "model": "vieneu-v3-turbo",
            "input": "Xin chào con.",
            "voice": "Trúc Ly",
            "style": "doc_truyen",
            "response_format": "pcm",
            "sample_rate": 24000,
        }
        self.assertEqual(self.client.post("/v1/audio/speech", json=payload).status_code, 401)
        response = self.client.post(
            "/v1/audio/speech",
            json=payload,
            headers={"Authorization": "Bearer internal-secret"},
        )
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers["x-audio-format"], "pcm_s16le")
        self.assertEqual(response.headers["x-audio-sample-rate"], "24000")
        self.assertEqual(len(response.content), 40)
        self.assertEqual(self.runtime.requests[0].voice, "Trúc Ly")

    def test_rejects_wrong_model_and_audio_format(self) -> None:
        headers = {"Authorization": "Bearer internal-secret"}
        wrong_model = self.client.post(
            "/v1/audio/speech", json={"model": "other", "input": "test"}, headers=headers
        )
        self.assertEqual(wrong_model.status_code, 404)
        wrong_rate = self.client.post(
            "/v1/audio/speech",
            json={"input": "test", "sample_rate": 48000},
            headers=headers,
        )
        self.assertEqual(wrong_rate.status_code, 422)

    def test_rejects_whitespace_only_input(self) -> None:
        response = self.client.post(
            "/v1/audio/speech",
            json={"input": "   \n\t"},
            headers={"Authorization": "Bearer internal-secret"},
        )
        self.assertEqual(response.status_code, 422)
        self.assertEqual(self.runtime.requests, [])

    def test_production_mode_requires_api_key(self) -> None:
        with patch.dict(
            "os.environ",
            {"VIENEU_REQUIRE_API_KEY": "true", "VIENEU_API_KEY": ""},
            clear=False,
        ):
            with self.assertRaisesRegex(ValueError, "VIENEU_API_KEY must be set"):
                Settings.from_env()


class BlockingEngine:
    def __init__(self) -> None:
        self.active = 0
        self.max_active = 0
        self.started = threading.Event()
        self.release = threading.Event()

    def synthesize(self, _: SynthesisInput) -> bytes:
        self.active += 1
        self.max_active = max(self.max_active, self.active)
        self.started.set()
        self.release.wait(timeout=2)
        self.active -= 1
        return b"pcm"

    @property
    def voices(self) -> list[dict[str, object]]:
        return []


class RuntimeQueueTests(unittest.IsolatedAsyncioTestCase):
    async def test_one_worker_and_bounded_queue(self) -> None:
        settings = replace(
            Settings.from_env(), queue_size=1, request_timeout_seconds=5
        )
        engine = BlockingEngine()
        runtime = ModelRuntime(settings, engine_factory=lambda _: engine)
        await runtime.start()
        try:
            for _ in range(100):
                if runtime.health()["ready"]:
                    break
                await asyncio.sleep(0.01)
            first = asyncio.create_task(runtime.submit(SynthesisInput("one", None, None)))
            await asyncio.to_thread(engine.started.wait, 1)
            second = asyncio.create_task(runtime.submit(SynthesisInput("two", None, None)))
            for _ in range(100):
                if runtime.health()["queueDepth"] == 1:
                    break
                await asyncio.sleep(0.01)
            with self.assertRaises(QueueFullError):
                await runtime.submit(SynthesisInput("three", None, None))
            engine.release.set()
            self.assertEqual(await first, b"pcm")
            self.assertEqual(await second, b"pcm")
            self.assertEqual(engine.max_active, 1)
        finally:
            engine.release.set()
            await runtime.stop()


if __name__ == "__main__":
    unittest.main()
