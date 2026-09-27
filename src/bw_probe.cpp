#include "saci/bw_probe.hpp"

#include <algorithm>
#include <cmath>

namespace saci {

void fft64(std::array<double, 64>& re, std::array<double, 64>& im) {
    constexpr std::size_t N = 64;
    constexpr double pi = 3.14159265358979323846;

    // Permutacao bit-reversal
    for (std::size_t i = 1, j = 0; i < N; ++i) {
        std::size_t bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    // Borboletas, camada por camada
    for (std::size_t len = 2; len <= N; len <<= 1) {
        const double ang = -2.0 * pi / static_cast<double>(len);
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (std::size_t i = 0; i < N; i += len) {
            double cwr = 1.0, cwi = 0.0;  // twiddle corrente (recorrencia)
            for (std::size_t j = 0; j < len / 2; ++j) {
                const double ur = re[i + j], ui = im[i + j];
                const double vr = re[i + j + len / 2] * cwr - im[i + j + len / 2] * cwi;
                const double vi = re[i + j + len / 2] * cwi + im[i + j + len / 2] * cwr;
                re[i + j] = ur + vr;
                im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr;
                im[i + j + len / 2] = ui - vi;
                const double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

void BandwidthProbe::sample_window(std::uint64_t bytes, double window_s) {
    if (window_s <= 0.0) return;
    const double kbps = static_cast<double>(bytes) * 8.0 / 1000.0 / window_s;
    kbps_[head_] = kbps;
    head_ = (head_ + 1) % kWindow;
    count_ = std::min(count_ + 1, kWindow);

    // --- Haar (1 nivel): media das ultimas 8 vs 8 anteriores -------------
    if (count_ >= 16) {
        auto mean_of = [&](std::size_t offset_from_end) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 8; ++k) {
                std::size_t idx = (head_ + kWindow + kWindow - offset_from_end - 8 + k) % kWindow;
                // posicoes mais antigas primeiro nao importa: media comutativa
                sum += kbps_[idx];
            }
            return sum / 8.0;
        };
        const double prev = mean_of(8);   // janelas [t-16, t-8)
        const double last = mean_of(0);   // janelas [t-8, t)
        constexpr double kMinSignalKbps = 100.0;  // abaixo: ruido de idle
        constexpr double kDropRatio = 0.6;        // queda > 40%
        if (!drop_ && prev >= kMinSignalKbps && last < prev * kDropRatio) {
            drop_ = true;
            good_since_drop_ = 0;
        } else if (drop_) {
            ++good_since_drop_;
            if (good_since_drop_ >= 4) drop_ = false;  // histerese
        }
    }
}

double BandwidthProbe::periodicity() const {
    if (count_ < kWindow) return 0.0;

    // Ultimas 64 amostras em ordem cronologica
    std::array<double, 64> re{}, im{};
    for (std::size_t k = 0; k < kWindow; ++k)
        re[k] = kbps_[(head_ + k) % kWindow];

    // Remove DC (media) — ciclo de congestionamento nao e componente nula
    double mean = 0.0;
    for (double v : re) mean += v;
    mean /= static_cast<double>(kWindow);
    for (double& v : re) v -= mean;

    // Janela de Hann: reduz vazamento espectral (amostra finita)
    for (std::size_t k = 0; k < kWindow; ++k) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 *
                                              static_cast<double>(k) /
                                              static_cast<double>(kWindow - 1)));
        re[k] *= w;
    }

    fft64(re, im);

    // Energia nos bins 1..31 (real: simetria; DC ja removido)
    double total = 0.0, peak = 0.0;
    for (std::size_t k = 1; k < kWindow / 2; ++k) {
        const double e = re[k] * re[k] + im[k] * im[k];
        total += e;
        peak = std::max(peak, e);
    }
    if (total <= 1e-9) return 0.0;
    // Energia de uma senoide pura concentra-se em um bin (2 com simetria)
    return std::min(1.0, peak * 2.0 / total);
}

double BandwidthProbe::estimate_kbps() const {
    if (count_ == 0) return 0.0;

    // Percentil 25 sobre a janela (copia barata: 64 doubles)
    std::array<double, kWindow> sorted{};
    const std::size_t n = count_;
    for (std::size_t k = 0; k < n; ++k)
        sorted[k] = kbps_[(head_ + kWindow + kWindow - n + k) % kWindow];
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n));
    const double p25 = sorted[n / 4];

    double est = p25 * margin_;
    if (drop_) {
        // em queda brusca, nao aposte: limite ao minimo recente
        double m = sorted[0];
        est = std::min(est, m);
    }
    return est;
}

} // namespace saci
