/*
 * GlideMix
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "mixhouse/mixhousetransitionspec.h"

namespace au::mixhouse {
/// The transitions the user can choose from and the choice Mix Now makes:
/// Mixtape's built-in ones plus any the user builds in Preferences → Transitions.
/// Where Mix Now lands the next track's IN point on the track that is playing.
enum class MixHouseMixNowLanding {
    NextBar,      ///< the next bar line (or the Snap grid): quickest, beat-matched
    NextPhrase,   ///< the next phrase start: beat-matched and phrase-matched, so both tracks change section together
};

class IMixHouseTransitionConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IMixHouseTransitionConfiguration)

public:
    virtual ~IMixHouseTransitionConfiguration() = default;

    /// The built-in transitions first, in a fixed order, then the user's in the order they were saved.
    virtual std::vector<MixHouseTransitionSpec> specs() const = 0;
    virtual std::optional<MixHouseTransitionSpec> spec(const std::string& name) const = 0;

    /// Adds a transition, or replaces the user's own one of the same name. Returns an empty
    /// string on success, else why it was refused (a built-in name, or an invalid spec).
    virtual std::string saveSpec(const MixHouseTransitionSpec& spec) = 0;
    /// Only the user's own transitions can be removed.
    virtual bool removeSpec(const std::string& name) = 0;

    /// What Mix Now applies once it has pulled the next track forward: the name of one of
    /// the specs above, or of one of MixMeister's own presets. Empty = just pull, no fade.
    virtual std::string mixNowTransition() const = 0;
    virtual void setMixNowTransition(const std::string& name) = 0;

    /// Phrase matching (start the incoming track on a phrase boundary of the outgoing one) is what
    /// DJs mean by mixing "on the phrase"; the next bar is the quick alternative.
    virtual MixHouseMixNowLanding mixNowLanding() const = 0;
    virtual void setMixNowLanding(MixHouseMixNowLanding landing) = 0;

    virtual muse::async::Notification changed() const = 0;
};
}
