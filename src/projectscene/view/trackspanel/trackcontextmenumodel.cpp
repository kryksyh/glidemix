/*
* Audacity: A Digital Audio Editor
*/
#include "trackcontextmenumodel.h"
#include "trackedit/dom/track.h"

#include "global/async/async.h"

using namespace au::projectscene;
using namespace muse::uicomponents;
using namespace muse::actions;

namespace {
constexpr const char* TRACK_FORMAT_CHANGE_ACTION = "action://trackedit/track/change-format?format=%1";
constexpr const char* TRACK_RATE_CHANGE_ACTION = "action://trackedit/track/change-rate?rate=%1";

constexpr const char* TRACK_VIEW_WAVEFORM_ACTION = "action://trackedit/track-view-waveform";
constexpr const char* TRACK_VIEW_SPECTROGRAM_ACTION = "action://trackedit/track-view-spectrogram";
constexpr const char* TRACK_VIEW_MULTI_ACTION = "action://trackedit/track-view-multi";
constexpr const char* TRACK_VIEW_HALF_WAVE_ACTION = "action://projectscene/track-view-half-wave";
constexpr const char* TRACK_VIEW_SINGLE_WAVE_ACTION = "action://projectscene/track-view-single-wave";

constexpr const char* TRACK_COLOR_MENU_ID = "trackColorMenu";
constexpr const char* TRACK_COLOR_PICKER_ACTION = "mixhouse-track-color-picker";
constexpr const char* TRACK_RESET_COLORS_ACTION = "mixhouse-track-reset-colors";
constexpr const char* TRACK_COLOR_BY_KEY_ACTION = "mixhouse-track-color-by-key";
constexpr const char* TRACK_CLIPS_COLOR_ACTION = "mixhouse-track-clips-color";
constexpr const char* TRACK_COLOR_INTENSITY_ACTION = "mixhouse-track-color-intensity";
constexpr const char* TRACK_SEPARATE_STEMS_ACTION = "mixhouse-separate-stems";
constexpr const char* TRACK_FORMAT_MENU_ID = "trackFormatMenu";
constexpr const char* TRACK_RATE_MENU_ID = "trackRateMenu";

//! NOTE: can be moved to the framework
bool containsAny(const ActionCodeList& list, const ActionCodeList& actionCodes)
{
    return std::any_of(actionCodes.begin(), actionCodes.end(), [&](ActionCode code) {
        return std::find(list.begin(), list.end(), code) != list.end();
    });
}

muse::actions::ActionQuery makeTrackFormatChangeAction(const au::trackedit::TrackFormat& format)
{
    return muse::actions::ActionQuery(muse::String(TRACK_FORMAT_CHANGE_ACTION).arg(
                                          muse::String::number(static_cast<int>(format))));
}

muse::actions::ActionQuery makeTrackRateChangeAction(uint64_t rate)
{
    return muse::actions::ActionQuery(muse::String(TRACK_RATE_CHANGE_ACTION).arg(muse::String::number(static_cast<int>(rate))));
}
}

namespace {
MenuItem* makeTrackColorPickerItem(QObject* parent)
{
    // This is a local QML interaction, not an action registered with the
    // application action registry.  Constructing it through makeMenuItem()
    // would therefore return nullptr, which a menu view cannot display.
    auto* item = new MenuItem(parent);
    item->setId(QString::fromUtf8(TRACK_COLOR_PICKER_ACTION));
    item->setTitle(muse::TranslatableString("trackcontextmenu", "Track color…"));
    return item;
}

MenuItem* makeResetColorsItem(QObject* parent)
{
    auto* item = new MenuItem(parent);
    item->setId(QString::fromUtf8(TRACK_RESET_COLORS_ACTION));
    item->setTitle(muse::TranslatableString("trackcontextmenu", "Reset colors"));
    return item;
}
}

