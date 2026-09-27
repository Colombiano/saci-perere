#!/usr/bin/env bash
# Baixa o modelo GGUF (Qwen2.5-0.5B-Instruct, Apache 2.0, ~400 MB) para o
# saci rodar no Termux. A URL fica AQUI dentro — nada de digitar link longo.
#
#   bash tools/termux_get_model.sh
#
# Se a rede rural cair no meio, rode de novo: o curl -C - retoma do byte
# em que parou. Sobrescreva destino/URL com SACI_GGUF / SACI_MODEL_URL.
set -euo pipefail

GGUF="${SACI_GGUF:-$HOME/.local/share/saci/qwen.gguf}"
URL="${SACI_MODEL_URL:-https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf}"

mkdir -p "$(dirname "$GGUF")"
echo "Baixando $URL"
echo "  -> $GGUF"
curl -L -C - -o "$GGUF" "$URL"
ls -lh "$GGUF"
echo "Pronto. Agora ative o backend: bash tools/termux_config_qwen.sh"
