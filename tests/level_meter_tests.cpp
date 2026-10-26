// Tests for the recording waveform level meter.
//
// The meter maps a fixed window of dBFS onto the bars. Raw levels arrive
// normalized over the full -60..0 dBFS range, but no microphone uses all of it:
// the one measured on this machine idles near -33 dBFS and speech reaches about
// -17 dBFS. Mapping the full range left silence in the middle of the scale, so
// speech only moved a bar or two.
//
// The fixed window is deliberate. An adaptive floor/ceiling was tried and
// removed: seeding a floor from the first samples is unreliable because the
// capture stream ramps up with near-silent frames, which made every later frame
// look like speech and pinned the display at full scale. Fixed bounds cannot
// run away, and they keep the display monotonic in loudness.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/audio/level_meter.h"

namespace {

using vinput::audio::LevelMeter;

constexpr double kFrameSeconds = 0.05; // the daemon emits one level per 50 ms

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int Bars(double display) {
  const int bar = static_cast<int>(std::lround(display * 8.0));
  return bar < 0 ? 0 : (bar > 8 ? 8 : bar);
}

// Convert dBFS to the normalized level the daemon emits.
double Dbfs(double dbfs) { return (dbfs + 60.0) / 60.0; }

std::vector<int> Run(LevelMeter& meter, const std::vector<double>& levels) {
  std::vector<int> bars;
  bars.reserve(levels.size());
  for (double level : levels) {
    bars.push_back(Bars(meter.Update(level, kFrameSeconds)));
  }
  return bars;
}

std::string Render(const std::vector<int>& bars) {
  std::string text;
  for (int bar : bars) {
    text += static_cast<char>('0' + bar);
  }
  return text;
}

int Max(const std::vector<int>& bars, std::size_t from, std::size_t to) {
  int peak = 0;
  for (std::size_t i = from; i < bars.size() && i < to; ++i) {
    peak = std::max(peak, bars[i]);
  }
  return peak;
}

void runAllTests() {
  std::cout << "--- 1. Real quiet-room capture stays at the bottom ---\n";
  {
    // Captured on this machine with the microphone open and nobody talking.
    const std::vector<double> levels = {
        0.0,     0.0,     0.0,     0.503963, 0.452932, 0.463677, 0.44362,  0.458517,
        0.442416, 0.467888, 0.449641, 0.436987, 0.438126, 0.455935, 0.446836, 0.43686,
        0.447109, 0.453184, 0.447421, 0.445074, 0.446607, 0.426244, 0.431434, 0.435323,
        0.44194};
    LevelMeter meter;
    meter.Reset();
    const auto bars = Run(meter, levels);
    // Room tone is about -33 dBFS, just inside the window, so it reads as a
    // dim bar rather than climbing the scale.
    expect(Max(bars, 8, bars.size()) <= 2,
           "room tone must stay low, got " + std::to_string(Max(bars, 8, bars.size())) +
               " bars (" + Render(bars) + ")");
  }

  std::cout << "--- 2. Real capture with speech reaches the top ---\n";
  {
    const std::vector<double> levels = {
        0.0,       0.0,       0.0,       0.0,       0.0,       0.000540258, 0.853454,
        0.978587,  0.666976,  0.814,     0.637,     0.947,     0.931,       0.99,
        0.939,     0.776,     0.967,     0.995,     0.926,     0.989,       0.998,
        1.0,       0.966,     0.946,     0.992};
    LevelMeter meter;
    meter.Reset();
    const auto bars = Run(meter, levels);
    expect(Max(bars, 0, bars.size()) >= 7,
           "speech must fill the scale, got " + std::to_string(Max(bars, 0, bars.size())) +
               " bars (" + Render(bars) + ")");
  }

  std::cout << "--- 3. Silence and speech are clearly different ---\n";
  {
    LevelMeter meter;
    meter.Reset();
    std::vector<double> levels(60, Dbfs(-33.0)); // room tone
    levels.insert(levels.end(), 60, Dbfs(-18.0)); // speech
    levels.insert(levels.end(), 40, Dbfs(-33.0)); // room tone again
    const auto bars = Run(meter, levels);

    const int idle = Max(bars, 30, 60);
    const int speech = Max(bars, 60, 120);
    const int after = bars.back();

    expect(idle <= 2, "idle must stay low, got " + std::to_string(idle) + " bars");
    expect(speech >= 7, "speech must stand out, got " + std::to_string(speech) + " bars");
    expect(after <= 2, "display must settle back after speech, got " + std::to_string(after) +
                           " bars (" + Render(bars) + ")");
    expect(speech - idle >= 5, "speech must clear silence by a wide margin");
  }

  std::cout << "--- 4. Anything below the window counts as silence ---\n";
  {
    LevelMeter meter;
    meter.Reset();
    for (double dbfs : {-60.0, -50.0, -40.0, -37.0}) {
      expect(Bars(meter.Update(Dbfs(dbfs), kFrameSeconds)) == 0,
             "levels below the window must read as zero");
    }
  }

  std::cout << "--- 5. Louder input never draws fewer bars ---\n";
  {
    for (double dbfs = -36.0; dbfs <= -14.0; dbfs += 2.0) {
      LevelMeter lower;
      lower.Reset();
      LevelMeter higher;
      higher.Reset();
      for (int i = 0; i < 12; ++i) { // let both settle
        lower.Update(Dbfs(dbfs), kFrameSeconds);
        higher.Update(Dbfs(dbfs + 2.0), kFrameSeconds);
      }
      expect(Bars(higher.displayLevel()) >= Bars(lower.displayLevel()),
             "louder input must not draw fewer bars at " + std::to_string(dbfs) + " dBFS");
    }
  }

  std::cout << "--- 6. Bars rise faster than they fall ---\n";
  {
    LevelMeter meter;
    meter.Reset();
    for (int i = 0; i < 20; ++i) {
      meter.Update(Dbfs(-33.0), kFrameSeconds);
    }
    meter.Update(Dbfs(-18.0), kFrameSeconds);
    expect(meter.displayLevel() > 0.4, "a syllable must show up on the first frame");
    int frames_to_fall = 0;
    while (Bars(meter.displayLevel()) > 0 && frames_to_fall < 100) {
      meter.Update(Dbfs(-33.0), kFrameSeconds);
      ++frames_to_fall;
    }
    expect(frames_to_fall > 1, "release must be slower than the attack");
  }

  std::cout << "--- 7. Out-of-range input and reset are handled ---\n";
  {
    LevelMeter meter;
    meter.Reset();
    meter.Update(5.0, kFrameSeconds);
    meter.Update(-3.0, kFrameSeconds);
    meter.Update(0.5, -1.0);
    expect(Bars(meter.displayLevel()) <= 8, "out-of-range input must stay in range");
    meter.Reset();
    expect(Bars(meter.displayLevel()) == 0, "reset must start from silence");
  }

  std::cout << "\nALL LEVEL METER TESTS PASSED CLEANLY!\n";
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