//! GlideMix: split the track into drums, bass, vocals and other (Demucs).
MenuItem* TrackContextMenuModel::makeSeparateStemsItem()
{
    auto* item = new MenuItem(this);
    item->setId(QString::fromUtf8(TRACK_SEPARATE_STEMS_ACTION));
    item->setTitle(muse::TranslatableString("trackcontextmenu", "Separate stems"));
    item->setEnabled(stems() && stems()->available());
    return item;
}

//! GlideMix: every colour setting of the track, the same submenu as on a
//! clip (Chris, 2026-10-27: both menus carry Color and Track visualization).
//! Clip color here colours the track's clips. Picking a colour turns Color by
//! key off, or the key colour would keep hiding it.
MenuItemList TrackContextMenuModel::makeColorItems()
{
    auto makeItem = [this](const char* id, const muse::TranslatableString& title) {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(id));
        item->setTitle(title);
        item->setEnabled(true);
        return item;
    };
    auto* byKey = new MenuItem(this);
    byKey->setId(QString::fromUtf8(TRACK_COLOR_BY_KEY_ACTION));
    byKey->setTitle(muse::TranslatableString("trackcontextmenu", "Color by key"));
    byKey->setEnabled(true);
    byKey->setCheckable(true);
    byKey->setChecked(clipColors() && clipColors()->colorByKey(m_trackId));

    return {
        byKey,
        makeSeparator(),
        makeItem(TRACK_CLIPS_COLOR_ACTION, muse::TranslatableString("trackcontextmenu", "Clip color…")),
        makeTrackColorPickerItem(this),
        makeItem(TRACK_COLOR_INTENSITY_ACTION, muse::TranslatableString("trackcontextmenu", "Intensity…")),
        makeSeparator(),
        makeResetColorsItem(this),
    };
}

MenuItem* TrackContextMenuModel::makeItemWithArg(const ActionCode& actionCode)
{
    MenuItem* item = makeMenuItem(actionCode);
    item->setArgs(ActionData::make_arg1<trackedit::TrackId>(m_trackId));
    return item;
}

MenuItemList TrackContextMenuModel::makeStereoTrackItems()
{
    MenuItemList items {
        makeItemWithArg("track-rename"),
        makeItemWithArg("track-duplicate"),
        makeItemWithArg("track-delete"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Move track"), makeTrackMoveItems()),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Color"), makeColorItems(), TRACK_COLOR_MENU_ID),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Track visualization"), makeTrackVisualizationItems()),
        makeSeparateStemsItem(),
        makeItemWithArg("toggle-vertical-rulers"),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Meters && monitoring"), makeMeterMonitoringItems()),
        makeSeparator(),
        makeItemWithArg("track-swap-channels"),
        makeItemWithArg("track-split-stereo-to-lr"),
        makeItemWithArg("track-split-stereo-to-center"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Format:"), makeTrackFormatItems(), TRACK_FORMAT_MENU_ID),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Rate:"), makeTrackRateItems(), TRACK_RATE_MENU_ID),
        makeItemWithArg("track-resample"),
    };

    return items;
}

MenuItemList TrackContextMenuModel::makeMonoTrackItems()
{
    MenuItemList items {
        makeItemWithArg("track-rename"),
        makeItemWithArg("track-duplicate"),
        makeItemWithArg("track-delete"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Move track"), makeTrackMoveItems()),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Color"), makeColorItems(), TRACK_COLOR_MENU_ID),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Track visualization"), makeTrackVisualizationItems()),
        makeSeparateStemsItem(),
        makeItemWithArg("toggle-vertical-rulers"),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Meters && monitoring"), makeMeterMonitoringItems()),
        makeSeparator(),
        makeItemWithArg("track-make-stereo"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Format:"), makeTrackFormatItems(), TRACK_FORMAT_MENU_ID),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Rate:"), makeTrackRateItems(), TRACK_RATE_MENU_ID),
        makeItemWithArg("track-resample"),
    };

    return items;
}

