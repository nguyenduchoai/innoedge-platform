from __future__ import annotations

import functools
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import logging

import numpy as np
import soxr

from .settings import Settings

log = logging.getLogger("vimate.vieneu.engine")


SOURCE_SAMPLE_RATE = 48_000
OUTPUT_SAMPLE_RATE = 24_000
VALID_STYLES = {"tu_nhien", "tin_tuc", "doc_truyen"}
DEFAULT_MANIFEST_PATH = "/models/huggingface/vimate-vieneu-manifest.json"


class AudioTooLongError(RuntimeError):
    """Generated audio exceeded the configured response limit."""


@dataclass(frozen=True)
class SynthesisInput:
    text: str
    voice: str | None
    style: str | None


def float48k_to_pcm24k(audio: Any, max_audio_seconds: int) -> bytes:
    waveform = np.asarray(audio, dtype=np.float32).reshape(-1)
    if waveform.size == 0:
        raise RuntimeError("VieNeu returned empty audio")
    waveform = np.nan_to_num(waveform, nan=0.0, posinf=1.0, neginf=-1.0)
    resampled = soxr.resample(waveform, SOURCE_SAMPLE_RATE, OUTPUT_SAMPLE_RATE, quality="HQ")
    if resampled.size > max_audio_seconds * OUTPUT_SAMPLE_RATE:
        raise AudioTooLongError(
            f"generated audio exceeds {max_audio_seconds} seconds"
        )
    clipped = np.clip(resampled, -1.0, 1.0)
    return (clipped * 32767.0).astype("<i2", copy=False).tobytes()


def _install_huggingface_revision_pins(settings: Settings) -> None:
    """Force every SDK download for the two runtime repos to exact commits."""
    import huggingface_hub

    pins = {
        settings.model_repo: settings.model_revision,
        settings.codec_repo: settings.codec_revision,
    }
    current = getattr(huggingface_hub, "_vimate_revision_pins", None)
    if current is not None:
        current.update(pins)
        return

    original = huggingface_hub.hf_hub_download
    huggingface_hub._vimate_revision_pins = pins

    @functools.wraps(original)
    def pinned_download(repo_id: str, *args: Any, **kwargs: Any) -> str:
        revision = huggingface_hub._vimate_revision_pins.get(repo_id)
        if revision:
            kwargs["revision"] = revision
            if getattr(huggingface_hub, "_vimate_local_only", False):
                kwargs["local_files_only"] = True
        return original(repo_id, *args, **kwargs)

    huggingface_hub.hf_hub_download = pinned_download


def preload_pinned_models(settings: Settings, manifest_path: str = DEFAULT_MANIFEST_PATH) -> None:
    """Download exact runtime artifacts during image build and write a manifest."""
    import huggingface_hub

    huggingface_hub.snapshot_download(
        repo_id=settings.model_repo,
        revision=settings.model_revision,
        # speaker_encoder.onnx: cần cho enroll giọng clone (lazy-load lúc chạy,
        # container offline nên phải bake sẵn).
        allow_patterns=["config.json", "denoiser.onnx", "speaker_encoder.onnx", "onnx_int8/*"]
        + (["update/*"] if settings.device == "cuda" else []),
    )
    if settings.device == "cuda":
        # MOSS tokenizer torch cho đường PyTorch/GPU (repo riêng, không pin
        # revision — bake lúc build nên runtime vẫn offline).
        huggingface_hub.snapshot_download(
            repo_id="OpenMOSS-Team/MOSS-Audio-Tokenizer-Nano",
        )
    huggingface_hub.snapshot_download(
        repo_id=settings.codec_repo,
        revision=settings.codec_revision,
        allow_patterns=[
            "moss_audio_tokenizer_decode_full.onnx",
            "moss_audio_tokenizer_decode_shared.data",
            "moss_audio_tokenizer_decode_step.onnx",
            "codec_browser_onnx_meta.json",
            "moss_audio_tokenizer_encode.onnx",
            "moss_audio_tokenizer_encode.data",
        ],
    )
    Path(manifest_path).write_text(
        json.dumps(
            {
                "modelRepo": settings.model_repo,
                "modelRevision": settings.model_revision,
                "codecRepo": settings.codec_repo,
                "codecRevision": settings.codec_revision,
            },
            sort_keys=True,
        ),
        encoding="utf-8",
    )


def _activate_baked_models(settings: Settings) -> None:
    import huggingface_hub

    manifest_path = os.getenv("VIENEU_MODEL_MANIFEST", DEFAULT_MANIFEST_PATH)
    try:
        actual = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"baked model manifest is unavailable: {exc}") from exc
    expected = {
        "modelRepo": settings.model_repo,
        "modelRevision": settings.model_revision,
        "codecRepo": settings.codec_repo,
        "codecRevision": settings.codec_revision,
    }
    if actual != expected:
        raise RuntimeError(f"baked model manifest mismatch: expected {expected}, got {actual}")
    huggingface_hub._vimate_local_only = True
    os.environ["HF_HUB_OFFLINE"] = "1"


CUSTOM_VOICE_RE = re.compile(r"^cv\d{1,10}$")


