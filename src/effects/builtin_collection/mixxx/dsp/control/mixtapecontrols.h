// GlideMix: values of Mixxx controls the built-in effects poll.
#pragma once

#include <atomic>
#include <string>

namespace mixtapecontrols {
//! The control's live value; unknown controls read 0. Never null.
std::atomic<double>* control(const std::string& group, const std::string& item);
void set(const std::string& group, const std::string& item, double value);
}