MenuItemList TrackContextMenuModel::makeLabelTrackItems()
{
    return {
        makeItemWithArg("track-rename"),
        makeItemWithArg("track-duplicate"),
        makeItemWithArg("track-delete"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("trackcontextmenu", "Move track"), makeTrackMoveItems()),
        makeTrackColorPickerItem(this),
        makeSeparator(),
        makeItemWithArg("export-labels")
    };
}

void TrackContextMenuModel::load()
{
    AbstractMenuModel::load();

    projectHistory()->historyChanged().onReceive(this, [this](auto) {
        updateColorCheckedState();
        updateTrackFormatState();
        updateTrackRateState();
        updateTrackMonoState();
        updateTrackViewCheckedState();
    }, muse::async::Asyncable::Mode::SetReplace);

    selectionController()->tracksSelected().onReceive(this, [this](const trackedit::TrackIdList&) {
        updateTrackMonoState();
    }, muse::async::Asyncable::Mode::SetReplace);

    // Color by key can also change from the clip menu or a colour pick.
    if (clipColors()) {
        clipColors()->trackColorsChanged().onReceive(this, [this](const trackedit::TrackId& track) {
            if (track == m_trackId) {
                findItem(QString::fromUtf8(TRACK_COLOR_BY_KEY_ACTION)).setChecked(clipColors()->colorByKey(m_trackId));
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    trackNavigationController()->openContextMenuRequested().onReceive(this, [this](const trackedit::TrackItemKey& key) {
        if (key.trackId == m_trackId && key.itemId == trackedit::INVALID_TRACK_ITEM) {
            emit openRequested();
        }
    }, muse::async::Asyncable::Mode::SetReplace);

    trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    auto track = prj->track(m_trackId);
    if (!track.has_value()) {
        return;
    }

    prj->trackChanged().onReceive(this, [this](const trackedit::Track& track) {
        if (track.id != m_trackId) {
            return;
        }
        emit trackColorChanged();
        load();
    }, muse::async::Asyncable::Mode::SetReplace);

    switch (track.value().type) {
    case trackedit::TrackType::Mono:
        setItems(makeMonoTrackItems());
        break;
    case trackedit::TrackType::Stereo:
        setItems(makeStereoTrackItems());
        break;
    case trackedit::TrackType::Label:
        setItems(makeLabelTrackItems());
        break;
    default:
        return;
    }

    updateColorCheckedState();
    updateTrackFormatState();
    updateTrackRateState();
    updateTrackMonoState();
    updateTrackViewCheckedState();
    updateRecordingState();

    globalContext()->isRecordingChanged().onNotify(this, [this]() {
        updateRecordingState();
    }, muse::async::Asyncable::Mode::SetReplace);
}

au::trackedit::TrackId TrackContextMenuModel::trackId() const
{
    return m_trackId;
}

void TrackContextMenuModel::setTrackId(const trackedit::TrackId& newTrackId)
{
    if (m_trackId == newTrackId) {
        return;
    }
    m_trackId = newTrackId;
    emit trackIdChanged();
}

void TrackContextMenuModel::resetColors()
{
    resetTrackColors();
}

void TrackContextMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId == "track-rename") {
        emit trackRenameRequested();
    } else if (itemId == TRACK_COLOR_PICKER_ACTION) {
        emit trackColorPickerRequested();
    } else if (itemId == TRACK_CLIPS_COLOR_ACTION) {
        emit clipsColorPickerRequested();
    } else if (itemId == TRACK_COLOR_INTENSITY_ACTION) {
        emit colorIntensityRequested();
    } else if (itemId == TRACK_RESET_COLORS_ACTION) {
        muse::async::Async::call(this, [this] { resetTrackColors(); });
    } else if (itemId == TRACK_SEPARATE_STEMS_ACTION) {
        const trackedit::TrackId track = m_trackId;
        muse::async::Async::call(this, [this, track] {
            if (stems()) {
                stems()->separateTrack(track);
            }
        });
    } else if (itemId == TRACK_COLOR_BY_KEY_ACTION) {
        if (clipColors()) {
            const bool on = !clipColors()->colorByKey(m_trackId);
            clipColors()->setColorByKey(m_trackId, on);
            findItem(itemId).setChecked(on);
        }
    } else if (itemId == spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION) {
        const auto project = globalContext()->currentProject();
        IF_ASSERT_FAILED(project) {
            return;
        }
        const auto track = project->trackeditProject()->track(m_trackId);
        IF_ASSERT_FAILED(track) {
            return;
        }
        const auto trackTitle = track->title;
        auto args = muse::actions::ActionData::make_arg2(m_trackId, trackTitle);
        dispatcher()->dispatch(spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION, std::move(args));
    } else {
        //! Why an async call?
        //!
        //! On the one hand, `pushHistoryState` may be called as a result of a QML item triggering an action that will lead to its deletion
        //! (e.g. deleting a track from a context menu).
        //!
        //! On the other hand, when the QML-triggered action is called after an undo, this undo history item gets discarded.
        //! If this undo item involved the creation of audio blocks, such as when generating audio, `pushHistoryState` will delete these blocks.
        //! If there was many of them, it will open a progress dialog which, to be updated, leads to `QCoreApplication::processEvents()` calls.
        //!
        //! This crashes because QCoreApplication::processEvents() will now process an event that deletes the QML item that is at the origin of this very call...
        //! Qt then throws the message "Object 0x..... destroyed while one of its QML signal handlers is in progress."
        //!
        //! https://github.com/audacity/audacity/issues/9530
        muse::async::Async::call(this, [this, itemId]{ AbstractMenuModel::handleMenuItem(itemId); });
    }
}

void TrackContextMenuModel::resetTrackColors()
{
    const auto project = globalContext()->currentTrackeditProject();
    if (!project || m_trackId == trackedit::INVALID_TRACK) {
        return;
    }
    // Clips with a colour of their own go back to following the track.
    bool clipsChanged = false;
    for (const trackedit::Clip& clip : project->clipList(m_trackId)) {
        if (!clip.isAutoColor && trackeditInteraction()) {
            clipsChanged |= trackeditInteraction()->changeClipColor(clip.key, trackedit::CLIP_COLOR_INDEX_NONE);
        }
    }
    if (clipColors()) {
        clipColors()->setColorByKey(m_trackId, true);   // Colour by key is the default
        clipColors()->setIntensity(m_trackId, mixhouse::IMixHouseClipColors::kFullIntensity);
    }
    if (clipsChanged) {
        projectHistory()->pushHistoryState(muse::trc("trackcontextmenu", "Reset clip colors"),
                                           muse::trc("trackcontextmenu", "Reset colors"));
    }
}

QVariantList TrackContextMenuModel::colorPalette() const
{
    QVariantList palette;
    palette.reserve(96);
    for (int index = 1; index <= 96; ++index) {
        palette.push_back(QVariantMap { { "index", index },
                                        { "color", projectSceneConfiguration()->clipColor(index).toQColor() } });
    }
    return palette;
}

int TrackContextMenuModel::currentColorIndex() const
{
    const auto project = globalContext()->currentTrackeditProject();
    const auto track = project ? project->track(m_trackId) : std::nullopt;
    return track ? track->colorIndex : 1;
}

//! GlideMix: Color > Clip color from the track menu -- every clip on the track.
void TrackContextMenuModel::setClipsColor(int colorIndex)
{
    const auto project = globalContext()->currentTrackeditProject();
    if (!project || !trackeditInteraction() || m_trackId == trackedit::INVALID_TRACK) {
        return;
    }
    if (colorIndex != trackedit::CLIP_COLOR_INDEX_NONE && (colorIndex < 1 || colorIndex > 96)) {
        return;
    }
    bool changed = false;
    for (const trackedit::Clip& clip : project->clipList(m_trackId)) {
        changed |= trackeditInteraction()->changeClipColor(clip.key, colorIndex);
    }
    if (clipColors()) {
        clipColors()->setColorByKey(m_trackId, false);   // show the colour just picked
    }
    if (changed) {
        projectHistory()->pushHistoryState(muse::trc("trackcontextmenu", "Changed clip color"),
                                           muse::trc("trackcontextmenu", "Clip color"));
    }
}

void TrackContextMenuModel::resetClipsColor()
{
    setClipsColor(trackedit::CLIP_COLOR_INDEX_NONE);
}

int TrackContextMenuModel::colorIntensity() const
{
    return clipColors() ? clipColors()->intensity(m_trackId) : mixhouse::IMixHouseClipColors::kFullIntensity;
}

void TrackContextMenuModel::setColorIntensity(int intensity)
{
    if (clipColors() && colorIntensity() != intensity) {
        clipColors()->setIntensity(m_trackId, intensity);
        emit colorIntensityChanged();
    }
}

void TrackContextMenuModel::setTrackColor(int colorIndex)
{
    if (colorIndex < 1 || colorIndex > 96 || m_trackId == trackedit::INVALID_TRACK) {
        return;
    }
    selectionController()->setSelectedTracks({ m_trackId });
    dispatcher()->dispatch(makeTrackColorChangeAction(colorIndex));
    // A colour picked on purpose is shown, not hidden behind the key colour.
    if (clipColors()) {
        clipColors()->setColorByKey(m_trackId, false);
    }
}

void TrackContextMenuModel::onActionsStateChanges(const muse::actions::ActionCodeList& codes)
{
    if (codes.empty()) {
        return;
    }

    for (const ActionCode& code : codes) {
        MenuItem& actionItem = findItem(code);
        if (actionItem.isValid()) {
            actionItem.setState(uiActionsRegister()->actionState(code));
        }
    }

    if (containsAny(codes, m_colorChangeActionCodeList)) {
        updateColorCheckedState();
    }

    if (containsAny(codes, m_trackViewTypeChangeActionCodeList)) {
        updateTrackViewCheckedState();
    }

    if (containsAny(codes, { ActionCode(TRACK_VIEW_HALF_WAVE_ACTION) })) {
        const auto project = globalContext()->currentProject();
        if (!project) {
            return;
        }

        const auto viewState = project->viewState();
        if (!viewState) {
            return;
        }

        const bool isHalfWave = viewState->isHalfWave(m_trackId).val;

        MenuItem& item = findItem(ActionCode(TRACK_VIEW_HALF_WAVE_ACTION));
        auto state = item.state();
        state.checked = isHalfWave;
        item.setState(state);
    }

    if (containsAny(codes, { ActionCode(TRACK_VIEW_SINGLE_WAVE_ACTION) })) {
        const auto project = globalContext()->currentProject();
        const auto viewState = project ? project->viewState() : nullptr;
        if (viewState) {
            MenuItem& item = findItem(ActionCode(TRACK_VIEW_SINGLE_WAVE_ACTION));
            auto state = item.state();
            state.checked = viewState->isSingleWave(m_trackId).val;
            item.setState(state);
        }
    }

    for (const auto& formatInfo : au::trackedit::availableTrackFormats()) {
        if (std::find(codes.begin(), codes.end(), makeTrackFormatChangeAction(formatInfo.format).toString()) != codes.end()) {
            updateTrackFormatState();
        }
    }

    for (const auto& rate : audioDriverController()->sampleRates()) {
        if (std::find(codes.begin(), codes.end(), makeTrackRateChangeAction(rate).toString()) != codes.end()) {
            updateTrackRateState();
        }
    }
}

void TrackContextMenuModel::updateColorCheckedState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }

    auto track = project->trackeditProject()->track(m_trackId);
    if (!track.has_value()) {
        return;
    }

    for (const auto& action : m_colorChangeActionCodeList) {
        MenuItem& item = findItem(ActionCode(action));
        ActionQuery query(action);

        bool checked = query.contains("colorindex")
                       && query.param("colorindex").toInt() == track.value().colorIndex;
        auto state = item.state();
        state.checked = checked;
        item.setState(state);
    }
}

