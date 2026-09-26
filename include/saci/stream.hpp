#pragma once
// Ontologia: FONTE DE VIDEO e DEGRAU DE QUALIDADE.
// Apenas o track de VIDEO e consumido (o audio original e descartado
// pelo design: a narracao TTS o substitui).
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace saci {

struct QualityRung {
    int height = 0;          // 144, 240, 360, 480...
    int video_kbps = 0;      // bitrate estimado do track sem audio
};

struct QualityLadder {
    std::vector<QualityRung> rungs;  // ordenado por height crescente

    // Escolhe o maior degrau que cabe na banda estimada (com folga de 25%
    // para jitter de rede rural). Metaprogramacao: tabela constexpr de
    // candidatos poderia virar std::array constexpr + std::ranges.
    const QualityRung& select(std::int64_t bandwidth_kbps) const {
        const QualityRung* best = &rungs.front();
        for (const auto& r : rungs) {
            if (r.video_kbps * 1.25 <= bandwidth_kbps) best = &r;
        }
        return *best;
    }
};

// Seletor de formato do yt-dlp: video-only, maior degrau <= max_height.
std::string yt_dlp_format_selector(int max_height);

} // namespace saci
