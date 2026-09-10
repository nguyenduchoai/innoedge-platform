#!/usr/bin/env sh
set -eu

MODEL=${QWEN3_ASR_MODEL:-Qwen/Qwen3-ASR-1.7B}
REVISION=${QWEN3_ASR_REVISION:-7278e1e70fe206f11671096ffdd38061171dd6e5}
SERVED_MODEL=${QWEN3_ASR_SERVED_MODEL:-$MODEL}
HOST=${QWEN3_ASR_HOST:-0.0.0.0}
PORT=${QWEN3_ASR_PORT:-8000}
GPU_MEMORY=${QWEN3_ASR_GPU_MEMORY_UTILIZATION:-0.62}
MAX_NUM_SEQS=${QWEN3_ASR_MAX_NUM_SEQS:-4}
MAX_MODEL_LEN=${QWEN3_ASR_MAX_MODEL_LEN:-4096}
DTYPE=${QWEN3_ASR_DTYPE:-half}
MM_LIMIT=${QWEN3_ASR_MM_LIMIT:-'{"audio":1}'}
API_KEY=${QWEN3_ASR_API_KEY:-}

if [ -n "${QWEN3_ASR_API_KEY_FILE:-}" ] && [ -r "$QWEN3_ASR_API_KEY_FILE" ]; then
  API_KEY=$(tr -d '\r\n' < "$QWEN3_ASR_API_KEY_FILE")
fi

set -- vllm serve "$MODEL" \
  --host "$HOST" \
  --port "$PORT" \
  --served-model-name "$SERVED_MODEL" \
  --revision "$REVISION" \
  --dtype "$DTYPE" \
  --gpu-memory-utilization "$GPU_MEMORY" \
  --max-num-seqs "$MAX_NUM_SEQS" \
  --max-model-len "$MAX_MODEL_LEN" \
  --limit-mm-per-prompt "$MM_LIMIT"

# vLLM 0.16.0 has stable Qwen3-ASR support, but eager mode avoids the MRoPE
# torch.compile startup failure seen by early Qwen3-ASR deployments.
if [ "${QWEN3_ASR_ENFORCE_EAGER:-true}" = true ]; then
  set -- "$@" --enforce-eager
fi

if [ -n "$API_KEY" ]; then
  set -- "$@" --api-key "$API_KEY"
fi

exec "$@"
