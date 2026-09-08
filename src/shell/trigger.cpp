#include "shell/trigger.h"

#include <algorithm>
#include <cmath>

namespace omni::shell {

void TriggerDetector::Reset() {
    inside_    = false;
    fired_     = false;
    dwellFrom_ = 0;
    travel_    = 0.f;
    last_      = POINT{};
}

bool TriggerDetector::Update(bool insideZone, POINT cursor, ULONGLONG nowMs) {
    if (!insideZone) {
        Reset();
        return false;
    }

    if (!inside_) {
        // Appena entrato: parte l'attesa da qui.
        inside_    = true;
        fired_     = false;
        dwellFrom_ = nowMs;
        travel_    = 0.f;
        last_      = cursor;
        return false;
    }

    const float dx = static_cast<float>(cursor.x - last_.x);
    const float dy = static_cast<float>(cursor.y - last_.y);
    travel_ += std::sqrt(dx * dx + dy * dy);
    last_ = cursor;

    // Si e' mosso troppo: non si sta fermando qui, sta passando. L'attesa
    // riparte da adesso invece di annullarsi, cosi' un cursore che rallenta e
    // si ferma apre comunque la barra senza doverla ricercare.
    if (travel_ > static_cast<float>(cfg_.travelPx)) {
        dwellFrom_ = nowMs;
        travel_    = 0.f;
        return false;
    }

    if (fired_) return false;

    if (nowMs - dwellFrom_ >= static_cast<ULONGLONG>(cfg_.delayMs)) {
        fired_ = true;
        return true;
    }
    return false;
}

float TriggerDetector::Progress(ULONGLONG nowMs) const {
    if (!inside_ || cfg_.delayMs <= 0) return 0.f;
    const auto elapsed = static_cast<float>(nowMs - dwellFrom_);
    return std::clamp(elapsed / static_cast<float>(cfg_.delayMs), 0.f, 1.f);
}

}  // namespace omni::shell
