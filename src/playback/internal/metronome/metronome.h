/*
 * GlideMix
 */
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "../../imetronome.h"

namespace au::playback {
class Metronome : public IMetronome
{
public:
    void init();

    bool enabled() const override;
    void setEnabled(bool enabled) override;

    double level() const override;
    void setLevel(double level) override;

    Voice voice() const override;
    void setVoice(Voice voice) override;

    int pitch() const override;
    double tonality() const override;
    double damp() const override;
    void setTuning(int pitch, double tonality, double damp) override;

    muse::async::Notification settingsChanged() const override;

    void prepare(int sampleRate) override;
    const Clicks* clicks() const override;

private:
    void publish();

    int m_sampleRate = 0;
    std::atomic<const Clicks*> m_current { nullptr };
    //! Every published set stays alive until the metronome goes away, so the
    //! audio thread can keep reading a pointer it already loaded. Sets are a
    //! few tens of KB and only change when a setting does.
    std::vector<std::unique_ptr<const Clicks> > m_published;
    muse::async::Notification m_settingsChanged;
};
}
