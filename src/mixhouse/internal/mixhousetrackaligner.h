/*
 * MixHouse DJ 2
 */
#pragma once

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/iprojecthistory.h"

#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselexicon.h"
#include "mixhouse/imixhouserekordboxxml.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousetrackaligner.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhousetempomap.h"
#include "mixhouse/imixhousetransitions.h"

namespace au::mixhouse {
class MixHouseTrackAligner : public IMixHouseTrackAligner, public muse::Contextable
{
    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory { this };
    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseLexicon> lexicon { this };
    muse::ContextInject<IMixHouseRekordboxXml> rekordboxXml { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;
    muse::ContextInject<IMixHouseBeatGrids> beatGrids { this };
    muse::ContextInject<IMixHouseTempoMap> tempoMap { this };
    muse::ContextInject<IMixHouseTransitions> transitions { this };

public:
    explicit MixHouseTrackAligner(const muse::modularity::ContextPtr& iocCtx);

    void alignDroppedFiles(const std::vector<muse::io::path_t>& filePaths,
                           const std::vector<trackedit::ClipKeyList>& newClipsPerFile,
                           bool projectWasEmpty, double pxPerSec = 0.0) override;
    bool knowsTempo(const muse::io::path_t& filePath) const override;
    void prepareSampleClips(const trackedit::ClipKeyList& clips, double loopBpm) override;
    void requestViewFocus(double start, double end, trackedit::TrackId track) override;
    muse::async::Channel<double, double, trackedit::TrackId> viewFocusRequested() const override;
    void requestViewFit(double start, double end, trackedit::TrackId track) override;
    muse::async::Channel<double, double, trackedit::TrackId> viewFitRequested() const override;

private:
    MixHouseTrackList findLibraryTrack(const muse::io::path_t& filePath) const;

    muse::async::Channel<double, double, trackedit::TrackId> m_viewFocusRequested;
    muse::async::Channel<double, double, trackedit::TrackId> m_viewFitRequested;
};
}
