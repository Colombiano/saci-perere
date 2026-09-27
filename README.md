# Saci Pererê

**"Dublagem" TTS open source de vídeos do YouTube para português, com disco
mínimo e banda rural mínima.** Como o Saci do folclore: leve, veloz num
redemoinho de pipes — e não deixa rastro no seu disco.

![Licença](https://img.shields.io/github/license/Colombiano/saci-perere)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![versão](https://img.shields.io/badge/vers%C3%A3o-0.3.0-orange)
![selftest](https://img.shields.io/badge/selftest-7%2F7-brightgreen)

> ⚖️ **Aviso legal**: o Saci Pererê respeita os Termos de Serviço do YouTube e
> a legislação de direitos autorais. Foi construído para **uso pessoal,
> acessibilidade e estudo**. Ver [DISCLAIMER.md](DISCLAIMER.md).

## Idiomas / Languages / Idiomas / 语言

- [Português](#português)
- [English](#english)
- [Español](#español)
- [中文](#中文)

---

## Português

### O problema

Vídeos que você precisa assistir para estudar não têm faixa de áudio em
português — e você está em ambiente rural: pouco disco, pouca banda. Baixar o
vídeo inteiro já estoura o orçamento.

### A ideia central

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

### Novidades da v0.2

- **`run_capture2`**: subprocessos com stdin+stdout de verdade (poll
  não-bloqueante — sem deadlock, exercido pelo selftest com 300 KiB).
- **Tradução em lote**: o SRT inteiro vai ao tradutor numa passada (com
  fallback por segmento quando o CLI não colabora).
- **TTS por stdin**: o texto chega ao Piper pelo pipe, sem arquivos
  temporários de texto.
- **Lua de verdade**: o script *retorna* a tabela de políticas (bug do
  esqueleto corrigido), e agora suporta **políticas por-site/per-canal**
  (voz, idioma e degrau de vídeo por padrão de URL). sol2 vendored em
  `vendor/sol` — `-DSACI_WITH_LUA=ON` funciona sem instalar nada.
- **`--selftest`**: 4 testes locais, sem rede (rode `./build/saci --selftest`).

### Novidades da v0.3

- **Probe de banda com DSP** (`src/bw_probe.cpp`): o saci bombeia o vídeo
  entre yt-dlp e ffmpeg e mede a vazão real. Estimativa robusta
  (percentil 25 × folga), **detector de quedas por wavelet de Haar** e
  **FFT radix-2 (N=64)** para medir periodicidade da vazão — links rurais
  têm ciclos de congestionamento, e o pior momento volta.
- **LLM open source chinesa**: `QwenEngine` — Qwen2.5 (Alibaba, Apache
  2.0, gratuito, roda local via Ollama) como backend de tradução. Troca em
  `translate_backend = "qwen"`, sem tocar no código.
- **Re-spawn do mux (modo fifo)**: queda prematura de rede → re-spawn com
  backoff, até `mux_retries`. A estimativa de banda é persistida em
  `bw_estimate.txt` para orientar a próxima sessão.
- **Selftest 7/7**: novos testes de FFT (senoide → periodicidade ≈ 1;
  constante → 0), estimador robusto e Haar.

### Roadmap de idiomas

Hoje o destino é **pt-BR**. Nas versões subsequentes, o mesmo pipeline passa a
atender **espanhol (`es`)** e **chinês (`zh`)** — o par
`source_lang`/`target_lang` já está no config exatamente para isso.

### Requisitos

- Compilador C++20 (GCC 12+ / Clang 15+)
- `yt-dlp`, `ffmpeg` (+ `ffprobe`), `piper` (TTS), `argos-translate` (opcional,
  ou tradução por LLM externo)
- `mpv` (player) — ou redirecione o stdout do mux
- Opcional: headers do Lua 5.4 (sol2 já vendored em `vendor/sol`)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # sem Lua
cmake -B build-lua -DCMAKE_BUILD_TYPE=Release -DSACI_WITH_LUA=ON
cmake --build build -j
./build/saci --selftest
```

### Uso

```bash
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua \
    --max-height 360 \
    --workdir /tmp/saci
```

### Config em Lua (`lua/default_config.lua`)

```lua
return {
  source_lang   = "en",        -- língua da legenda
  target_lang   = "pt-BR",     -- destino da narração (es/zh no roadmap)
  max_height    = 360,
  tts_voice     = "pt_BR-faber-medium.onnx",
  -- políticas por-site: a PRIMEIRA regra que casar com a URL vence
  sites = {
    -- { pattern = "youtube.com/@CanalDeAulas", tts_voice = "outra-voz.onnx" },
    -- { pattern = "example.com/aulas", max_height = 240 },
  },
}
```

### Design

- **Move-only por construção** (`concept MovableBuffer`): cópia é falha de
  compilação.
- **Coroutines C++20** (`Task<T>`) orquestram I/O sem threads.
- **Motores trocáveis por concepts** (`TranslationEngine`, `TTSEngine`).
- **Sincronização**: `atempo ∈ [0.85, 1.30]` (naturalidade); drift > 300 ms
  dispara replanejamento.
- **Zero-disco**: o vídeo flui em pipes; só a narração (~15 MB/h) toca em disco.

A ontologia completa (classes, relações, máquinas de estado, invariantes) está
em [docs/ONTOLOGY.md](docs/ONTOLOGY.md); o documento de design com o roadmap
técnico em [EXPLICACAO.md](EXPLICACAO.md).

### Estrutura do projeto

```
saci-perere/
├── CMakeLists.txt          # C++20; Lua opcional via -DSACI_WITH_LUA=ON
├── LICENSE                 # MIT — Luiz Paulo Colombiano
├── DISCLAIMER.md           # aviso legal (ToS YouTube, direitos autorais)
├── README.md               # este arquivo (PT/EN/ES/ZH)
├── EXPLICACAO.md           # design doc (pipeline, invariantes, roadmap)
├── docs/
│   └── ONTOLOGY.md         # ontologia formal (classes, relações, Mermaid)
├── lua/
│   └── default_config.lua  # políticas declarativas (retorna a tabela Config)
├── vendor/sol/             # sol2 (MIT) vendored — build Lua sem instalar nada
├── include/saci/           # headers: concepts, coro, segment, ring_buffer,
│                           # subtitle, translate, tts, sync, stream, muxer,
│                           # bw_probe, config, proc, orchestrator
├── src/                    # translation units (main, proc, subtitle,
│                           # stream, translate, tts, sync, muxer, config,
│                           # bw_probe, orchestrator)
└── build/                  # gerado pelo cmake (não versionado)
```

### Licença

Código sob **MIT** — copyright Luiz Paulo Colombiano. Ver [LICENSE](LICENSE) e
[DISCLAIMER.md](DISCLAIMER.md).

---

## English

**Open-source TTS "dubbing" of YouTube videos into Portuguese, with minimal
disk and minimal rural bandwidth.** Like the Saci of Brazilian folklore:
lightweight, fast as a whirlwind of pipes — and leaves no trace on your disk.

> ⚖️ **Legal notice**: Saci Pererê respects the YouTube Terms of Service and
> copyright law. It was built for **personal use, accessibility and study**.
> See [DISCLAIMER.md](DISCLAIMER.md).

### The problem

Videos you need to watch in order to learn have no Portuguese audio track —
and you are in a rural area: little disk, little bandwidth. Downloading the
whole video blows the budget.

### The core idea

**Never download the whole video.** The pipeline:

```
subtitle (.srt, KBs)
   └─► translation (pt-BR, offline Argos/NLLB or external LLM)
        └─► per-segment TTS (Piper — neural, real-time even on weak CPUs)
             └─► synchronization (bounded atempo + drift correction)
                  └─► narration.mp3 (~15 MB/hour)  [only big thing on disk]
                       │
video-only from YouTube ┴─► ffmpeg mux ─► player (mpv)   [all in pipes]
```

The original YouTube audio is **discarded by design**: the TTS narration
replaces it, and the quality selector picks the highest rung that fits the
estimated bandwidth with a 25% margin.

### What's new in v0.2

- **`run_capture2`**: real stdin+stdout subprocesses (non-blocking poll — no
  deadlock, exercised by the 300 KiB selftest).
- **Batch translation**: the whole SRT goes to the translator in one pass
  (with per-segment fallback when the CLI doesn't cooperate).
- **TTS over stdin**: text reaches Piper through the pipe, no temp text files.
- **Lua for real**: the script *returns* the policy table (skeleton bug
  fixed), and now supports **per-site/per-channel policies** (voice, language
  and video rung per URL pattern). sol2 vendored in `vendor/sol` —
  `-DSACI_WITH_LUA=ON` works out of the box.
- **`--selftest`**: 4 local tests, no network needed.

### What's new in v0.3

- **DSP bandwidth probe** (`src/bw_probe.cpp`): saci now pumps the video
  between yt-dlp and ffmpeg and measures the real throughput. Robust
  estimator (25th percentile × margin), **Haar-wavelet drop detector** and
  **radix-2 FFT (N=64)** for throughput periodicity — rural links have
  congestion cycles, and the worst moment always comes back.
- **Chinese open-source LLM**: `QwenEngine` — Qwen2.5 (Alibaba, Apache
  2.0, free, runs locally via Ollama) as the translation backend. Switch
  with `translate_backend = "qwen"`, no code changes.
- **Mux re-spawn (fifo mode)**: premature network failure → re-spawn with
  backoff, up to `mux_retries`. The bandwidth estimate is persisted to
  `bw_estimate.txt` to steer the next session.
- **Selftest 7/7**: new FFT tests (sine → periodicity ≈ 1; constant → 0),
  robust estimator and Haar.

### Language roadmap

Today's target is **pt-BR**. In subsequent releases the same pipeline will
serve **Spanish (`es`)** and **Chinese (`zh`)** — the
`source_lang`/`target_lang` pair in the config exists exactly for that.

### Requirements

- C++20 compiler (GCC 12+ / Clang 15+)
- `yt-dlp`, `ffmpeg` (+ `ffprobe`), `piper` (TTS), `argos-translate` (optional,
  or an external LLM translator)
- `mpv` (player) — or redirect the mux stdout
- Optional: Lua 5.4 headers (sol2 is vendored in `vendor/sol`)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # without Lua
cmake -B build-lua -DCMAKE_BUILD_TYPE=Release -DSACI_WITH_LUA=ON
cmake --build build -j
./build/saci --selftest
```

### Usage

```bash
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua \
    --max-height 360 \
    --workdir /tmp/saci
```

### Design

- **Move-only by construction** (`MovableBuffer` concept): copying fails to
  compile.
- **C++20 coroutines** (`Task<T>`) orchestrate I/O without threads.
- **Swappable engines via concepts** (`TranslationEngine`, `TTSEngine`).
- **Sync**: `atempo ∈ [0.85, 1.30]` (naturalness); drift > 300 ms triggers
  re-planning.
- **Zero-disk**: the video flows through pipes; only the narration (~15 MB/h)
  touches disk.

Full ontology (classes, relations, state machines, invariants) in
[docs/ONTOLOGY.md](docs/ONTOLOGY.md); the design document with the technical
roadmap in [EXPLICACAO.md](EXPLICACAO.md).

### License

Code under the **MIT License** — copyright Luiz Paulo Colombiano. See
[LICENSE](LICENSE) and [DISCLAIMER.md](DISCLAIMER.md).

---

## Español

**"Doblaje" TTS open source de vídeos de YouTube al portugués, con disco
mínimo y banda rural mínima.** Como el Saci del folclore brasileño: ligero,
rápido como un remolino de pipes — y no deja rastro en tu disco.

> ⚖️ **Aviso legal**: Saci Pererê respeta los Términos de Servicio de YouTube
> y la legislación de derechos de autor. Fue construido para **uso personal,
> accesibilidad y estudio**. Ver [DISCLAIMER.md](DISCLAIMER.md).

### El problema

Los vídeos que necesitas ver para estudiar no tienen pista de audio en
portugués — y estás en zona rural: poco disco, poca banda. Descargar el vídeo
completo ya rompe el presupuesto.

### La idea central

**Nunca descargar el vídeo completo.** El pipeline:

```
subtítulo (.srt, KBs)
   └─► traducción (pt-BR, Argos/NLLB offline o LLM externo)
        └─► TTS por segmento (Piper — neuronal, en tiempo real incluso en CPU débil)
             └─► sincronización (atempo limitado + corrección de drift)
                  └─► narracion.mp3 (~15 MB/hora)  [única cosa grande en disco]
                       │
vídeo-only de YouTube ──┴─► ffmpeg mux ─► player (mpv)   [todo en pipes]
```

El audio original de YouTube se **descarta por diseño**: la narración TTS lo
reemplaza, y el selector de calidad elige el escalón más alto que cabe en la
banda estimada con un margen del 25%.

### Novedades de la v0.2

- **`run_capture2`**: subprocessos con stdin+stdout de verdad (poll no
  bloqueante — sin deadlock, ejercido por el selftest de 300 KiB).
- **Traducción en lote**: el SRT completo va al traductor en una sola pasada
  (con respaldo por segmento cuando el CLI no coopera).
- **TTS por stdin**: el texto llega a Piper por el pipe, sin archivos
  temporales de texto.
- **Lua de verdad**: el script *devuelve* la tabla de políticas (bug del
  esqueleto corregido) y ahora soporta **políticas por sitio/canal** (voz,
  idioma y escalón de vídeo por patrón de URL). sol2 incluido en
  `vendor/sol` — `-DSACI_WITH_LUA=ON` funciona sin instalar nada.
- **`--selftest`**: 4 pruebas locales, sin red.

### Novedades de la v0.3

- **Probe de banda con DSP** (`src/bw_probe.cpp`): saci bombea el vídeo
  entre yt-dlp y ffmpeg y mide el caudal real. Estimador robusto
  (percentil 25 × margen), **detector de caídas por wavelet de Haar** y
  **FFT radix-2 (N=64)** para medir la periodicidad del caudal — los
  enlaces rurales tienen ciclos de congestión, y el peor momento vuelve.
- **LLM open source china**: `QwenEngine` — Qwen2.5 (Alibaba, Apache
  2.0, gratuito, corre local vía Ollama) como backend de traducción. Se
  activa con `translate_backend = "qwen"`, sin tocar el código.
- **Re-spawn del mux (modo fifo)**: caída prematura de red → re-spawn con
  backoff, hasta `mux_retries`. La estimación de banda se guarda en
  `bw_estimate.txt` para orientar la próxima sesión.
- **Selftest 7/7**: nuevas pruebas de FFT (senoide → periodicidad ≈ 1;
  constante → 0), estimador robusto y Haar.

### Roadmap de idiomas

Hoy el destino es **pt-BR**. En versiones posteriores, el mismo pipeline
atenderá **español (`es`)** y **chino (`zh`)** — el par
`source_lang`/`target_lang` ya está en la config exactamente para eso.

### Requisitos

- Compilador C++20 (GCC 12+ / Clang 15+)
- `yt-dlp`, `ffmpeg` (+ `ffprobe`), `piper` (TTS), `argos-translate` (opcional,
  o traducción por LLM externo)
- `mpv` (player) — o redirige el stdout del mux
- Opcional: cabeceras de Lua 5.4 (sol2 ya está en `vendor/sol`)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # sin Lua
cmake -B build-lua -DCMAKE_BUILD_TYPE=Release -DSACI_WITH_LUA=ON
cmake --build build -j
./build/saci --selftest
```

### Uso

```bash
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua \
    --max-height 360 \
    --workdir /tmp/saci
```

### Diseño

- **Move-only por construcción** (concepto `MovableBuffer`): copiar es error
  de compilación.
- **Coroutines C++20** (`Task<T>`) orquestan I/O sin threads.
- **Motores intercambiables por concepts** (`TranslationEngine`, `TTSEngine`).
- **Sincronización**: `atempo ∈ [0.85, 1.30]` (naturalidad); drift > 300 ms
  dispara replanificación.
- **Zero-disco**: el vídeo fluye en pipes; solo la narración (~15 MB/h) toca
  disco.

La ontología completa está en [docs/ONTOLOGY.md](docs/ONTOLOGY.md); el
documento de diseño con el roadmap técnico en [EXPLICACAO.md](EXPLICACAO.md).

### Licencia

Código bajo **MIT** — copyright Luiz Paulo Colombiano. Ver
[LICENSE](LICENSE) y [DISCLAIMER.md](DISCLAIMER.md).

---

## 中文

**面向葡萄牙语的开源 YouTube 视频 TTS"配音"工具 —— 磁盘占用最小、
适配农村低带宽网络。** 就像巴西民间传说里的 Saci:轻巧、像管道旋风一
样迅捷 —— 在你的磁盘上不留下痕迹。

> ⚖️ **法律声明**:Saci Pererê 遵守 YouTube 服务条款和著作权法律。本工
> 具仅为**个人使用、无障碍和学习**而构建。详见
> [DISCLAIMER.md](DISCLAIMER.md)。

### 问题所在

你需要观看学习的外语视频没有葡萄牙语音轨 —— 而你在农村地区:磁盘
小、带宽低。下载整个视频就会超出预算。

### 核心思路

**永远不下载完整视频。** 处理流水线:

```
字幕 (.srt, 仅 KB 级)
   └─► 翻译 (pt-BR,离线 Argos/NLLB 或外部 LLM)
        └─► 逐段 TTS (Piper —— 神经网络,低端 CPU 也能实时)
             └─► 同步 (受限的 atempo + 漂移校正)
                  └─► narracao.mp3 (~15 MB/小时)  [磁盘上唯一的大文件]
                       │
来自 YouTube 的纯视频流 ─┴─► ffmpeg 混流 ─► 播放器 (mpv)   [全部走管道]
```

YouTube 原始音轨**按设计被丢弃**:TTS 旁白取而代之,画质选择器会挑出
在估计带宽(留 25% 余量)内能放下的最高档位。

### v0.2 新特性

- **`run_capture2`**:真正的 stdin+stdout 双向子进程(非阻塞 poll —— 无
  死锁,300 KiB 自测覆盖)。
- **批量翻译**:整个 SRT 一次送入翻译器(CLI 不配合时自动回退到逐段)。
- **TTS 走 stdin**:文本经管道直达 Piper,无需临时文本文件。
- **真正的 Lua**:脚本*返回*策略表(修复了骨架版本的 bug),并支持
  **按站点/频道策略**(按 URL 模式配置音色、语言、画质档位)。sol2 已
  内置在 `vendor/sol` —— `-DSACI_WITH_LUA=ON` 开箱即用。
- **`--selftest`**:4 个本地测试,无需网络。

### v0.3 新特性

- **带 DSP 的带宽探测器**(`src/bw_probe.cpp`):saci 现在在 yt-dlp 和
  ffmpeg 之间泵送视频并测量真实吞吐量。稳健估计器(第 25 百分位 ×
  余量)、**Haar 小波骤降检测器**,以及用于测量吞吐量周期性的
  **radix-2 FFT(N=64)** —— 农村链路存在拥塞周期,最糟的时刻总会回来。
- **中国开源 LLM**:`QwenEngine` —— Qwen2.5(阿里巴巴,Apache 2.0,免费,
  通过 Ollama 本地运行)作为翻译后端。在 `translate_backend = "qwen"`
  中切换,无需改代码。
- **mux 重生(fifo 模式)**:网络过早掉线 → 带退避地重生,直到
  `mux_retries`。带宽估计持久化到 `bw_estimate.txt`,用于指导下一次会话。
- **Selftest 7/7**:新增 FFT 测试(正弦 → 周期性 ≈ 1;常量 → 0)、
  稳健估计器和 Haar 测试。

### 语言路线图

当前目标语言是 **pt-BR**。在后续版本中,同一流水线将支持**西班牙语
(`es`)**和**中文 (`zh`)** —— 配置中的 `source_lang`/`target_lang`
字段正是为此准备的。

### 环境要求

- C++20 编译器(GCC 12+ / Clang 15+)
- `yt-dlp`、`ffmpeg`(+ `ffprobe`)、`piper`(TTS)、`argos-translate`(可选,
  或用外部 LLM 翻译)
- `mpv`(播放器)—— 或重定向 mux 的 stdout
- 可选:Lua 5.4 头文件(sol2 已内置在 `vendor/sol`)

### 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # 不带 Lua
cmake -B build-lua -DCMAKE_BUILD_TYPE=Release -DSACI_WITH_LUA=ON
cmake --build build -j
./build/saci --selftest
```

### 使用

```bash
./build/saci "https://youtube.com/watch?v=XXXX" \
    --config lua/default_config.lua \
    --max-height 360 \
    --workdir /tmp/saci
```

### 设计要点

- **移动语义贯穿始终**(`MovableBuffer` 概念):复制即编译错误。
- **C++20 协程**(`Task<T>`)无线程编排 I/O。
- **引擎可插拔**(concepts:`TranslationEngine`、`TTSEngine`)。
- **同步**:`atempo ∈ [0.85, 1.30]`(保证自然度);漂移 > 300 ms 触发重规划。
- **零磁盘**:视频流经管道;只有旁白(~15 MB/小时)写入磁盘。

完整的本体论(类、关系、状态机、不变量)见
[docs/ONTOLOGY.md](docs/ONTOLOGY.md);含技术路线图的设计文档见
[EXPLICACAO.md](EXPLICACAO.md)。

### 许可证

代码采用 **MIT 许可证** —— 版权所有 Luiz Paulo Colombiano。见
[LICENSE](LICENSE) 与 [DISCLAIMER.md](DISCLAIMER.md)。
