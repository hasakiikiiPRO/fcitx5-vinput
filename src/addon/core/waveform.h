#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace vinput::addon {

/// Number of bars rendered in the live recording waveform.
inline constexpr std::size_t kWaveformBarCount = 12;

/// Highest bar index; also the number of distinct block glyphs minus one.
inline constexpr std::uint8_t kWaveformMaxBar = 8;

/// Render one bar per level in [0, kWaveformMaxBar] as block glyphs.
///
/// The glyphs are multi-byte in UTF-8, so they must be appended as whole code
/// points. Indexing the table by byte instead would emit a lone continuation
/// byte, producing invalid UTF-8; fcitx::Text::append throws on that and the
/// escaping exception takes down the entire input method.
inline std::string RenderWaveformBars(std::span<const std::uint8_t> bars) {
  static constexpr std::string_view kBlocks[kWaveformMaxBar + 1] = {
      " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
  std::string wave;
  wave.reserve(bars.size() * 3);
  for (std::uint8_t bar : bars) {
    wave += kBlocks[std::min<std::uint8_t>(bar, kWaveformMaxBar)];
  }
  return wave;
}

} // namespace vinput::addon