void TrackContextMenuModel::updateTrackViewCheckedState()
{
    const auto prj = globalContext()->currentProject();
    if (!prj) {
        // Probably closing down.
        return;
    }
    const auto viewType = prj->viewState()->trackViewType(m_trackId).val;
    for (const auto& viewTypeCode : m_trackViewTypeChangeActionCodeList) {
        MenuItem& item = findItem(viewTypeCode);
        auto state = item.state();
        // GlideMix: Half-wave and Single wave are on/off of their own, not view types.
        if (viewTypeCode == TRACK_VIEW_HALF_WAVE_ACTION || viewTypeCode == TRACK_VIEW_SINGLE_WAVE_ACTION) {
            state.checked = viewTypeCode == TRACK_VIEW_HALF_WAVE_ACTION ? prj->viewState()->isHalfWave(m_trackId).val
                            : prj->viewState()->isSingleWave(m_trackId).val;
            item.setState(state);
            continue;
        }
        switch (viewType) {
        case trackedit::TrackViewType::Waveform:
            state.checked = (viewTypeCode == TRACK_VIEW_WAVEFORM_ACTION);
            break;
        case trackedit::TrackViewType::Spectrogram:
            state.checked = (viewTypeCode == TRACK_VIEW_SPECTROGRAM_ACTION);
            break;
        case trackedit::TrackViewType::WaveformAndSpectrogram:
            state.checked = (viewTypeCode == TRACK_VIEW_MULTI_ACTION);
            break;
        default:
            assert(viewType == trackedit::TrackViewType::Undefined);
            state.checked = false;
        }
        item.setState(state);
    }
}

