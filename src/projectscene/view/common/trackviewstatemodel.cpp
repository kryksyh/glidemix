/*
* Audacity: A Digital Audio Editor
*/
#include "trackviewstatemodel.h"

using namespace au::projectscene;
using namespace au::project;

TrackViewStateModel::TrackViewStateModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

IProjectViewStatePtr TrackViewStateModel::viewState() const
{
    IAudacityProjectPtr prj = globalContext()->currentProject();
    return prj ? prj->viewState() : nullptr;
}

void TrackViewStateModel::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        init();
    }, muse::async::Asyncable::Mode::SetReplace);

    IProjectViewStatePtr vs = viewState();
    if (!vs) {
        return;
    }

    if (m_trackId != -1) {
        m_trackHeight = vs->trackHeight(m_trackId);
        m_trackHeight.ch.onReceive(this, [this](int h) {
            if (m_trackHeight.val == h) {
                return;
            }

            auto prj = globalContext()->currentTrackeditProject();
            if (prj && !prj->track(m_trackId)) {
                return;
            }

            m_trackHeight.val = h;
            emit trackHeightChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_isTrackCollapsed = vs->isTrackCollapsed(m_trackId);
        m_isTrackCollapsed.ch.onReceive(this, [this](bool v) {
            if (m_isTrackCollapsed.val == v) {
                return;
            }

            m_isTrackCollapsed.val = v;
            emit isTrackCollapsedChanged();
            emit sampleLineChanged();   // a sample line's own hide button
        }, muse::async::Asyncable::Mode::SetReplace);

        vs->automationLanesChanged().onNotify(this, [this]() { emit automationLanesChanged(); },
                                              muse::async::Asyncable::Mode::SetReplace);
        // GlideMix Samples: the Samples button lights while the line below is
        // shown; a fold or unfold anywhere changes the total height.
        vs->totalTrackHeight().ch.onReceive(this, [this](int) { emit sampleLineChanged(); },
                                            muse::async::Asyncable::Mode::SetReplace);
        if (const auto prj = globalContext()->currentTrackeditProject()) {
            prj->tracksChanged().onReceive(this, [this](const std::vector<trackedit::Track>&) { emit sampleLineChanged(); },
                                           muse::async::Asyncable::Mode::SetReplace);
            prj->trackAdded().onReceive(this, [this](const trackedit::Track&) { emit sampleLineChanged(); },
                                        muse::async::Asyncable::Mode::SetReplace);
            prj->trackRemoved().onReceive(this, [this](const trackedit::Track&) { emit sampleLineChanged(); },
                                          muse::async::Asyncable::Mode::SetReplace);
        }
        m_isSingleWave = vs->isSingleWave(m_trackId);
        m_isSingleWave.ch.onReceive(this, [this](bool v) {
            m_isSingleWave.val = v;
            emit isSingleWaveChanged();
        });
        m_channelHeightRatio = vs->channelHeightRatio(m_trackId);
        m_channelHeightRatio.ch.onReceive(this, [this](double ratio) {
            if (m_channelHeightRatio.val == ratio) {
                return;
            }
            m_channelHeightRatio.val = ratio;
            emit channelHeightRatioChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_displayBounds = vs->verticalDisplayBounds(m_trackId);
        m_displayBounds.ch.onReceive(this, [this](std::pair<float, float> bounds) {
            if (m_displayBounds.val == bounds) {
                return;
            }
            m_displayBounds.val = bounds;
            emit displayBoundsChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        m_rulerType = vs->trackRulerType(m_trackId);
        m_rulerType.ch.onReceive(this, [this](int type) {
            if (m_rulerType.val == type) {
                return;
            }
            m_rulerType.val = type;
            emit isLinearChanged();
        }, muse::async::Asyncable::Mode::SetReplace);

        emit trackHeightChanged();
        emit isTrackCollapsedChanged();
        emit channelHeightRatioChanged();
        emit displayBoundsChanged();
    }

    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        emit isPlayingChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    recordController()->isRecordingChanged().onNotify(this, [this]() {
        emit isRecordingChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    m_meterModel = new playback::PlaybackMeterModel(this);
    emit meterModelChanged();
}

void TrackViewStateModel::changeTrackHeight(int deltaY)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->changeTrackHeight(m_trackId, deltaY);
    }
}

void TrackViewStateModel::changeChannelHeightRatio(double ratio)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->setChannelHeightRatio(m_trackId, ratio);
    }
}

QVariant TrackViewStateModel::trackId() const
{
    return QVariant::fromValue(m_trackId);
}

void TrackViewStateModel::setTrackId(const QVariant& _newTrackId)
{
    trackedit::TrackId newTrackId = _newTrackId.toInt();
    if (m_trackId == newTrackId) {
        return;
    }
    m_trackId = newTrackId;
    emit trackIdChanged();

    init();
}

int TrackViewStateModel::trackHeight() const
{
    return m_trackHeight.val;
}

bool TrackViewStateModel::isTrackCollapsed() const
{
    return m_isTrackCollapsed.val;
}

double TrackViewStateModel::channelHeightRatio() const
{
    return m_channelHeightRatio.val;
}

QVariant TrackViewStateModel::displayBounds() const
{
    const auto& [min, max] = m_displayBounds.val;
    return QVariant::fromValue(QMap<QString, QVariant> {
        { "min", static_cast<double>(min) },
        { "max", static_cast<double>(max) }
    });
}

bool TrackViewStateModel::isLinear() const
{
    const auto prjViewState = viewState();
    if (!prjViewState) {
        return true;
    }

    const auto rulerType = prjViewState->trackRulerType(m_trackId).val;
    return rulerType != static_cast<int>(au::trackedit::TrackRulerType::DbLog);
}

bool TrackViewStateModel::isPlaying() const
{
    return playbackController()->isPlaying();
}

bool TrackViewStateModel::isRecording() const
{
    return recordController()->isRecording();
}

au::playback::PlaybackMeterModel* TrackViewStateModel::meterModel() const
{
    return m_meterModel;
}

bool TrackViewStateModel::isSampleLine() const
{
    const IProjectViewStatePtr vs = viewState();
    return vs && m_trackId != -1 && vs->isSampleLine(m_trackId);
}

namespace {
// The track right under `trackId`, or -1.
au::trackedit::TrackId trackBelow(const au::trackedit::ITrackeditProjectPtr& prj, au::trackedit::TrackId trackId)
{
    if (!prj) {
        return -1;
    }
    const auto ids = prj->trackIdList();
    for (size_t i = 0; i + 1 < ids.size(); ++i) {
        if (ids[i] == trackId) {
            return ids[i + 1];
        }
    }
    return -1;
}
}

bool TrackViewStateModel::hasSampleLine() const
{
    const IProjectViewStatePtr vs = viewState();
    if (!vs || m_trackId == -1 || isSampleLine()) {
        return false;
    }
    const auto below = trackBelow(globalContext()->currentTrackeditProject(), m_trackId);
    return below != -1 && vs->isSampleLine(below);
}

namespace {
// GlideMix Samples: a sample line shows at its own height (not the song's,
// which new lanes copy since 1.2.7). Hidden it takes no room at all (1 px,
// Chris: "really hidden, not shrunk") -- its samples keep playing.
constexpr int kSampleLineHeight = 110;
constexpr int kSampleLineHidden = 1;
}

bool TrackViewStateModel::sampleLineShown() const
{
    const IProjectViewStatePtr vs = viewState();
    if (!vs || m_trackId == -1) {
        return false;
    }
    if (isSampleLine()) {
        return vs->trackHeight(m_trackId).val > kSampleLineHidden;
    }
    const auto below = trackBelow(globalContext()->currentTrackeditProject(), m_trackId);
    return below != -1 && vs->isSampleLine(below) && vs->trackHeight(below).val > kSampleLineHidden;
}

void TrackViewStateModel::toggleSampleLine()
{
    const IProjectViewStatePtr vs = viewState();
    const auto prj = globalContext()->currentTrackeditProject();
    if (!vs || !prj || !trackeditInteraction() || m_trackId == -1) {
        return;
    }
    // On the sample line itself: its hide button folds and unfolds it.
    if (isSampleLine()) {
        vs->setTrackHeight(m_trackId, vs->trackHeight(m_trackId).val > kSampleLineHidden ? kSampleLineHidden : kSampleLineHeight);
        emit sampleLineChanged();
        return;
    }
    const auto below = trackBelow(prj, m_trackId);
    if (below != -1 && vs->isSampleLine(below)) {
        // The song's SAMPLES button: shows or hides (folds) its line. Never
        // deletes it -- the line's own menu does that, like any lane.
        vs->setTrackHeight(below, vs->trackHeight(below).val > kSampleLineHidden ? kSampleLineHidden : kSampleLineHeight);
        emit sampleLineChanged();
        return;
    }
    // A new stereo track, moved right under the song, named and marked.
    const auto before = prj->trackIdList();
    if (!trackeditInteraction()->newStereoTrack()) {
        return;
    }
    trackedit::TrackId added = -1;
    for (const auto id : prj->trackIdList()) {
        if (std::find(before.begin(), before.end(), id) == before.end()) {
            added = id;
        }
    }
    if (added == -1) {
        return;
    }
    vs->setSampleLine(added, true);
    trackeditInteraction()->changeTrackTitle(added, muse::String(trackedit::SAMPLE_LINE_TITLE));
    const auto ids = prj->trackIdList();
    const auto at = std::find(ids.begin(), ids.end(), m_trackId);
    if (at != ids.end()) {
        trackeditInteraction()->moveTracksTo({ added }, static_cast<int>(std::distance(ids.begin(), at)) + 1);
    }
    vs->setTrackHeight(added, kSampleLineHeight);
    emit sampleLineChanged();
}

QVariantList TrackViewStateModel::automationLanes() const
{
    QVariantList out;
    const auto vs = viewState();
    if (!vs || m_trackId == -1) {
        return out;
    }
    for (const auto& lane : vs->automationLanes(m_trackId)) {
        out.push_back(QVariantMap {
            { "instanceId", QString::fromStdString(lane.instanceId) },
            { "pluginId", QString::fromStdString(lane.pluginId) },
            { "parameterId", QString::fromStdString(lane.parameterId) },
            { "label", QString::fromStdString(lane.label) },
            { "color", QString::fromStdString(lane.color) },
            { "defaultValue", lane.defaultValue },
            { "stepped", lane.stepped },
        });
    }
    return out;
}

int TrackViewStateModel::automationLanesHeight() const
{
    const auto vs = viewState();
    return vs && m_trackId != -1
           ? static_cast<int>(vs->automationLanes(m_trackId).size()) * IProjectViewState::kAutomationLaneHeight : 0;
}

void TrackViewStateModel::removeAutomationLane(const QString& instanceId, const QString& parameterId)
{
    const auto vs = viewState();
    if (vs && vs->removeAutomationLane(m_trackId, instanceId.toStdString(), parameterId.toStdString())) {
        vs->changeTrackHeight(m_trackId, -IProjectViewState::kAutomationLaneHeight);
    }
}
