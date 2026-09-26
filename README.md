# Saci Pererê

**"Dublagem" TTS open source de vídeos do YouTube para português, com disco
mínimo e banda rural mínima.** Como o Saci do folclore: leve, veloz num
redemoinho de pipes — e não deixa rastro no seu disco.

![Licença](https://img.shields.io/github/license/Colombiano/saci-perere)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Status](https://img.shields.io/badge/status-esqueleto%20v0.1-orange)

> ⚖️ **Aviso legal**: o Saci Pererê respeita os Termos de Serviço do YouTube e
> a legislação de direitos autorais. Foi construído para **uso pessoal,
> acessibilidade e estudo**. Ver [DISCLAIMER.md](DISCLAIMER.md).

## O problema

Vídeos que você precisa assistir para estudar não têm faixa de áudio em
português — e você está em ambiente rural: pouco disco, pouca banda. Baixar o
vídeo inteiro em 1080p (ou até 360p) já estoura o orçamento.

## A ideia central

**Nunca baixar o vídeo inteiro.** O pipeline:

```
legenda (.srt, KBs)
   └─► tradução (pt-BR, offline Argos/NLLB ou LLM externo)
        └─► TTS por segmento (Piper — neural, tempo real até em CPU fraca)
             └─► sincronização (atempo limitado + correção de drift)
                  └─► narracao.mp3 (~15 MB/hora)  [única coisa grande em disco]
                       │
vídeo-only do YouTube ──┴─► ffmpeg mux ─► player (mpv)   [tudo em pipes]
```

O áudio original do YouTube é **descartado por design**: a narração TTS o
substitui, e o seletor de qualidade escolhe o maior degrau que cabe na banda
estimada com folga de 25%.

## Requisitos

- Compilador C++20 (GCC 12+ / Clang 15+)
- `yt-dlp`, `ffmpeg` (+ `ffprobe`), `piper` (TTS), `argos-translate` (opcional,
  ou tradução por LLM externo)
- `mpv` (player) — ou redirecione o stdout do mux
- Opcional: Lua 5.4 + sol2 (`-DSACI_WITH_LUA=ON`)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Uso

```bash
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua \
    --max-height 360 \
    --workdir /tmp/saci
```

## Design

- **Move-only por construção**: segmentos de mídia são `std::unique_ptr`
  internamente; cópia é falha de compilação (conceito `MovableBuffer`).
- **Coroutines C++20** (`Task<T>`) orquestram etapas de I/O sem threads.
- **Motores trocáveis por concepts**: `TranslationEngine`, `TTSEngine` —
  sem herança, qualquer tipo que satisfaça o conceito serve.
- **Lua embutida** (opcional): políticas por-site (degrau de qualidade,
  idioma-alvo, overrides) sem recompilar.
- **Sincronização** no `SyncFitter`: `atempo` limitado a [0.85, 1.30] para
  naturalidade; drift acumulado > 300 ms dispara replanejamento.
- **Zero-disco**: o vídeo flui `yt-dlp | ffmpeg | mpv` em pipes; só o áudio TTS
  (~15 MB/h) e as legendas (KB) tocam em disco.

## Estrutura do projeto

```
saci-perere/
├── CMakeLists.txt          # C++20; Lua opcional via -DSACI_WITH_LUA=ON
├── LICENSE                 # MIT — Luiz Paulo Colombiano
├── DISCLAIMER.md           # aviso legal (ToS YouTube, direitos autorais)
├── README.md               # este arquivo
├── EXPLICACAO.md           # documento de design (pipeline, invariantes)
├── docs/
│   └── ONTOLOGY.md         # ontologia formal (classes, relações, Mermaid)
├── lua/
│   └── default_config.lua  # políticas declarativas (mapeiam 1:1 p/ Config)
├── include/saci/           # headers: concepts, coro, segment, ring_buffer,
│                           # subtitle, translate, tts, sync, stream, muxer,
│                           # config, proc, orchestrator
├── src/                    # translation units (main, proc, subtitle,
│                           # stream, translate, tts, sync, muxer, config,
│                           # orchestrator)
└── build/                  # gerado pelo cmake (não versionado)
```

## Estado atual (esqueleto v0.1)

O pipeline ponta-a-ponta já existe e compila; os pontos pendentes de
produção estão listados no [EXPLICACAO.md](EXPLICACAO.md) (seção "Pontos de
atenção"), com roadmap: `run_capture2` com stdin+stdout, tradução em lote do
SRT, loop adaptativo de banda, re-spawn do mux e `co_await` real sobre pipes.

## Licença

Código sob **MIT** — copyright Luiz Paulo Colombiano. Ver [LICENSE](LICENSE).
Fotos/legendas/áudio de terceiros pertencem aos seus detentores; veja o
[DISCLAIMER.md](DISCLAIMER.md).
