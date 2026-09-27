#!/usr/bin/env bash
# Saci Pererê no Android, sem root — instalação pelo Termux (F-Droid).
#
# Uso (de dentro do repo clonado no Termux):
#   pkg install git && git clone https://github.com/Colombiano/saci-perere.git
#   cd saci-perere && bash tools/termux_setup.sh
#
# Faz tudo: dependências (pkg + pip), build com Lua, install em $PREFIX
# (binário + man page) e config do usuário em ~/.config/saci/config.lua.
# TTS = espeak-ng via wrapper com interface do piper; tradução = Argos se o
# pip colaborar, senão o script imprime o caminho llama.cpp + Qwen (shim).
set -euo pipefail

# --- guardas -----------------------------------------------------------------
if [ -z "${PREFIX:-}" ] || [ ! -d "$PREFIX/bin" ]; then
    echo "saci-termux: rode DENTRO do Termux (PREFIX não encontrado)." >&2
    echo "  Instale o Termux pelo F-Droid — a versão da Play Store está" >&2
    echo "  abandonada e não acompanha o Android 15/16." >&2
    exit 1
fi
if [ ! -f CMakeLists.txt ] || ! grep -q "project(saci" CMakeLists.txt 2>/dev/null; then
    echo "saci-termux: rode a partir da RAIZ do repo saci-perere." >&2
    exit 64
fi

echo "== 1/5 pacotes (pkg) =="
pkg update -y
# 'espeak' é o nome do PACOTE no Termux (o binário instalado é espeak-ng).
# Blocos separados de propósito: o apt aborta TODA a linha se um nome não
# existe — assim um erro não derruba as demais dependências.
if ! pkg install -y clang cmake make git ffmpeg mpv curl python python-pip \
    lua54 pkg-config; then
    cat >&2 <<'FIM'
saci-termux: falha ao instalar pacotes essenciais. Se vários pacotes vieram
com "Unable to locate", seu Termux é provavelmente o da PLAY STORE (repo
congelado em 2020). Migre para o Termux do F-Droid e rode de novo.
FIM
    exit 69
fi
pkg install -y espeak || pkg install -y espeak-ng || true
ESPEAK_BIN="$(command -v espeak-ng || command -v espeak || true)"
if [ -z "$ESPEAK_BIN" ]; then
    echo "saci-termux: nenhum TTS encontrado (nem espeak-ng nem espeak)." >&2
    exit 69
fi
echo "TTS: $ESPEAK_BIN"

echo "== 2/5 yt-dlp (pip) =="
pip install --upgrade yt-dlp
YT_DLP="$(command -v yt-dlp)"

echo "== 3/5 build do saci (com Lua) =="
# tira o CMakeCache de uma tentativa anterior falha (reconfigura limpo)
rm -f build-termux/CMakeCache.txt
cmake -B build-termux -DCMAKE_BUILD_TYPE=Release -DSACI_WITH_LUA=ON
cmake --build build-termux -j"$(nproc)"
./build-termux/saci --selftest

echo "== 4/5 install em \$PREFIX (binário + man page) =="
cmake --install build-termux --prefix "$PREFIX"

echo "== 5/5 config do usuário =="
SACI_HOME="$HOME/.local/share/saci"
mkdir -p "$SACI_HOME/voices" "$HOME/.config/saci"
install -m 755 tools/termux_tts_espeak.sh "$SACI_HOME/piper-run"

# Tradução: Argos primeiro. Falhar é comum no Termux (ctranslate2 não tem
# wheel pra Android/aarch64) — nesse caso o aviso no fim orienta o plano B.
TRANSLATE_CMD=""
if pip install argostranslate; then
    TRANSLATE_CMD="$(command -v argos-translate || true)"
fi

cat > "$HOME/.config/saci/config.lua" <<EOF
-- Gerado por tools/termux_setup.sh (Termux/Android). Edite à vontade.
-- workdir default respeita \$TMPDIR (tmp do Termux), como o src/main.cpp.
return {
  max_height       = 360,
  player           = "mpv",
  yt_dlp           = "$YT_DLP",

  sub_lang_pref    = "en",
  source_lang      = "en",
  target_lang      = "pt-BR",

  tts_bin          = "$SACI_HOME/piper-run",  -- espeak-ng (interface piper)
  tts_voice        = "espeak-ng",             -- ignorado pelo wrapper

  translate_cmd    = "${TRANSLATE_CMD:-argos-translate}",
  translate_backend = "argos",

  ring_bytes       = 50 * 1024 * 1024,
  fifo_mode        = true,     -- resume fino por Range: bom em rede móvel
  mux_retries      = 3,
  source_retries   = 3,
}
EOF

cat <<'FIM'
Pronto! Dia a dia:
  saci --selftest
  saci "https://www.youtube.com/watch?v=XXXXXXXXXXX"
  man saci
FIM

if [ -z "$TRANSLATE_CMD" ]; then
    cat >&2 <<'FIM'
⚠ Tradução offline NÃO configurada: o Argos não instalou via pip (comum no
  Termux — o ctranslate2 não tem wheel para Android/aarch64). Plano B, no
  Wi-Fi:
    1) pkg install -y llama-cpp
    2) baixe um GGUF pequeno do HuggingFace (ex.: Qwen2.5-0.5B-Instruct
       Q4_K_M, ~400 MB) para ~/.local/share/saci/qwen.gguf
    3) no ~/.config/saci/config.lua, troque o bloco de tradução por:
         translate_backend = "qwen",
         llm_cmd           = "<repo>/tools/termux_ollama_shim.sh",
FIM
fi
