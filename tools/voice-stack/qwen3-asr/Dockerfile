# syntax=docker/dockerfile:1.7

# vLLM 0.16.0 is the first stable release that includes Qwen3-ASR. The
# manifest digest prevents a mutable tag from changing the production image.
# Its CUDA 12.9 runtime supports the production NVIDIA 535 driver.
ARG VLLM_IMAGE="vllm/vllm-openai:v0.16.0@sha256:4801151759655c57606c844662e5213403c032a62d149c7ce61d615759a821ef"
FROM ${VLLM_IMAGE}

ARG VLLM_VERSION="0.16.0"

USER root

# Official vLLM images omit optional audio packages. Install the matching
# versioned extra so WAV/data-URI requests cannot degrade into HTTP 4xx/5xx.
RUN uv pip install --system "vllm[audio]==${VLLM_VERSION}" \
    && rm -rf /root/.cache/uv

COPY --chmod=0755 qwen3-asr-entrypoint.sh /usr/local/bin/qwen3-asr-entrypoint
COPY --chmod=0755 qwen3-asr-healthcheck.py /usr/local/bin/qwen3-asr-healthcheck
COPY --chmod=0755 qwen3-asr-warmup.py /usr/local/bin/qwen3-asr-warmup

ENTRYPOINT ["/usr/local/bin/qwen3-asr-entrypoint"]
