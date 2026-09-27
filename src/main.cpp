#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>

#include "saci/config.hpp"
#include "saci/orchestrator.hpp"
#include "saci/proc.hpp"

int main(int argc, char** argv) {
    // Daemon de pipes: escrever em leitor morto devolve EPIPE tratado em
    // codigo (Writable::try_complete) em vez de nos matar (SIGPIPE).
    // Sem isto, a morte do ffmpeg/player derruba o saci inteiro (bug real:
    // video longo, v0.6).
    std::signal(SIGPIPE, SIG_IGN);

    // Autoteste dos pipes bidirecionais (nao precisa de rede nem de TTS).
    if (argc == 2 && std::string(argv[1]) == "--selftest")
        return saci::selftest();

    if (argc < 2) {
        std::cerr << "uso: saci <url> [--config FILE] [--max-height N] "
                     "[--workdir DIR]\n";
        return 64;
    }
    const std::string url = argv[1];

    saci::Config cfg;
    // v0.6.3: workdir default respeita $TMPDIR (no Android/Termux /tmp nao
    // existe; temp padrao e' $PREFIX/tmp). Desktop: TMPDIR vazio -> /tmp.
    const char* tmp = std::getenv("TMPDIR");
    std::filesystem::path workdir =
        std::filesystem::path(tmp && *tmp ? tmp : "/tmp") / "saci";

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* k) -> std::string {
            if (i + 1 >= argc) { std::cerr << k << " precisa de valor\n"; std::exit(64); }
            return argv[++i];
        };
        if (a == "--config") cfg = saci::Config::load(need("--config"));
        else if (a == "--max-height") cfg.max_height = std::stoi(need("--max-height"));
        else if (a == "--workdir") workdir = need("--workdir");
        else { std::cerr << "argumento desconhecido: " << a << "\n"; return 64; }
    }

    try {
        saci::Orchestrator orch(std::move(cfg));
        return orch.run(url, workdir);
    } catch (const std::exception& e) {
        std::cerr << "erro fatal: " << e.what() << "\n";
        return 1;
    }
}
