#pragma once
// Ontologia: SESSAO DE MUX (ffmpeg como unico intercalador A/V).
// Quatro processos, com o saci como BOMBA (pump) no meio do caminho:
//   yt-dlp (video-only, stdout) -> [PUMP saci] -> ffmpeg (mux+loudnorm)
//        -> player
// O pump mede a vazao real do video e alimenta o BandwidthProbe (v0.3):
// item 3 do roadmap (feedback de banda) comeca AQUI, medindo o sinal real
// em vez de estimar no escuro.
// Sincronizacao PTS/DTS por construcao: um unico muxer.
#include <atomic>
#include <string>
#include <vector>

#include "saci/bw_probe.hpp"
#include "saci/coro.hpp"
#include "saci/reactor.hpp"

namespace saci {

struct MuxSession {
    std::vector<std::string> source_argv;  // ex.: yt-dlp -f ... -o - URL
    std::string narration_path;            // narracao.mp3 (lado A do mux)
    std::vector<std::string> player_argv;  // ex.: mpv --cache=yes -
};

// Bomba (v0.4, roadmap item 5): yt-dlp -> ffmpeg COMO COROUTINE.
// Cada leitura/escrita e um co_await de prontidao no Reactor — nada de
// poll() manual. `done` segue a convencao: 0 = rodando; code+1 ao final.
// Fecha os fds ao terminar. Exposta para o selftest (head|pump|wc).
Task<int> pump_fds(Reactor& reactor, int q_read, int f_write,
                   BandwidthProbe& probe, std::atomic<int>& done);

// Estrutura de processos vivos. ~MuxHandle faz waitpid de todos.
class MuxHandle {
public:
    MuxHandle() = default;
    MuxHandle(MuxHandle&&) noexcept = default;
    MuxHandle& operator=(MuxHandle&&) noexcept = default;
    MuxHandle(const MuxHandle&) = delete;
    MuxHandle& operator=(const MuxHandle&) = delete;
    ~MuxHandle();

    // Roda a bomba como coroutine (reator proprio) e depois waitpid de todos.
    // Retorna exit code do ffmpeg (ou do primeiro a falhar).
    int wait_all();

    // true se yt-dlp/ffmpeg morreram ANTES do fim natural (rede caiu,
    // processo killado). O orchestrator usa isso para o re-spawn (fifo).
    bool premature_exit() const { return premature_; }

    // Medicao acumulada da bomba (vazao observada no ultimo wait_all).
    const BandwidthProbe& probe() const { return probe_; }
private:
    std::vector<int> pids_;      // todos (para ~MuxHandle reapar quem sobrar)
    std::vector<int> fds_;       // pipes que o handle fecha
    int ytdlp_pid_ = -1;
    int ff_pid_ = -1;
    int q_read_ = -1;    // leitura do yt-dlp
    int f_write_ = -1;   // escrita no ffmpeg
    bool premature_ = false;
    BandwidthProbe probe_;
    friend MuxHandle spawn_mux(const MuxSession&);
};

MuxHandle spawn_mux(const MuxSession& sess);

} // namespace saci
