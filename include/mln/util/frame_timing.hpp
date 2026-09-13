#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <vector>

namespace mln {
namespace util {

/// A small, thread-safe rolling-window recorder for per-frame timing samples
/// in milliseconds. Built for task "make it measurable": every frame's real
/// CPU prep time and real GPU execution time (the latter read off a Metal
/// command buffer's own `GPUStartTime`/`GPUEndTime`, not guessed from a
/// delegate-callback arrival rate) are pushed in here as they happen, and a
/// caller pulls a `report()` whenever it wants a distribution rather than a
/// single sample.
///
/// Two independent instances live on `Map` - see `Map::recordFrameCPUMs`/
/// `recordFrameGPUMs`/`getFrameTimingReport`. The CPU one is fed from
/// whichever thread drives `RendererFrontend::render()` (synchronous, so
/// `record()` runs right after the call it timed). The GPU one is fed from
/// Metal's own completion-handler queue, asynchronously and on a different
/// thread than the CPU side entirely - hence the mutex, not because either
/// side is contended on its own.
///
/// The window is COUNT-based (bounded by `capacity`), not time-based: this
/// class does not know or care what "the run" is. A caller that wants a
/// distribution over a specific sustained interval (bench.py's `--sustain`
/// mode) calls `reset()` right before starting the motion and `report()`
/// right after stopping it, so the report describes exactly that interval
/// rather than a trailing window contaminated by whatever came before.
class FrameTimingRecorder {
public:
    struct Report {
        std::size_t count = 0;
        double medianMs = 0.0;
        double p95Ms = 0.0;
        double meanMs = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
    };

    /// 8192 samples is ~136s at a steady 60fps, comfortably past any
    /// sustained-motion run this harness is expected to hold, while
    /// costing at most 64KB of doubles per recorder.
    explicit FrameTimingRecorder(std::size_t capacity_ = 8192) : capacity(capacity_) {}

    void record(double milliseconds) {
        std::lock_guard<std::mutex> lock(mutex);
        samples.push_back(milliseconds);
        if (samples.size() > capacity) {
            // Drop the whole overflow at once rather than one sample at a
            // time, so a long run does not pay an O(n) erase every frame
            // once it fills up.
            samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - capacity));
        }
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        samples.clear();
    }

    Report report() const {
        std::vector<double> copy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            copy = samples;
        }
        Report result;
        result.count = copy.size();
        if (copy.empty()) {
            return result;
        }
        std::sort(copy.begin(), copy.end());
        result.minMs = copy.front();
        result.maxMs = copy.back();
        double sum = 0.0;
        for (double value : copy) {
            sum += value;
        }
        result.meanMs = sum / static_cast<double>(copy.size());
        result.medianMs = percentile(copy, 0.5);
        result.p95Ms = percentile(copy, 0.95);
        return result;
    }

private:
    static double percentile(const std::vector<double>& sortedValues, double fraction) {
        if (sortedValues.empty()) {
            return 0.0;
        }
        const auto rawIndex = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sortedValues.size())));
        const auto index = std::min(sortedValues.size() - 1, rawIndex == 0 ? 0 : rawIndex - 1);
        return sortedValues[index];
    }

    std::size_t capacity;
    mutable std::mutex mutex;
    std::vector<double> samples;
};

} // namespace util
} // namespace mln
