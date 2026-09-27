#!/usr/bin/env bash
# Shim de 'ollama run' sobre llama-cli — backend Qwen do saci no Termux.
#
# No config Lua:
#   translate_backend = "qwen",
#   llm_cmd           = "<repo>/tools/termux_ollama_shim.sh",
#   llm_model         = "qualquer-nome"   (ignorado; o modelo é o arquivo GGUF)
#
# Modelo GGUF: $SACI_GGUF (default ~/.local/share/saci/qwen.gguf). Baixe UMA
# vez no Wi-Fi (ex.: Qwen2.5-0.5B-Instruct Q4_K_M, ~400 MB, do HuggingFace).
set -euo pipefail

if [ "${1:-}" != "run" ]; then
    echo "shim: só entendo 'ollama run <modelo>'" >&2
    exit 64
fi

GGUF="${SACI_GGUF:-$HOME/.local/share/saci/qwen.gguf}"
if [ ! -f "$GGUF" ]; then
    echo "shim: modelo GGUF não encontrado em $GGUF" >&2
    echo "      baixe um Qwen2.5-0.5B-Instruct Q4_K_M do HuggingFace para lá" >&2
    exit 66
fi

LLAMA="$(command -v llama-cli || command -v main || true)"
if [ -z "$LLAMA" ]; then
    echo "shim: llama-cli não encontrado (pkg install llama-cpp)" >&2
    exit 69
fi

prompt=$(cat)
# So' flags estaveis do llama-cli: -m (modelo), -p (prompt one-shot; o
# processo imprime a resposta e sai), -ngl 0 (CPU). Sem --no-display:
# o nome dessa flag varia entre versoes e quebraria o exec.
exec "$LLAMA" -m "$GGUF" -p "$prompt" -ngl 0
