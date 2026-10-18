// GlideMix: Mixxx's PollingControlProxy, reading the few Mixxx controls
// the built-in effects poll (the EQ crossover frequencies) from Mixtape
// Studio's control table instead of Mixxx's control system.
#pragma once

#include <QString>

#include "control/mixtapecontrols.h"

class PollingControlProxy {
public:
    PollingControlProxy(const QString& group, const QString& item)
        : m_value(mixtapecontrols::control(group.toStdString(), item.toStdString()))
    {
    }

    double get() const { return m_value->load(std::memory_order_relaxed); }
    bool toBool() const { return get() > 0.0; }

private:
    const std::atomic<double>* m_value;
};
