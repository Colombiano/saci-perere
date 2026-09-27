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
  I/O-bound **sem threads dedicadas por etapa**: cada estágio é uma corrotina que produz um valor
  ou propaga exceção via `unhandled_exception`.
- Eager scheduling (`initial_suspend_never`) — sem event loop externo, por
  simplicidade de esqueleto; a migração para `co_await` em I/O real fica
  localizada no `Task`.
- **v0.4 — o `co_await` virou real**: o `Reactor` (poll + `jthread`/
  `stop_token`) retoma corrotinas em prontidão de fd; a bomba do mux
  (`pump_fds`) é uma `Task<int>` cujas leituras/escritas são
  `co_await Readable/Writable`. Término sinalizado com
  `std::atomic::wait/notify` — sem busy-loop. Ver ADR-002.

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
| 2 | Tradução em lote do SRT | `translate.cpp`, `orchestrator.cpp`, `tools/argos_bridge.py` | ✅ v0.2/v0.6 (batch CLI + fallback; ponte Python = caminho rápido real, 1 carga p/ N segmentos) |
| 3 | Loop adaptativo de banda → degrau | `stream.hpp`, `src/bw_probe.cpp` | ✅ v0.3 (pump mede vazão real; FFT/Haar; troca mid-stream fica p/ fonte adaptativa) |
| 4 | Re-spawn do mux (modo fifo) | `muxer.cpp`, `orchestrator.cpp` | ✅ v0.3/v0.5 (backoff + `bw_estimate.txt`; **resume fino por Range com stall detect** — ver ADR-003) |
| 5 | `co_await` real sobre pipes (io_uring) | `coro.hpp`, `reactor.hpp/cpp`, `muxer.cpp` | ✅ v0.4 (Reactor poll próprio — ver ADR-002; bomba virou `Task<int>`; bugs CLOEXEC e self-pipe) |
| 6 | Políticas por-site em Lua (`sites`) | `config.hpp/cpp`, `lua/default_config.lua` | ✅ v0.2 (sol2 vendored; primeira regra que casa vence) |
| 7 | Idiomas `es` e `zh` além de `pt-BR` | pares `source_lang`/`target_lang` | roadmap de idiomas |
| 8 | Backend de tradução por LLM (Qwen; Apache 2.0, exceto 3B/72B) | `translate.hpp/cpp`, `orchestrator.cpp` | ✅ v0.3 (`QwenEngine` via Ollama; `translate_backend` em Lua) |

---

## 7. Decisões (ADR)

Registro de decisões de arquitetura com contexto, fatos verificados e
gatilhos objetivos de reavaliação. Regra da casa: uma decisão só é reaberta
com um fato novo (número, licença, dependência) — não com gosto.

### ADR-001 (v0.3): DSP própria (Cooley-Tukey radix-2) em vez do KFR

**Status:** aceita · **Data:** 2026-09-26

