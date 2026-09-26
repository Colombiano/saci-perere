# Saci Pererê — Projeto, Esqueleto e Ontologia

**Dublagem TTS open source de vídeos do YouTube com disco mínimo e banda rural mínima.**
C++20 · metaprogramação · move semantics · coroutines · um poquinho de Lua.

---

## 1. O projeto

`saci` resolve o seguinte problema: assistir vídeos do YouTube "dublados" em
português em uma máquina com **pouco espaço em disco** e **internet rural
lenta**, usando apenas software open source.

A ideia central (decisão de arquitetura): **nunca baixar o vídeo inteiro**.
O pipeline é:

```
legenda (.srt, KBs)
   └─► tradução (pt-BR, offline Argos/NLLB ou LLM externo)
        └─► TTS por segmento (Piper — neural, tempo real até em CPU fraca)
             └─► sincronização (atempo limitado + correção de drift)
                  └─► narracao.mp3 (~15 MB/hora)  [única coisa grande em disco]
                       │
vídeo-only do YouTube ──┴─► ffmpeg mux ─► player (mpv)   [tudo em pipes]
```

O áudio original do YouTube é **descartado por design** (seletor
`bv*[acodec=none]`): a narração TTS o substitui, e o seletor de qualidade
escolhe o maior degrau de vídeo que cabe na banda estimada com folga de 25%.

---

## 2. Estrutura do esqueleto

```
saci/
├── CMakeLists.txt          # C++20; Lua opcional via -DSACI_WITH_LUA=ON
├── README.md
├── EXPLICACAO.md           # este arquivo
├── docs/
│   └── ONTOLOGY.md         # ontologia formal (classes, relações, invariantes)
├── lua/
│   └── default_config.lua  # políticas declarativas (mapeia 1:1 p/ Config)
├── include/saci/
│   ├── concepts.hpp        # concept MovableBuffer, SegmentSource, etc.
│   ├── coro.hpp            # Task<T>/Task<void> (coroutines C++20)
│   ├── segment.hpp         # Segment move-only (unidade em trânsito)
│   ├── ring_buffer.hpp     # RingBuffer<Seg,N> — backpressure (rede rural)
│   ├── subtitle.hpp        # TranscriptSegment, SubtitleTrack, parse SRT
│   ├── translate.hpp       # concept TranslationEngine, ArgosEngine
│   ├── tts.hpp             # Utterance, PiperEngine, NarrationPlan/Assembler
│   ├── sync.hpp            # SyncPolicy, SyncFitter, DriftCorrector
│   ├── stream.hpp          # QualityLadder, seletor de formato yt-dlp
│   ├── muxer.hpp           # MuxSession, MuxHandle (3 processos em pipes)
│   ├── config.hpp          # Config (fonte de verdade: Lua ou chave=valor)
│   ├── proc.hpp            # run_capture / run_quiet (fork/exec/stdout)
│   └── orchestrator.hpp    # enum Stage + Orchestrator (máquina de estados)
└── src/                    # 9 translation units (main, proc, subtitle,
                            # stream, translate, tts, sync, muxer, config,
                            # orchestrator)
```

