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

1. ~~**Pipe bidirecional pendente**~~ **(resolvido na v0.2)**:
   `run_capture2` em `src/proc.cpp` alimenta stdin e captura stdout com
   poll() nao-bloqueante (sem deadlock — o selftest exerce 300 KiB).
   Tradutor e TTS viajam por ele; `./build/saci --selftest` valida.
2. **yt-dlp como subprocesso**: a extração de URLs segue fora do C++
   (divisão honesta de trabalho — o C++ orquestra; yt-dlp/ffmpeg fazem o
   demux/mux pesado). Alternativa futura: libcurl + API Piped/Invidious.
3. **Controle adaptativo em runtime**: **(medição resolvida na v0.3)** — o pump
   do mux (`MuxHandle::pump`) mede a vazão real e alimenta o `BandwidthProbe`
   (percentil 25 + Haar p/ quedas + FFT p/ periodicidade); a estimativa é
   logada e persistida em `bw_estimate.txt`. **Pendente:** trocar o degrau NO
   MEIO do stream — streams progressivas do YouTube não admitem troca
   adaptativa (HLS seria outra fonte); hoje o probe orienta a escolha
   inicial da PRÓXIMA sessão.
4. ~~**Tradução em lote**~~ **(resolvido na v0.2)**:
   `ArgosEngine::translate_batch` envia o SRT inteiro numa invocação (caminho
   rápido) e cai no por-segmento quando o CLI devolve contagem de linhas
   inesperada (caminho correto). Janelas SRT intactas em ambos.
5. **Erros**: **(re-spawn resolvido na v0.3)** — em `fifo_mode`, queda prematura
   (yt-dlp/ffmpeg) dispara re-spawn do mux com backoff, até `mux_retries`.
   **Pendente:** resume fino por HTTP Range (não recomeçar do zero); o
   `RingBuffer` em disco segue disponível para essa evolução.
6. **Segurança/legal**: respeitar os termos do YouTube e direitos autorais —
   uso pessoal, acessibilidade e estudo; legendas/dublagem são conteúdo
   protegido.

---

## 6. Roadmap sugerido

| Ordem | Tarefa | Arquivos | Estado |
|---|---|---|---|
| 1 | `run_capture2` com stdin+stdout | `proc.hpp/cpp` | ✅ v0.2 (poll não-bloqueante + selftest 300 KiB) |
| 2 | Tradução em lote do SRT | `translate.cpp`, `orchestrator.cpp` | ✅ v0.2 (batch + fallback por segmento) |
| 3 | Loop adaptativo de banda → degrau | `stream.hpp`, `src/bw_probe.cpp` | ✅ v0.3 (pump mede vazão real; FFT/Haar; troca mid-stream fica p/ fonte adaptativa) |
| 4 | Re-spawn do mux (modo fifo) | `muxer.cpp`, `orchestrator.cpp` | ✅ v0.3 (backoff + `bw_estimate.txt`; resume por Range é roadmap) |
| 5 | `co_await` real sobre pipes (io_uring) | `coro.hpp` | pendente |
| 6 | Políticas por-site em Lua (`sites`) | `config.hpp/cpp`, `lua/default_config.lua` | ✅ v0.2 (sol2 vendored; primeira regra que casa vence) |
| 7 | Idiomas `es` e `zh` além de `pt-BR` | pares `source_lang`/`target_lang` | roadmap de idiomas |
| 8 | Backend de tradução por LLM (Qwen, Apache 2.0) | `translate.hpp/cpp`, `orchestrator.cpp` | ✅ v0.3 (`QwenEngine` via Ollama; `translate_backend` em Lua) |
