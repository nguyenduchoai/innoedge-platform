FROM python:3.12.11-slim-bookworm@sha256:519591d6871b7bc437060736b9f7456b8731f1499a57e22e6c285135ae657bf7 AS runtime

ARG VIENEU_VERSION=3.3.0
# "cpu" (ONNX int8) | "cuda" (PyTorch GPU — cài thêm torch cu121, ảnh nặng hơn ~2.5GB)
ARG VIENEU_DEVICE=cpu
ARG VIENEU_SOURCE_COMMIT=36c4b501b0634a8f59805e6b529a058fbd30190b
ARG VIENEU_MODEL_REVISION=2da0efab622a1722125991736524f080b751ef5b
ARG VIENEU_CODEC_REVISION=ceff0d0749bfb3fa2d61149794ec6feef0d1e1ae

LABEL org.opencontainers.image.title="VIMATE VieNeu-TTS sidecar" \
      org.opencontainers.image.source="https://github.com/pnnbao97/VieNeu-TTS" \
      org.opencontainers.image.version="${VIENEU_VERSION}" \
      org.opencontainers.image.revision="${VIENEU_SOURCE_COMMIT}"

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PYTHONHASHSEED=0 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    HOME=/home/vieneu \
    HF_HOME=/models/huggingface \
    HF_HUB_DISABLE_TELEMETRY=1 \
    TOKENIZERS_PARALLELISM=false \
    VIENEU_MODEL_REVISION=${VIENEU_MODEL_REVISION} \
    VIENEU_CODEC_REVISION=${VIENEU_CODEC_REVISION} \
    VIENEU_DEVICE=${VIENEU_DEVICE}

RUN apt-get update \
    && apt-get install --yes --no-install-recommends ca-certificates libgomp1 libsndfile1 ffmpeg \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 vieneu \
    && useradd --uid 10001 --gid vieneu --create-home --shell /usr/sbin/nologin vieneu \
    && mkdir -p /app /models/huggingface /data \
    && chown -R vieneu:vieneu /app /models /data /home/vieneu

WORKDIR /app
COPY requirements.lock /app/requirements.lock
RUN python -m pip install --no-cache-dir --require-hashes --requirement /app/requirements.lock \
    && python -m pip check

# GPU: torch CUDA wheel (bundle CUDA runtime — host chỉ cần driver + nvidia-container-toolkit).
# Không --require-hashes cho layer này (wheel torch theo index riêng của PyTorch).
RUN if [ "$VIENEU_DEVICE" = "cuda" ]; then \
      python -m pip install --no-cache-dir torch torchaudio --index-url https://download.pytorch.org/whl/cu121 \
      && python -m pip install --no-cache-dir transformers accelerate; \
    fi

COPY --chown=vieneu:vieneu app /app/app
RUN PYTHONPATH=/app python -m app.preload \
    && chown -R vieneu:vieneu /models/huggingface

ENV HF_HUB_OFFLINE=1 \
    TRANSFORMERS_OFFLINE=1 \
    VIENEU_MODELS_BAKED=1

USER 10001:10001
EXPOSE 8080
HEALTHCHECK --interval=20s --timeout=5s --start-period=300s --retries=3 \
  CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/health/ready', timeout=3).read()"]

CMD ["python", "-m", "app.main"]
