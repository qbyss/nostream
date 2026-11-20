#pragma once

#include <algorithm>

namespace evo {

struct NightShiftSchedule {
    bool enabled = false;
    int startMinute = 22 * 60;
    int stopMinute = 7 * 60;

    bool valid() const {
        return startMinute >= 0 && startMinute < 1440 && stopMinute >= 0 &&
            stopMinute < 1440 && startMinute != stopMinute;
    }

    float warmth(int localSecond) const {
        if (!enabled || !valid() || localSecond < 0 || localSecond >= 86400)
            return 0.0f;
        const int elapsed = (localSecond - startMinute * 60 + 86400) % 86400;
        const int duration = ((stopMinute - startMinute + 1440) % 1440) * 60;
        if (elapsed >= duration)
            return 0.0f;
        const int fade = std::min(1800, duration / 2);
        return std::min(1.0f, static_cast<float>(std::min(elapsed, duration - elapsed)) / fade);
    }
};

} // namespace evo
