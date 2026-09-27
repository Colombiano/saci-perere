#!/usr/bin/env bash
# Aponta o config do saci para o backend de tradução Qwen (llama.cpp).
# Pré-requisito: modelo GGUF baixado (default ~/.local/share/saci/qwen.gguf;
# sobrescreva com a variável SACI_GGUF). Use: bash tools/termux_config_qwen.sh
set -euo pipefail

CFG="$HOME/.config/saci/config.lua"
GGUF="${SACI_GGUF:-$HOME/.local/share/saci/qwen.gguf}"
SHIM="$(cd "$(dirname "$0")" && pwd)/termux_ollama_shim.sh"

if [ ! -f "$GGUF" ]; then
    echo "termux_config_qwen: modelo GGUF não encontrado em $GGUF" >&2
    echo "  baixe no Wi-Fi com curl (uma linha só; -C - retoma se cair), ex.:" >&2
    echo "  curl -L -C - -o \"$GGUF\" \"https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf\"" >&2
    exit 66
fi
if [ ! -f "$CFG" ]; then
    echo "termux_config_qwen: config não encontrado em $CFG" >&2
    echo "  rode primeiro: bash tools/termux_setup.sh" >&2
    exit 66
fi

cp -n "$CFG" "$CFG.bak" 2>/dev/null || true
sed -i \
    -e 's/translate_backend *= *"argos"/translate_backend = "qwen"/' \
    -e "s|llm_cmd *= *\"[^\"]*\"|llm_cmd = \"$SHIM\"|" \
    "$CFG"

grep -q 'translate_backend *= *"qwen"' "$CFG" || {
    echo "termux_config_qwen: falha ao atualizar $CFG (backup em $CFG.bak)" >&2
    exit 1
}
echo "OK: backend qwen ativo em $CFG"
echo "     llm_cmd = $SHIM"
echo "     GGUF    = $GGUF"
echo "  (backup do config original em $CFG.bak)"