class VieNeuEngine:
    """Single in-process VieNeu v3 Turbo ONNX/CPU model instance."""

    def __init__(self, settings: Settings):
        self._settings = settings
        _install_huggingface_revision_pins(settings)
        _activate_baked_models(settings)

        # Import after installing revision pins because the SDK imports HF helpers lazily.
        from vieneu import Vieneu

        if settings.device == "cuda":
            # GPU: backbone PyTorch (subfolder "update") + MOSS tokenizer torch.
            # Artifacts đã bake lúc build (preload với VIENEU_DEVICE=cuda).
            self._tts = Vieneu(
                mode="v3turbo",
                backend="pytorch",
                device="cuda",
                backbone_repo=settings.model_repo,
            )
        else:
            self._tts = Vieneu(
                mode="v3turbo",
                backend="onnx",
                precision="int8",
                threads=settings.threads,
                backbone_repo=settings.model_repo,
            )
        self._voices = self._load_voices()
        self._voice_ids = {item["id"] for item in self._voices}
        self._restore_custom_voices()
        if settings.warmup_text:
            warmup = self._tts.infer(
                settings.warmup_text,
                voice=settings.default_voice,
                style=settings.default_style,
                apply_watermark=settings.apply_watermark,
            )
            float48k_to_pcm24k(warmup, settings.max_audio_seconds)

    def _load_voices(self) -> list[dict[str, Any]]:
        voices: list[dict[str, Any]] = []
        for label, voice_id in self._tts.list_preset_voices():
            voices.append(
                {
                    "id": str(voice_id),
                    "name": str(label),
                    "default": str(voice_id) == self._settings.default_voice,
                }
            )
        if not voices:
            raise RuntimeError("VieNeu loaded without preset voices")
        if not any(item["id"] == self._settings.default_voice for item in voices):
            raise RuntimeError(
                f"default voice {self._settings.default_voice!r} is not available"
            )
        return voices

    @property
    def voices(self) -> list[dict[str, Any]]:
        return [dict(item) for item in self._voices]

    # ---- Custom voices (clone giọng phụ huynh) ----------------------------
    # Tên bắt buộc dạng "cv<số>" (id DB phía server Go) — không bao giờ đụng
    # preset. GET /v1/voices KHÔNG liệt kê custom (per-account, server Go tự
    # quản hiển thị); synth thì nhận vì tên nằm trong _voice_ids.

    def _custom_store_read(self) -> dict[str, Any]:
        import json
        path = Path(self._settings.custom_voices_path)
        if not path.exists():
            return {}
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            log.exception("custom voices file hỏng — bỏ qua, enroll lại khi cần")
            return {}

    def _custom_store_write(self, data: dict[str, Any]) -> None:
        import json
        import os as _os
        path = Path(self._settings.custom_voices_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        _os.replace(tmp, path)

    def _restore_custom_voices(self) -> None:
        data = self._custom_store_read()
        for name, v in data.items():
            if not CUSTOM_VOICE_RE.fullmatch(name):
                continue
            emb = v.get("speaker_emb")
            codes = v.get("codes")
            if emb is None:
                continue
            self._tts._preset_voices[name] = {
                "description": v.get("description", ""),
                "gender": v.get("gender", ""),
                "style": v.get("style", self._settings.default_style),
                "speaker_emb": np.asarray(emb, dtype=np.float32),
                "codes": None if codes is None else np.asarray(codes, dtype=np.int64),
            }
            self._voice_ids.add(name)
        if data:
            log.info("restored %d custom voices", len(data))

    def add_custom_voice(self, name: str, wav_path: str, description: str = "") -> dict[str, Any]:
        if not CUSTOM_VOICE_RE.fullmatch(name):
            raise ValueError("custom voice name must match cv<digits>")
        # Re-enroll idempotent: đè bản cũ cùng tên.
        self._tts._preset_voices.pop(name, None)
        self._tts.add_voice(name, wav_path, denoise=True, description=description, save=False)
        entry = self._tts._preset_voices[name]
        data = self._custom_store_read()
        emb = entry.get("speaker_emb")
        codes = entry.get("codes")
        data[name] = {
            "description": description,
            "gender": entry.get("gender", ""),
            "style": entry.get("style", self._settings.default_style),
            "speaker_emb": [round(float(x), 6) for x in np.asarray(emb).reshape(-1)],
            "codes": None if codes is None else np.asarray(codes, dtype=int).tolist(),
        }
        self._custom_store_write(data)
        self._voice_ids.add(name)
        return {"id": name, "description": description}

    def remove_custom_voice(self, name: str) -> dict[str, Any]:
        if not CUSTOM_VOICE_RE.fullmatch(name):
            raise ValueError("custom voice name must match cv<digits>")
        self._tts._preset_voices.pop(name, None)
        self._voice_ids.discard(name)
        data = self._custom_store_read()
        data.pop(name, None)
        self._custom_store_write(data)
        return {"deleted": name}

    def synthesize(self, request: SynthesisInput) -> bytes:
        style = request.style or self._settings.default_style
        if style not in VALID_STYLES:
            raise ValueError(f"unsupported style {style!r}")
        voice = request.voice or self._settings.default_voice
        if voice not in self._voice_ids:
            raise ValueError(f"unsupported preset voice {voice!r}")
        audio = self._tts.infer(
            request.text,
            voice=voice,
            style=style,
            apply_watermark=self._settings.apply_watermark,
        )
        return float48k_to_pcm24k(audio, self._settings.max_audio_seconds)