void TrackContextMenuModel::updateTrackFormatState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }

    auto track = project->trackeditProject()->track(m_trackId);
    if (!track.has_value()) {
        return;
    }

    for (const auto& formatInfo : au::trackedit::availableTrackFormats()) {
        MenuItem& item = findItem(makeTrackFormatChangeAction(formatInfo.format).toString());
        if (track.value().format != formatInfo.format) {
            item.setChecked(false);
            continue;
        }

        item.setChecked(true);
        MenuItem& menu = findMenu(QString::fromUtf8(TRACK_FORMAT_MENU_ID));
        menu.setTitle(muse::TranslatableString("trackcontextmenu", "Format: %1")
                      .arg(formatInfo.description));
    }
}

void TrackContextMenuModel::updateTrackRateState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }

    const auto track = project->trackeditProject()->track(m_trackId);
    if (!track.has_value()) {
        return;
    }

    bool isOnAvailableRates = false;
    for (const auto& rate : audioDriverController()->sampleRates()) {
        MenuItem& item = findItem(makeTrackRateChangeAction(rate).toString());
        if (track.value().rate != rate) {
            item.setChecked(false);
            continue;
        }

        item.setChecked(true);
        isOnAvailableRates = true;
    }

    MenuItem& customRateItem = findItem(ActionCode("track-change-rate-custom"));
    customRateItem.setChecked(!isOnAvailableRates);

    MenuItem& menu = findMenu(QString::fromUtf8(TRACK_RATE_MENU_ID));
    //: %1 is a sample rate in hertz, e.g. "44100 Hz"
    menu.setTitle(muse::TranslatableString("trackcontextmenu", "Rate: %1 Hz")
                  .arg(muse::String::number(static_cast<int>(track.value().rate))));
}

