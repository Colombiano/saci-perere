#pragma once
// Ontologia: POLITICA DE SINCRONIA (o coracao do processo).
// - ratio = janela / duracao_medida, limitado a [tempo_min, tempo_max]
//   (limites de NATURALIDADE da fala, nao do atempo tecnico).
// - drift acumulado > threshold_ms => replanejamento (invariante).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace saci {

struct SyncPolicy {
    double tempo_min = 0.85;
    double tempo_max = 1.30;
    long drift_threshold_ms = 300;
};

class SyncFitter {
public:
    explicit SyncFitter(SyncPolicy p) : policy_(p) {}

    // planeja UMA fala contra SUA janela; retorna atempo e flag overflow.
    struct Fit {
        double atempo = 1.0;
        bool overflow = false;   // nem com tempo_max a fala coube
        long pad_ms = 0;         // silencio pos-fala dentro da janela
    };
    Fit fit(long utter_ms, long window_ms) const;

    // Corretor de drift acumulado entre janelas planas (SRT) e fala real.
    class DriftCorrector {
    public:
        explicit DriftCorrector(long threshold_ms)
            : threshold_(threshold_ms) {}
        // alimenta erro observado; retorna true se replanejamento e exigido.
        bool observe(long error_ms);
        long accumulated() const { return acc_; }
        void reset() { acc_ = 0; }
    private:
        long threshold_;
        long acc_ = 0;
    };

    const SyncPolicy& policy() const { return policy_; }

private:
    SyncPolicy policy_;
};

} // namespace saci
