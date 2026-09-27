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
    echo "  baixe no Wi-Fi (uma linha; retoma se a rede cair):" >&2
    echo "  bash tools/termux_get_model.sh" >&2
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

# Templates antigos do termux_setup.sh NAO tinham linha llm_cmd: o sed de
# substituicao virava no-op silencioso e o saci caia no default "ollama"
# (exit 127 no execvp). Se a linha nao existe, INSERE antes do '}' final.
if ! grep -q 'llm_cmd' "$CFG"; then
    sed -i "\$i\\  llm_cmd = \"$SHIM\"," "$CFG"
fi
# mesmo tratamento para translate_backend ausente (improvavel, mas barato)
if ! grep -q 'translate_backend' "$CFG"; then
    sed -i "\$i\\  translate_backend = \"qwen\"," "$CFG"
fi

# Verifica AS DUAS substituicoes NO ARQUIVO (o echo acima nao prova nada).
# Caminho com caracteres regex exige grep -F.
if ! grep -q 'translate_backend *= *"qwen"' "$CFG" \
   || ! grep -qF "llm_cmd = \"$SHIM\"" "$CFG"; then
    echo "termux_config_qwen: FALHA ao atualizar $CFG — conteúdo atual:" >&2
    grep -nE 'translate_backend|llm_cmd|llm_model' "$CFG" >&2
    exit 1
fi
echo "OK: backend qwen ativo em $CFG"
grep -nE 'translate_backend|llm_cmd' "$CFG"
echo "     GGUF    = $GGUF"
echo "  (backup do config original em $CFG.bak)"
