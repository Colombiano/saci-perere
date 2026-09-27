#!/data/data/com.termux/files/usr/bin/bash
# Shim de 'ollama run' sobre um llama-server PERSISTENTE (Termux).
#
# Por que server: carregar o GGUF de ~470 MB do disco a cada segmento sai
# a ~20 s/traducao; com o server quente na RAM, cada chamada e' um POST
# local de poucos segundos. O server fica de pe entre rodadas do saci.
#
# Por que retry: o server pode travar, ser morto pelo Android (OOM) ou o
# aparelho pode dormir no meio do POST (curl exit 28 = timeout). O shim
# ressobe o server e repete o prompt (ate 3x) em vez de derrubar o saci.
#
# No config Lua:
#   translate_backend = "qwen",
#   llm_cmd           = "<repo>/tools/termux_ollama_shim.sh",
#   llm_model         = "qualquer"      (ignorado; o modelo e' o arquivo GGUF)
#
# Modelo: $SACI_GGUF (default ~/.local/share/saci/qwen.gguf).
# Logs: server em ~/.local/share/saci/llama-server.log, shim em shim-debug.log.
# Dica rural: rode 'termux-wake-lock' antes de sessoes longas (tela apagada
# congela o Termux e estoura os timeouts).
set -euo pipefail

DBG="$HOME/.local/share/saci/shim-debug.log"
{
    echo "=== $(date) | argv: $*"
    echo "    PATH=$PATH"
} >> "$DBG" 2>/dev/null || true
log() { echo "    $*" >> "$DBG" 2>/dev/null || true; }

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

healthy() { curl -s -o /dev/null --max-time 5 "$BASE/health"; }

boot_server() {
    pkill -f "llama-serve[r].*--port $PORT" 2>/dev/null || true
    nohup llama-server -m "$GGUF" --port "$PORT" -ngl 0 -c 2048 \
        > "$LOG" 2>&1 &
    for _ in $(seq 1 240); do
        healthy && return 0
        sleep 1
    done
    return 1
}

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

for attempt in 1 2 3; do
    if ! healthy; then
        echo "shim: server fora do ar (tentativa $attempt) — resubindo…" >&2
        log "server morto; ressubo (tentativa $attempt)"
        boot_server || { echo "shim: llama-server não subiu (log: $LOG)" >&2; exit 69; }
    fi
    rc=0
    resp=$(curl -s --max-time 240 -w $'\n%{http_code}' \
        "$BASE/v1/chat/completions" \
        -H 'Content-Type: application/json' \
        -d "$payload") || rc=$?
    if [ "$rc" -ne 0 ]; then
        log "curl rc=$rc (tentativa $attempt)"
        echo "shim: curl falhou com $rc (tentativa $attempt)" >&2
        continue
    fi
    http=${resp##*$'\n'}
    body=${resp%$'\n'*}
    log "http=$http (tentativa $attempt)"
    if [ "$http" != "200" ]; then
        echo "shim: HTTP $http — $(printf '%s' "$body" | head -c 300)" >&2
        continue
    fi
    if printf '%s' "$body" | python3 -c '
import json, sys
data = json.load(sys.stdin)
if "choices" not in data:
    print("shim: resposta sem choices: " + json.dumps(data)[:300], file=sys.stderr)
    sys.exit(1)
print(data["choices"][0]["message"]["content"])'; then
        exit 0
    fi
done
echo "shim: esgotadas as 3 tentativas (log do server: $LOG)" >&2
exit 1