**Contexto.** O probe de banda da v0.3 usa FFT (N=64, 1× a cada janela de
2 s), wavelet de Haar e estimador de percentil. Pergunta: adotar o
framework [kfrlib/kfr](https://github.com/kfrlib/kfr) no lugar da
implementação própria seria mais robusto e mais rápido?

**Decisão.** Manter a implementação própria. O KFR não é adotado.

**Fatos verificados.**

1. **Falsa dicotomia.** Cooley-Tukey é a *família de algoritmos* que o KFR
   implementa (otimizada com split-radix, SIMD e dispatch). A escolha real
   é hand-rolled radix-2 vs. framework — e a nossa `fft64` já é
   Cooley-Tukey iterativa com bit-reversal.
2. **Licença incompatível com o projeto.** O KFR é dual
   GPLv2+/comercial. O saci é MIT. Adotar o KFR forçaria relicenciar o
   projeto para GPLv2+ ou comprar licença comercial.
3. **O DSP não é gargalo — medido, não opinado.** Benchmark na máquina
   alvo (`g++ -O2`, N=64): **0.429 µs/FFT**. Chamada 1× a cada 2 s ⇒
   **0.000021% de 1 núcleo**. O gargalo do pipeline é I/O de rede (o pump
   esperando a banda rural). Ganho de 100× na FFT mudaria 0.04% do nada.
4. **Build rural.** A fft64 é C++ puro, zero dependências, compila em
   GCC 12+ sem nada externo. O KFR adicionaria uma biblioteca grande
   (ainda que sem deps externas) para uma função de 40 linhas já coberta
   pelo selftest (senoide → periodicidade 1.000; constante → 0.000).

**Consequências.**

- ✅ Projeto segue MIT, build intacto, superfície de dependência não cresce.
- ✅ Selftest continua validando o DSP localmente, sem fixtures externas.
- ❌ Perdemos: tamanhos não-potência-de-2, filtros IIR/FIR prontos,
  resampling, SIMD NEON/AVX e a maturidade comprovada do KFR (usado em
  pesquisa de ondas gravitacionais, LIGO/Virgo/KAGRA).

**Gatilhos de reavaliação** (qualquer um, com número novo na mão):

- Janela espectral **> ~4096 pontos** ou taxa de chamada alta (≥ 1 FFT/s).
- Necessidade real de **filtro IIR/FIR** (suavizar a estimativa do probe),
  **resampling** de áudio das utterances ou **DCT**.
- Alvo **ARM/NEON** onde SIMD seja crítico e medido.
- **N** não-potência-de-2 se tornado requisito.

**Caminhos caso um gatilho dispare:** (a) licença comercial do KFR;
(b) relicenciar o saci para GPLv2+; (c) usar a FFT da `libavutil` do
FFmpeg — **já é dependência do projeto** — via subprocesso ou linkagem,
antes de puxar um framework novo.

### ADR-002 (v0.4): Reactor poll próprio em vez de io_uring/liburing

**Status:** aceita · **Data:** 2026-09-26

**Contexto.** O item 5 do roadmap pedia "`co_await` real sobre pipes
(io_uring)". A v0.4 entrega o `co_await` real, mas sobre um reator
`poll(2)` próprio em vez de io_uring via liburing. Por quê?

**Decisão.** Reator poll próprio (`include/saci/reactor.hpp`,
`src/reactor.cpp`). io_uring não é adotado.

**Fatos verificados.**

1. **Dependência.** liburing é mais uma dependência de build/link em um
   projeto cujo argumento é justamente build trivial em máquina rural
   (GCC 12+, zero deps além do stdlib — o sol2 vendored já é a exceção
   declarada). io_uring puro via syscalls é viável, mas adiciona dezenas
   de linhas de setup de rings/opcodes para um ganho que não medimos.
2. **Volume de fds.** O saci multiplexa **2** pipes (yt-dlp→bomba,
   bomba→ffmpeg). poll() escala mal depois de ~centenas de fds; io_uring
   brilha em milhares. Estamos três ordens de grandeza abaixo do ponto
   em que a escolha importa.
3. **Portabilidade.** poll(2) é POSIX; io_uring é Linux-only e muda de
   comportamento entre kernels — exatamente o tipo de variável que uma
   máquina rural antiga não perdoa.
4. **Coroutines primeiro.** O objetivo do item era `co_await` real sobre
   pipes — o reator entrega isso com ~120 linhas auditáveis, e o padrão
   de awaiter (`FdWaiter`) não muda se um dia o backend virar io_uring.

**Consequências.**

- ✅ Build continua zero-dep; selftest cobre reactor + bomba ponta a ponta.
- ❌ Latência de prontidão com timeout de 100 ms (irrelevante para o pump,
  que convive com janelas de 2 s); sem batching de syscalls do io_uring.

**Gatilhos de reavaliação.**

- Fds multiplexados **> ~100** (ex.: filas fifo por canal em disco).
- Necessidade de **zero-copy** (splice/sendfile via io_uring).
- Medição mostrando o poll como gargalo (hoje: 0,000021% do orçamento —
  ver ADR-001).

### ADR-003 (v0.5): Resume Range via curl como subprocesso, não cliente HTTP próprio

**Status:** aceita · **Data:** 2026-09-26

**Contexto.** O resume fino precisa baixar bytes de um offset arbitrário
(`Range: bytes=N-`). Opções: (a) cliente HTTP em C++ próprio (sockets +
parser de resposta), (b) libcurl linkada, (c) `curl` como subprocesso
com `--range N-`, trocando apenas o processo-fonte na cadeia do mux.

**Decisão.** (c) `curl` como subprocesso-fonte nas retomadas.

**Fatos verificados.**

1. **Zero-dep C++ preservado.** O EXPLICACAO já listava libcurl como
   "alternativa futura" para a extração de URLs; um cliente próprio são
   centenas de linhas para parse de HTTP/1.1, redirects, chunked encoding
   e TLS — TLS elimina (a) e (b) na prática rural (certificados,
   SNI, atualização de CA). curl já resolveu tudo isso, em toda máquina.
2. **Troca cirúrgica na cadeia.** A fonte já é um processo na cadeia de
   pipes; trocar yt-dlp por curl na retomada não altera o mux, o ffmpeg
   nem o player. A bomba mede bytes dos dois da mesma forma.
3. **O offset é nosso, não do curl.** `-C -` (continue) do curl depende
   de arquivo em disco — quebraria o zero-disco. `--range N-` com N
   calculado pela bomba (bytes entregues ao ffmpeg + pendências) é
   exato e testável sem rede (fontes falsas no selftest).
4. **Risco assumido e documentado:** o servidor precisa honrar Range
   (206). O googlevideo do YouTube honra; se um dia não honrar, o curl
   recebe 200 com o corpo inteiro — mitigação futura: checar
   `Content-Range` na primeira janela e abortar a retomada se vier 200.

**Gatilhos de reavaliação.**

- Necessidade de métricas/telemetria HTTP finas (status, headers) por
  retomada → curl com `--write-out` cobre 90%.
- TLS pinado ou proxies rurais autenticados → aí sim libcurl ou cliente
  próprio entram na conversa.
