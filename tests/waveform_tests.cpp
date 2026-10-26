// Regression test for the live recording waveform.
//
// The bar glyphs are multi-byte UTF-8. An earlier implementation indexed the
// glyph table by byte, so every non-zero level emitted a lone continuation
// byte; fcitx::Text::append then threw "Invalid utf8 string" and the escaping
// exception aborted the whole input method on the first audio level signal.

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcitx-utils/utf8.h>
#include <fcitx/text.h>

#include "addon/core/waveform.h"

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Push the rendered waveform through the same path the input panel uses.
void expectAcceptableByInputPanel(const std::string& wave, std::uint8_t bar) {
  expect(fcitx::utf8::validate(wave),
         "waveform for bar " + std::to_string(bar) + " is not valid UTF-8");
  fcitx::Text text;
  try {
    text.append(wave);
  } catch (const std::exception& e) {
    throw std::runtime_error("fcitx::Text rejected waveform for bar " + std::to_string(bar) +
                             ": " + e.what());
  }
}

void runAllTests() {
  std::cout << "--- 1. Every bar level renders valid UTF-8 ---\n";
  for (std::uint8_t bar = 0; bar <= vinput::addon::kWaveformMaxBar; ++bar) {
    const std::vector<std::uint8_t> bars(vinput::addon::kWaveformBarCount, bar);
    expectAcceptableByInputPanel(vinput::addon::RenderWaveformBars(bars), bar);
  }

  std::cout << "--- 2. Mixed levels render valid UTF-8 ---\n";
  const std::vector<std::uint8_t> mixed = {0, 8, 1, 7, 2, 6, 3, 5, 4, 0, 8, 1};
  expectAcceptableByInputPanel(vinput::addon::RenderWaveformBars(mixed), 0);

  std::cout << "--- 3. Bar glyphs advance the waveform monotonically ---\n";
  for (std::uint8_t bar = 0; bar < vinput::addon::kWaveformMaxBar; ++bar) {
    const std::vector<std::uint8_t> low(1, bar);
    const std::vector<std::uint8_t> high(1, static_cast<std::uint8_t>(bar + 1));
    const std::string low_text = vinput::addon::RenderWaveformBars(low);
    const std::string high_text = vinput::addon::RenderWaveformBars(high);
    expect(low_text != high_text, "adjacent bar levels must render differently");
    expect(fcitx::utf8::lengthValidated(high_text) == 1,
           "each bar must render exactly one code point");
  }

  std::cout << "--- 4. Out-of-range levels clamp instead of overrunning ---\n";
  const std::vector<std::uint8_t> over = {255, 254, 9};
  const std::string clamped = vinput::addon::RenderWaveformBars(over);
  expectAcceptableByInputPanel(clamped, 255);
  const std::vector<std::uint8_t> full(vinput::addon::kWaveformBarCount,
                                       vinput::addon::kWaveformMaxBar);
  expect(clamped.substr(0, 6) == vinput::addon::RenderWaveformBars(full).substr(0, 6),
         "over-range levels must clamp to the tallest glyph");

  std::cout << "--- 5. Empty input renders an empty waveform ---\n";
  expect(vinput::addon::RenderWaveformBars({}).empty(), "empty bars must render empty text");

  std::cout << "\nALL WAVEFORM TESTS PASSED CLEANLY!\n";
}

} // namespace

int main() {
  try {
    runAllTests();
  } catch (const std::exception& e) {
    std::cerr << "TEST FAILED: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
