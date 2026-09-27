#pragma once
// Ontologia: SESSAO DE MUX (ffmpeg como unico intercalador A/V).
// Quatro processos, com o saci como BOMBA (pump) no meio do caminho:
//   yt-dlp/curl (video-only, stdout) -> [BOMBA saci] -> ffmpeg -> player
// O pump mede a vazao real e, na v0.5, faz RESUME FINO: se a FONTE cai,
// so ela renasce — com `curl --range <offset>-` na URL direta — e o mux
// nem percebe (o video "pausa" e continua do byte exato). ffmpeg/player
// nunca reiniciam; sem re-render, sem reabrir o player.
// Sincronizacao PTS/DTS por construcao: um unico muxer.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "saci/bw_probe.hpp"
#include "saci/coro.hpp"
#include "saci/reactor.hpp"

namespace saci {

struct MuxSession {
    std::vector<std::string> source_argv;  // ex.: yt-dlp -f ... -o - URL
    std::string narration_path;            // narracao.mp3 (lado A do mux)
    std::vector<std::string> player_argv;  // ex.: mpv --cache=yes -
    // v0.5: URL direta do video (yt-dlp -g) para resume fino por Range.
    // Vazia => sem resume: comportamento v0.4 (re-spawn completo no fifo).
    std::string direct_url;
    int source_retries = 0;                // retomadas permitidas da fonte
};

// Como (re)criar a FONTE do video: attempt=1..N, offset=proximo byte que
// a fonte deve enviar (0 na primeira). Devolve {pid, fd_de_leitura}.
// O pump reapza o pid ao fim de cada tentativa (waitpid proprio).
using SourceSpawner =
    std::function<std::pair<pid_t, int>(int attempt, std::uint64_t offset)>;

struct PumpOptions {
    SourceSpawner spawn_source;     // obrigatorio
    int max_source_retries = 0;     // 0 = sem resume (fonte unica)
    double stall_seconds = 6.0;     // fonte muda por este tempo => kill+resume
};

// A bomba como coroutine (v0.4/0.5): cada leitura/escrita e um co_await
// de prontidao no Reactor. Na v0.5, loop externo de (re)spawn da FONTE
// com offset exato: bytes escritos no ffmpeg + pendencias no buffer —
// o stream continua sem duplicar nem furar. `done`: 0 = rodando;
// code+1 ao final. Fecha f_write ao terminar; o spawner e o pump fecham
// os fds de leitura de cada fonte. Exposta para o selftest.
Task<int> pump_fds(Reactor& reactor, int f_write, BandwidthProbe& probe,
                   std::atomic<int>& done, PumpOptions opts,
                   std::uint64_t start_offset = 0);

// Estrutura de processos vivos. ~MuxHandle faz waitpid de todos.
class MuxHandle {
public:
    MuxHandle() = default;
    MuxHandle(MuxHandle&& o) noexcept;
    MuxHandle& operator=(MuxHandle&& o) noexcept;
    MuxHandle(const MuxHandle&) = delete;
    MuxHandle& operator=(const MuxHandle&) = delete;
    ~MuxHandle();

    // Roda a bomba como coroutine (reator proprio) e depois waitpid do
    // ffmpeg e do player (as FONTES sao reapadas pelo proprio pump).
    // Retorna exit code do ffmpeg (ou o codigo da bomba em falha).
    int wait_all();

    // true se a bomba/fonte falharam DEPOIS de esgotar as retomadas.
    bool premature_exit() const { return premature_; }

    // Medicao acumulada da bomba (vazao observada no ultimo wait_all).
    const BandwidthProbe& probe() const { return probe_; }
private:
    std::vector<int> pids_;      // ffmpeg + player (fontes: o pump cuida)
    std::vector<int> fds_;
    int ff_pid_ = -1;
    int f_write_ = -1;   // escrita no ffmpeg (a bomba fecha)
    bool premature_ = false;
    BandwidthProbe probe_;
    // stash da sessao para o spawner de fontes (movido em spawn_mux)
    std::vector<std::string> src_argv_;
    std::string direct_url_;
    int source_retries_ = 0;
    friend MuxHandle spawn_mux(const MuxSession&);
};

MuxHandle spawn_mux(const MuxSession& sess);

} // namespace saci
