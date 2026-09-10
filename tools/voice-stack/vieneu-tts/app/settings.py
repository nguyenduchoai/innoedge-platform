from __future__ import annotations

import os
import re
from dataclasses import dataclass


_COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def _env_int(name: str, default: int, minimum: int, maximum: int) -> int:
    raw = os.getenv(name, "").strip()
    value = default if not raw else int(raw)
    if value < minimum or value > maximum:
        raise ValueError(f"{name} must be in [{minimum}, {maximum}]")
    return value


def _env_bool(name: str, default: bool) -> bool:
    raw = os.getenv(name, "").strip().lower()
    if not raw:
        return default
    if raw in {"1", "true", "yes", "on"}:
        return True
    if raw in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be a boolean")


@dataclass(frozen=True)
class Settings:
    host: str
    port: int
    api_key: str
    require_api_key: bool
    queue_size: int
    request_timeout_seconds: int
    max_text_chars: int
    max_audio_seconds: int
    threads: int
    device: str  # "cpu" (ONNX int8) | "cuda" (PyTorch GPU)
    model_id: str
    default_voice: str
    default_style: str
    model_repo: str
    model_revision: str
    codec_repo: str
    codec_revision: str
    warmup_text: str
    apply_watermark: bool
    custom_voices_path: str
    max_ref_seconds: int

    @classmethod
    def from_env(cls) -> "Settings":
        settings = cls(
            host=os.getenv("VIENEU_HOST", "0.0.0.0").strip(),
            port=_env_int("VIENEU_PORT", 8080, 1, 65535),
            api_key=os.getenv("VIENEU_API_KEY", "").strip(),
            require_api_key=_env_bool("VIENEU_REQUIRE_API_KEY", False),
            queue_size=_env_int("VIENEU_QUEUE_SIZE", 8, 1, 64),
            request_timeout_seconds=_env_int("VIENEU_REQUEST_TIMEOUT_SECONDS", 75, 5, 180),
            max_text_chars=_env_int("VIENEU_MAX_TEXT_CHARS", 600, 20, 2000),
            max_audio_seconds=_env_int("VIENEU_MAX_AUDIO_SECONDS", 60, 1, 120),
            threads=_env_int("VIENEU_THREADS", 8, 1, 32),
            device=os.getenv("VIENEU_DEVICE", "cpu").strip().lower() or "cpu",
            model_id=os.getenv("VIENEU_MODEL_ID", "vieneu-v3-turbo").strip(),
            default_voice=os.getenv("VIENEU_DEFAULT_VOICE", "Phạm Tuyên").strip(),
            default_style=os.getenv("VIENEU_DEFAULT_STYLE", "tu_nhien").strip(),
            model_repo=os.getenv(
                "VIENEU_MODEL_REPO", "pnnbao-ump/VieNeu-TTS-v3-Turbo"
            ).strip(),
            model_revision=os.getenv(
                "VIENEU_MODEL_REVISION", "2da0efab622a1722125991736524f080b751ef5b"
            ).strip().lower(),
            codec_repo=os.getenv(
                "VIENEU_CODEC_REPO", "OpenMOSS-Team/MOSS-Audio-Tokenizer-Nano-ONNX"
            ).strip(),
            codec_revision=os.getenv(
                "VIENEU_CODEC_REVISION", "ceff0d0749bfb3fa2d61149794ec6feef0d1e1ae"
            ).strip().lower(),
            warmup_text=os.getenv("VIENEU_WARMUP_TEXT", "Xin chào con.").strip(),
            apply_watermark=_env_bool("VIENEU_APPLY_WATERMARK", True),
            custom_voices_path=os.getenv(
                "VIENEU_CUSTOM_VOICES_PATH", "/data/custom_voices.json"
            ).strip(),
            max_ref_seconds=_env_int("VIENEU_MAX_REF_SECONDS", 30, 5, 120),
        )
        settings.validate()
        return settings

    def validate(self) -> None:
        for name, value in (
            ("VIENEU_MODEL_REVISION", self.model_revision),
            ("VIENEU_CODEC_REVISION", self.codec_revision),
        ):
            if not _COMMIT_RE.fullmatch(value):
                raise ValueError(f"{name} must be an exact 40-character commit SHA")
        if not self.model_id or not self.model_repo or not self.codec_repo:
            raise ValueError("model id/repositories must not be empty")
        if self.require_api_key and not self.api_key:
            raise ValueError("VIENEU_API_KEY must be set when VIENEU_REQUIRE_API_KEY=true")
        if self.default_style not in {"tu_nhien", "tin_tuc", "doc_truyen"}:
            raise ValueError("VIENEU_DEFAULT_STYLE is invalid")
