#!/data/data/com.termux/files/usr/bin/bash
# Wrapper espeak-ng com a interface de linha de comando do piper, para o
# saci ter TTS no Termux sem o binário do piper (o release é glibc-only).
#
# Shebang Termux de propósito: este script é EXECUTADO pelo saci (kernel
# lê o shebang) — /usr/bin/env não existe no Android e daria exit 127.
#
# Interface emulada (ver src/tts.cpp):
#   piper --model <voz.onnx> --output_file <arq.wav> --stdin   (texto no stdin)
# --model/--stdin são aceitos e ignorados; o sotaque vem do espeak (-v pt-br).
set -u

out=""
while [ $# -gt 0 ]; do
    case "$1" in
        --output_file) out="${2:-}"; shift 2 ;;
        *) shift ;;
    esac
done

if [ -z "$out" ]; then
    echo "termux_tts_espeak: falta --output_file" >&2
    exit 64
fi

ESPEAK_BIN="$(command -v espeak-ng || command -v espeak || true)"
if [ -z "$ESPEAK_BIN" ]; then
    echo "termux_tts_espeak: nem espeak-ng nem espeak encontrados no PATH" >&2
    echo "  instale com: pkg install -y espeak" >&2
    exit 69
fi

text=$(cat)
# -s 170: ritmo de fala; o SyncFit ajusta velocidade/posição depois (atempo).
exec "$ESPEAK_BIN" -v pt-br -s 170 -w "$out" -- "$text"
