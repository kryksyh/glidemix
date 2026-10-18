/*
 * GlideMix
 */
#pragma once

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/itrackeditinteraction.h"

#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselexicon.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "mixhouse/imixhousetransitions.h"

namespace au::mixhouse {
class MixHouseKeyMatch : public IMixHouseKeyMatch, public muse::Contextable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseLexicon> lexicon { this };
    muse::ContextInject<IMixHouseRekordboxXml> rekordboxXml { this };
    muse::ContextInject<IMixHouseTransitions> transitions { this };

public:
    explicit MixHouseKeyMatch(const muse::modularity::ContextPtr& iocCtx);

    ClipKeyInfo keyInfo(const trackedit::ClipKey& clip) const override;
    Match match(const trackedit::ClipKey& clip) const override;
    bool transpose(const trackedit::ClipKey& clip, int semitones) override;

private:
    int libraryKeyId(const QString& clipTitle) const;
};
}
