#pragma once

#include <algorithm>
#include <cmath>

namespace vinput::audio {

/// Turn raw input levels into display levels for the recording waveform.
///
/// Raw levels arrive normalized over -60..0 dBFS, but no microphone uses that
/// whole range: the one measured on this machine idles at -32 dBFS and peaks
/// around -17 dBFS when speaking. Mapping the full -60..0 range onto the bars
/// therefore leaves silence sitting in the middle of the scale, so speech only
/// nudges a bar or two.
///
/// Instead map a fixed window of kFloorDb..kCeilingDb onto the bars. A fixed
/// window keeps the display monotonic in loudness: a given level always draws
/// the same height, so the bars track the voice rather than the room.
class LevelMeter {
public:
  /// Feed one raw level in [0, 1]; returns a display level in [0, 1].
  /// `dt` is the time since the previous call, in seconds.
  double Update(double level, double dt) {
    level = std::clamp(level, 0.0, 1.0);
    dt = std::max(dt, 0.0);

    // Raw levels are (dBFS + 60) / 60, so undo that to get back to dBFS.
    const double dbfs = level * 60.0 - 60.0;
    const double target = std::clamp((dbfs - kFloorDb) / (kCeilingDb - kFloorDb), 0.0, 1.0);

    // Fast attack so the first syllable visibly jumps, slower release so the
    // bars glide down instead of flickering between words.
    const double rate = target > display_ ? kAttackRate : kReleaseRate;
    display_ += (target - display_) * (1.0 - std::exp(-rate * dt));
    return display_;
  }

  /// Forget the smoothed level, e.g. when a new recording starts.
  void Reset() { display_ = 0.0; }

  /// Current display level without feeding new input.
  [[nodiscard]] double displayLevel() const { return display_; }

private:
  // Measured on this machine: room tone sits near -32 dBFS and speech reaches
  // about -17 dBFS. Starting the scale just below the room tone puts silence at
  // the bottom of the display.
  static constexpr double kFloorDb = -36.0;
  static constexpr double kCeilingDb = -14.0;
  static constexpr double kAttackRate = 30.0;
  static constexpr double kReleaseRate = 6.0;

  double display_ = 0.0;
};

} // namespace vinput::audio
