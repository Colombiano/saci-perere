#include "saci/sync.hpp"

#include <algorithm>
#include <cmath>

namespace saci {

SyncFitter::Fit SyncFitter::fit(long utter_ms, long window_ms) const {
    Fit f;
    if (utter_ms <= 0 || window_ms <= 0) return f;

    double ratio = static_cast<double>(window_ms) / utter_ms;
    f.atempo = std::clamp(ratio, policy_.tempo_min, policy_.tempo_max);

    long fitted = static_cast<long>(std::lround(utter_ms * f.atempo));
    f.overflow = fitted > window_ms;
    f.pad_ms = std::max(0L, window_ms - fitted);
    return f;
}

bool SyncFitter::DriftCorrector::observe(long error_ms) {
    // erro = (duracao real acumulada) - (linha do tempo plana do SRT).
    acc_ += error_ms;
    if (std::abs(acc_) > threshold_) { acc_ = 0; return true; }
    return false;
}

} // namespace saci
