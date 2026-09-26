#pragma once
// Ontologia: SESSAO DE MUX (ffmpeg como unico intercalador A/V).
// Tres processos ligados por pipes:
//   yt-dlp (video-only, stdout) -> ffmpeg (mux + loudnorm, stdout) -> player
// Sincronizacao PTS/DTS por construcao: um unico muxer.
#include <string>
#include <vector>

namespace saci {

struct MuxSession {
    std::vector<std::string> source_argv;  // ex.: yt-dlp -f ... -o - URL
    std::string narration_path;            // narracao.mp3 (lado A do mux)
    std::vector<std::string> player_argv;  // ex.: mpv --cache=yes -
};

// Estrutura de processos vivos. ~MuxHandle faz waitpid de todos.
class MuxHandle {
public:
    MuxHandle() = default;
    MuxHandle(MuxHandle&&) noexcept = default;
    MuxHandle& operator=(MuxHandle&&) noexcept = default;
    MuxHandle(const MuxHandle&) = delete;
    MuxHandle& operator=(const MuxHandle&) = delete;
    ~MuxHandle();

    int wait_all();  // retorna exit code do ffmpeg (ou do primeiro a falhar)
private:
    std::vector<int> pids_;
    std::vector<int> fds_;
    friend MuxHandle spawn_mux(const MuxSession&);
};

MuxHandle spawn_mux(const MuxSession& sess);

} // namespace saci
