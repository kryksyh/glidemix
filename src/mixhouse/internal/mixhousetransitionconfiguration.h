/*
 * GlideMix
 */
#pragma once

#include <QString>

#include "async/notification.h"

#include "mixhouse/imixhousetransitionconfiguration.h"

namespace au::mixhouse {
class MixHouseTransitionConfiguration : public IMixHouseTransitionConfiguration
{
public:
    void init();

    std::vector<MixHouseTransitionSpec> specs() const override;
    std::optional<MixHouseTransitionSpec> spec(const std::string& name) const override;
    std::string saveSpec(const MixHouseTransitionSpec& spec) override;
    bool removeSpec(const std::string& name) override;
    std::string mixNowTransition() const override;
    void setMixNowTransition(const std::string& name) override;
    MixHouseMixNowLanding mixNowLanding() const override;
    void setMixNowLanding(MixHouseMixNowLanding landing) override;
    bool mixerView() const override;
    void setMixerView(bool mixer) override;
    muse::async::Notification changed() const override;

    static QString transitionsFilePath();

private:
    std::vector<MixHouseTransitionSpec> userSpecs() const;
    void storeUserSpecs(const std::vector<MixHouseTransitionSpec>& specs);

    muse::async::Notification m_changed;
};
}