Build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua --max-height 360 --workdir /tmp/saci
```

Dependências de runtime: `yt-dlp`, `ffmpeg`/`ffprobe`, `piper`,
`argos-translate` (ou tradutor alternativo), `mpv`.

---

## 3. Como os requisitos de linguagem foram aplicados

### 3.1 Move semantics (sem cópias no caminho quente)

- `Segment` guarda bytes em `std::unique_ptr<std::byte[]>`; cópia é
  **erro de compilação** (`Segment(const Segment&) = delete`).
- A constraint é genérica via concept `MovableBuffer`: qualquer tipo com
  `data()/size()/clear()` e `std::movable` pode circular no pipeline.
- `RingBuffer`, `Task<T>`, `MuxHandle` seguem a mesma regra: handles
  transferíveis, nunca duplicados (RAII com destrutor que faz `waitpid`/`close`).

### 3.2 Metaprogramação

- **Concepts**: `MovableBuffer`, `SegmentSource`, `TranslationEngine`,
  `TTSEngine` — os motores (tradutor, TTS) são trocáveis por qualquer tipo
  que satisfaça o conceito, sem herança.
- **Templates com constraints**: `RingBuffer<Seg, N> requires std::movable<Seg>`.
- **Design constexpr/ranges**: `QualityLadder::select` documenta a evolução
  para tabela `std::array constexpr` + `std::ranges` quando a ladder for
  compilada em vez de configurada.

### 3.3 Coroutines C++20

- `Task<T>` / `Task<void>` (promise type própria) orquestram as etapas
  I/O-bound **sem threads**: cada estágio é uma corrotina que produz um valor
  ou propaga exceção via `unhandled_exception`.
- Eager scheduling (`initial_suspend_never`) — sem event loop externo, por
  simplicidade de esqueleto; a migração para `co_await` em I/O real fica
  localizada no `Task`.

### 3.4 Um poquinho de Lua

- Opcional (`-DSACI_WITH_LUA=ON`, sol2): o script retorna uma tabela que
  preenche `saci::Config` — políticas por-site/per-canal sem recompilar.
- Sem Lua, um parser mínimo `chave = valor` (mesmas chaves) mantém o
  binário funcional. Ver tabela de mapeamento na ontologia.

---

## 4. Ontologia

A versão formal está em `docs/ONTOLOGY.md` (com diagramas Mermaid).
Resumo:

**Classes de domínio**: `VideoSource`, `SubtitleTrack`, `TranscriptSegment`,
`Translation`, `Utterance`, `NarrationPlan::Item`, `NarrationTrack`,
`QualityLadder`, `Segment`, `RingBuffer`, `MuxSession`, `Config`.

**Cadeia de relações**:

```
VideoSource --has--> SubtitleTrack --yields--> TranscriptSegment
TranscriptSegment --translatesTo(1:1)--> Translation
Translation --synthesizedAs(1:1)--> Utterance
Utterance + TranscriptSegment --fittedBy--> NarrationPlan::Item
NarrationPlan::Item* --composes--> NarrationTrack
VideoSource --feeds(video-only)--> MuxSession <--feeds-- NarrationTrack
MuxSession --pipes--> Player      Config --governs--> todos os estágios
```

**Máquinas de estado**:

1. *Pipeline*: `FetchSubs → Parse → Translate → Tts → SyncFit → Render →
   StreamMux → Done/Error`.
2. *Segmento no anel*: `Pending → Fetching → Buffered → Consumed | Evicted`;
   `push()==false` é o sinal de backpressure (anel cheio → produtor espera).

**Invariantes (garantias)**:

1. Janelas SRT **imutáveis** — tradução nunca altera tempos; adaptação
   acontece só no áudio TTS.
2. `atempo ∈ [0.85, 1.30]` — limites de *naturalidade* da fala, não do
   `atempo` técnico do ffmpeg; fora disso, `overflow=true` (fala truncada).
3. Drift acumulado > **300 ms** ⇒ replanejamento (`DriftCorrector`).
4. Duração da locução **medida** com `ffprobe`, nunca assumida.
5. Tudo move-only (`MovableBuffer`); cópia é erro de compilação.
6. **Um único muxer** (ffmpeg) — dessincronia A/V acumulada é impossível.
7. **Zero-disco**: só legendas (KB) + `narracao.mp3` (~15 MB/h) + anel de
   buffer residem em disco; o vídeo flui em pipes.
8. Áudio original **descartado**: seletor exige `[acodec=none]`, mux usa
   `-map 0:v -map 1:a`.

---

## 5. Pontos de atenção (esqueleto ≠ produção)

1. **Pipe bidirecional pendente**: `run_capture`/`run_quiet` capturam stdout
   mas ainda não alimentam stdin do tradutor/TTS (o texto vai de forma
   simplificada). O *contrato* das classes (`translate(text)`,
   `synthesize(text)`) já está correto — falta o `run_capture2` com pipe
   de entrada. É o próximo passo natural.
2. **yt-dlp como subprocesso**: a extração de URLs segue fora do C++
   (divisão honesta de trabalho — o C++ orquestra; yt-dlp/ffmpeg fazem o
   demux/mux pesado). Alternativa futura: libcurl + API Piped/Invidious.
3. **Controle adaptativo em runtime**: a estimativa de banda → troca de degrau
   está modelada (`QualityLadder::select`) mas o loop de feedback em
   background (thread/co_await medindo taxa do pipe) ainda não existe.
4. **Tradução em lote**: chamar `argos-translate` por segmento é lento; o
   ideal é traduzir o SRT inteiro numa passada (o formato preserva índices).
5. **Erros**: falhas de rede no `StreamMux` não tentam re-conectar; o modo
   `fifo_mode` (sobreviver a quedas com fila circular em disco) está no
   `Config`/`RingBuffer` mas o re-spawn do mux é futuro.
6. **Segurança/legal**: respeitar os termos do YouTube e direitos autorais —
   uso pessoal, acessibilidade e estudo; legendas/dublagem são conteúdo
   protegido.

---

## 6. Roadmap sugerido

| Ordem | Tarefa | Arquivos |
|---|---|---|
| 1 | `run_capture2` com stdin+stdout | `proc.hpp/cpp` |
| 2 | Tradução em lote do SRT | `translate.cpp`, `orchestrator.cpp` |
| 3 | Loop adaptativo de banda → degrau | `stream.hpp`, novo `src/bw_probe.cpp` |
| 4 | Re-spawn do mux (modo fifo) | `muxer.cpp`, `orchestrator.cpp` |
| 5 | `co_await` real sobre pipes (io_uring) | `coro.hpp` |