void TrackContextMenuModel::updateTrackMonoState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }

    au::trackedit::ITrackeditProjectPtr trackeditProject = project->trackeditProject();
    if (!trackeditProject) {
        return;
    }

    const au::trackedit::TrackList trackList = trackeditProject->trackList();
    const auto it = std::find_if(trackList.begin(), trackList.end(), [this](const au::trackedit::Track& t) {
        return t.id == m_trackId;
    });

    if (it == trackList.end()) {
        return;
    }

    MenuItem& makeStereoItem = findItem(ActionCode("track-make-stereo"));
    if (!makeStereoItem.isValid()) {
        return;
    }

    const auto selectedTracks = selectionController()->selectedTracks();
    bool canMakeStereo = (selectedTracks.size() == 1)
                         && (it + 1 != trackList.end())
                         && (it->type == au::trackedit::TrackType::Mono)
                         && (it + 1)->type == au::trackedit::TrackType::Mono;

    auto state = makeStereoItem.state();
    state.enabled = canMakeStereo;
    makeStereoItem.setState(state);
}

void TrackContextMenuModel::updateRecordingState()
{
    bool recording = globalContext()->isRecording();

    MenuItem& formatMenu = findMenu(QString::fromUtf8(TRACK_FORMAT_MENU_ID));
    if (formatMenu.isValid()) {
        auto state = formatMenu.state();
        state.enabled = !recording;
        formatMenu.setState(state);
    }

    MenuItem& rateMenu = findMenu(QString::fromUtf8(TRACK_RATE_MENU_ID));
    if (rateMenu.isValid()) {
        auto state = rateMenu.state();
        state.enabled = !recording;
        rateMenu.setState(state);
    }
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeTrackColorItems()
{
    m_colorChangeActionCodeList.clear();

    MenuItemList items;
    const auto& colorInfos = projectSceneConfiguration()->clipColorInfos();
    for (const auto& info : colorInfos) {
        items << makeMenuItem(makeTrackColorChangeAction(info.index).toString(),
                              info.title);
        m_colorChangeActionCodeList.push_back(makeTrackColorChangeAction(info.index).toString());
    }

    return items;
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeTrackFormatItems()
{
    muse::uicomponents::MenuItemList items;
    for (const auto& formatInfo : trackedit::availableTrackFormats()) {
        items << makeMenuItem(makeTrackFormatChangeAction(formatInfo.format).toString(),
                              formatInfo.description);
    }
    return items;
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeTrackRateItems()
{
    muse::uicomponents::MenuItemList items;
    for (const auto& rate : audioDriverController()->sampleRates()) {
        items << makeMenuItem(makeTrackRateChangeAction(rate).toString(),
                              //: %1 is a sample rate in hertz, e.g. "44100 Hz"
                              muse::TranslatableString("trackcontextmenu", "%1 Hz").arg(muse::String::number(static_cast<int>(rate))));
    }
    items << makeSeparator();
    items << makeItemWithArg("track-change-rate-custom");
    return items;
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeTrackMoveItems()
{
    return {
        makeItemWithArg("track-move-up"),
        makeItemWithArg("track-move-down"),
        makeSeparator(),
        makeItemWithArg("track-move-top"),
        makeItemWithArg("track-move-bottom"),
    };
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeTrackVisualizationItems()
{
    m_trackViewTypeChangeActionCodeList.clear();
    muse::uicomponents::MenuItemList items;
    items.reserve(5);

    for (const std::string action : { TRACK_VIEW_WAVEFORM_ACTION,
                                      TRACK_VIEW_SPECTROGRAM_ACTION,
                                      TRACK_VIEW_MULTI_ACTION,
                                      TRACK_VIEW_HALF_WAVE_ACTION,
                                      TRACK_VIEW_SINGLE_WAVE_ACTION,
                                      "separator" }) {
        if (action == "separator") {
            items.push_back(makeSeparator());
        } else {
            muse::actions::ActionQuery query(action);
            query.addParam("trackId", muse::Val { m_trackId });
            items.push_back(makeMenuItem(query.toString()));
            m_trackViewTypeChangeActionCodeList.push_back(action);
        }
    }

    items.push_back(makeMenuItem(spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION));

    return items;
}

muse::uicomponents::MenuItemList TrackContextMenuModel::makeMeterMonitoringItems()
{
    return {
        makeItemWithArg("action://record/toggle-mic-metering"),
        makeSeparator(),
        makeItemWithArg("action://record/toggle-input-monitoring"),
    };
}
