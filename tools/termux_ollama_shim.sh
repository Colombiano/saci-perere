#!/data/data/com.termux/files/usr/bin/bash
# Shim de 'ollama run' sobre um llama-server PERSISTENTE (Termux).
#
# Por que server: carregar o GGUF de ~470 MB do disco a cada segmento sai
# a ~20 s/traducao; com o server quente na RAM, cada chamada e' um POST
# local de poucos segundos. O server fica de pe entre rodadas do saci.
#
# No config Lua:
#   translate_backend = "qwen",
#   llm_cmd           = "<repo>/tools/termux_ollama_shim.sh",
#   llm_model         = "qualquer"      (ignorado; o modelo e' o arquivo GGUF)
#
# Modelo: $SACI_GGUF (default ~/.local/share/saci/qwen.gguf). Log do server:
# ~/.local/share/saci/llama-server.log
set -euo pipefail

[ "${1:-}" = "run" ] || { echo "shim: só entendo 'ollama run <modelo>'" >&2; exit 64; }

GGUF="${SACI_GGUF:-$HOME/.local/share/saci/qwen.gguf}"
[ -f "$GGUF" ] || { echo "shim: GGUF não encontrado em $GGUF" >&2; exit 66; }

PORT="${SACI_LLAMA_PORT:-18099}"
BASE="http://127.0.0.1:$PORT"
SACI_HOME_DIR="$HOME/.local/share/saci"
LOG="$SACI_HOME_DIR/llama-server.log"
mkdir -p "$SACI_HOME_DIR"

if ! command -v llama-server > /dev/null 2>&1; then
    echo "shim: llama-server não encontrado (pkg install llama-cpp)" >&2
    exit 69
fi

# (Re)sobe o server se não estiver respondendo (ex.: sessão anterior caiu).
if ! curl -s -o /dev/null --max-time 5 "$BASE/health"; then
    # mata zumbi da porta (bracket trick: padrão não casa com esta própria linha)
    pkill -f "llama-serve[r].*--port $PORT" 2>/dev/null || true
    nohup llama-server -m "$GGUF" --port "$PORT" -ngl 0 -c 2048 \
        > "$LOG" 2>&1 &
    for _ in $(seq 1 180); do
        curl -s -o /dev/null --max-time 2 "$BASE/health" && break
        sleep 1
    done
    if ! curl -s -o /dev/null --max-time 2 "$BASE/health"; then
        echo "shim: llama-server não respondeu em $BASE (log: $LOG)" >&2
        exit 69
    fi
fi

prompt=$(cat)

# /v1/chat/completions aplica o chat template do modelo (Qwen-Instruct)
# e devolve só a mensagem — exatamente o que o QwenEngine espera.
payload=$(python3 -c '
import json, sys
print(json.dumps({
    "model": "qwen2.5",
    "messages": [{"role": "user", "content": sys.argv[1]}],
    "max_tokens": 256,
    "temperature": 0.1,
}))' "$prompt")

# Status HTTP + corpo separados: se o server devolver {"error": ...}
# (payload inválido, endpoint ausente, modelo carregando), mostramos o
# motivo real em vez de um KeyError críptico no parser.
resp=$(curl -s --max-time 300 -w $'\n%{http_code}' \
    "$BASE/v1/chat/completions" \
    -H 'Content-Type: application/json' \
    -d "$payload")
http=${resp##*$'\n'}
body=${resp%$'\n'*}
if [ "$http" != "200" ]; then
    echo "shim: HTTP $http — $(printf '%s' "$body" | head -c 400)" >&2
    exit 1
fi
printf '%s' "$body" | python3 -c '
import json, sys
data = json.load(sys.stdin)
if "choices" not in data:
    print("shim: resposta sem choices: " + json.dumps(data)[:400], file=sys.stderr)
    sys.exit(1)
print(data["choices"][0]["message"]["content"])'
