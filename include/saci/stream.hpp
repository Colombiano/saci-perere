#pragma once
// Ontologia: FONTE DE VIDEO e DEGRAU DE QUALIDADE.
// Apenas o track de VIDEO e consumido (o audio original e descartado
// pelo design: a narracao TTS o substitui).
//
// v0.4: a escada padrao vive em tabela consteval e o select usa ranges —
// exatamente a evolucao anotada no EXPLICACAO ("std::array constexpr +
// std::ranges quando a ladder for compilada em vez de configurada").
// A tabela Lua em `ladder` (default_config.lua) sobrescreve os degraus.
#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <string>
#include <vector>

namespace saci {

struct QualityRung {
    int height = 0;          // 144, 240, 360, 480...
    int video_kbps = 0;      // bitrate estimado do track sem audio
};

// Degraus padrao avaliados em TEMPO DE COMPILACAO (consteval).
// Valores conservadores de video-only (kbps). A Lua pode sobrescrever.
consteval std::array<QualityRung, 5> default_rungs() {
    return {{{144, 80}, {240, 300}, {360, 700}, {480, 1200}, {720, 2500}}};
}

struct QualityLadder {
    std::vector<QualityRung> rungs;  // ordenado por height crescente

    static QualityLadder with_defaults() {
        constexpr auto d = default_rungs();
        return QualityLadder{{d.begin(), d.end()}};
    }

    // Escolhe o maior degrau que cabe na banda estimada (com folga de 25%
    // para jitter de rede rural). ranges: filtra quem cabe e pega o maior
    // por height via projecao.
    const QualityRung& select(std::int64_t bandwidth_kbps) const {
        auto cabem = rungs | std::views::filter([bandwidth_kbps](const QualityRung& r) {
            return static_cast<std::int64_t>(r.video_kbps) * 125 / 100 <= bandwidth_kbps;
        });
        auto best = std::ranges::max_element(cabem, {}, &QualityRung::height);
        return best != cabem.end() ? *best : rungs.front();
    }
};

// Seletor de formato do yt-dlp: video-only, maior degrau <= max_height.
std::string yt_dlp_format_selector(int max_height);

} // namespace saci
