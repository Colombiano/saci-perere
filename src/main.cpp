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
    bool cfg_loaded = false;
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
        if (a == "--config") {
            cfg = saci::Config::load(need("--config"));
            cfg_loaded = true;
        }
        else if (a == "--max-height") cfg.max_height = std::stoi(need("--max-height"));
        else if (a == "--workdir") workdir = need("--workdir");
        else { std::cerr << "argumento desconhecido: " << a << "\n"; return 64; }
    }

    // v0.7.2: sem --config explicito, o binario sozinho (ex.: instalado no
    // Termux via cmake --install) busca o config padrao do usuario. Antes
    // so' o wrapper do desktop carregava config — no celular o saci rodava
    // com defaults e morria no argos (exit 127).
    if (!cfg_loaded) {
        try {
            if (const char* home = std::getenv("HOME")) {
                std::error_code ec;
                const auto def = std::filesystem::path(home) / ".config" /
                                 "saci" / "config.lua";
                if (std::filesystem::exists(def, ec))
                    cfg = saci::Config::load(def);
            }
        } catch (const std::exception& e) {
            std::cerr << "[saci] config padrao com erro (seguindo com "
                         "defaults): " << e.what() << "\n";
        }
    }

    try {
        saci::Orchestrator orch(std::move(cfg));
        return orch.run(url, workdir);
    } catch (const std::exception& e) {
        std::cerr << "erro fatal: " << e.what() << "\n";
        return 1;
    }
}
