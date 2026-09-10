from __future__ import annotations

import unittest
from dataclasses import replace

import numpy as np

from app.engine import (
    AudioTooLongError,
    OUTPUT_SAMPLE_RATE,
    SynthesisInput,
    VieNeuEngine,
    float48k_to_pcm24k,
)
from app.settings import Settings


class PCMConversionTests(unittest.TestCase):
    def test_resamples_float48k_to_pcm16le24k(self) -> None:
        audio = np.linspace(-1.0, 1.0, 48_000, dtype=np.float32)
        pcm = float48k_to_pcm24k(audio, max_audio_seconds=2)
        self.assertEqual(len(pcm), OUTPUT_SAMPLE_RATE * 2)
        decoded = np.frombuffer(pcm, dtype="<i2")
        self.assertEqual(decoded.shape, (OUTPUT_SAMPLE_RATE,))
        self.assertLess(decoded[0], 0)
        self.assertGreater(decoded[-1], 0)

    def test_rejects_empty_audio(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "empty audio"):
            float48k_to_pcm24k(np.array([], dtype=np.float32), max_audio_seconds=1)

    def test_rejects_audio_over_limit(self) -> None:
        audio = np.zeros(48_001, dtype=np.float32)
        with self.assertRaises(AudioTooLongError):
            float48k_to_pcm24k(audio, max_audio_seconds=1)

    def test_rejects_unknown_preset_voice_before_inference(self) -> None:
        engine = object.__new__(VieNeuEngine)
        engine._settings = replace(Settings.from_env(), default_voice="Phạm Tuyên")
        engine._voice_ids = {"Phạm Tuyên"}
        with self.assertRaisesRegex(ValueError, "unsupported preset voice"):
            engine.synthesize(SynthesisInput("Xin chào", "Không tồn tại", None))


if __name__ == "__main__":
    unittest.main()
