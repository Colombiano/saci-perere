#pragma once
// Ontologia: PROBE DE BANDA (estimativa robusta para rede rural).
//
// Tres ferramentas de processamento de sinais trabalhando sobre a
// janela deslizante de vazao (kbps medidos no pump do mux):
//
//   1. ESTIMADOR ROBUSTO — percentil 25 da janela x margem de seguranca.
//      Percentil (nao media) porque uma rajada nao deve inflar a escolha
//      do degrau; a fala precisa caber ate no pior segundo recente.
//   2. HAAR (wavelet) — detector de quedas abruptas: compara a media das
//      ultimas 8 janelas contra as 8 anteriores. Queda > 40% com sinal
//      minimo dispara `drop_detected()` (com histerese de 4 janelas boas
//      para nao flapping). Haar aqui e o coeficiente de escala de 1 nivel:
//      exatamente o teste media/media, sem adorno.
//   3. FFT radix-2 (N=64) — mede PERIODICIDADE da vazao (0..1). Links
//      rurais tem ciclos de congestionamento (horario comercial, aquecido
//      do radio); se a vazao e ciclica, o estimador pesa o minimo do
//      ciclo, porque o pior momento volta.
#include <array>
#include <cstddef>
#include <cstdint>

namespace saci {

// FFT in-place, radix-2 iterativa, N fixo em 64. re/im entram com as
// amostras (im zerado) e saem com a DFT. Uso interno + selftest.
void fft64(std::array<double, 64>& re, std::array<double, 64>& im);

class BandwidthProbe {
public:
    static constexpr std::size_t kWindow = 64;   // janela espectral
    explicit BandwidthProbe(double safety_margin = 0.75)
        : margin_(safety_margin) {}

    // Alimenta UMA janela de medicao do pump (bytes no intervalo).
    void sample_window(std::uint64_t bytes, double window_s);

    // Estimativa em kbps para decisao de degrau (robusta; respeita drop).
    double estimate_kbps() const;
    // Haar: queda abrupta recente na vazao.
    bool drop_detected() const { return drop_; }
    // FFT: forca do componente ciclico (0..1; ~1 = senoide pura).
    double periodicity() const;
    std::size_t samples() const { return count_; }

private:
    std::array<double, kWindow> kbps_{};
    std::size_t head_ = 0;      // proxima posicao (anel)
    std::size_t count_ = 0;     // amostras ja vistas (<= kWindow)
    double margin_;
    bool drop_ = false;
    int good_since_drop_ = 99;  // histerese
};

} // namespace saci
