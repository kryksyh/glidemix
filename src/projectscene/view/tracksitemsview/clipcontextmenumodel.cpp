/*
* Audacity: A Digital Audio Editor
*/
#include "clipcontextmenumodel.h"

#include "spectrogram/spectrogramtypes.h"
#include "trackedit/dom/track.h"
#include "framework/global/translation.h"
#include "global/realfn.h"
#include "ui/view/iconcodes.h"

#include <cstring>

using namespace au::projectscene;
using namespace muse::uicomponents;
using namespace muse::actions;

static const ActionCode ENABLE_STRETCH_CODE("stretch-clip-to-match-tempo");
static const ActionCode RENDER_PITCH_SPEED_CODE("clip-render-pitch-speed");
static const ActionCode RESET_PITCH_SPEED_CODE("clip-reset-pitch-speed");
static const ActionCodeList PITCH_SPEED_CODES { RENDER_PITCH_SPEED_CODE, RESET_PITCH_SPEED_CODE };

namespace {
constexpr const char* CLIP_COLOR_PICKER_ACTION = "mixhouse-clip-color-picker";
// GlideMix: Match Key / Transpose items carry the target transpose.
constexpr const char* KEY_TRANSPOSE_PREFIX = "mixtape-key-transpose:";
constexpr const char* COLOR_INTENSITY_ITEM = "mixtape-clip-color-intensity";
constexpr const char* COLOR_BY_KEY_ITEM = "mixtape-clip-color-by-key";
// GlideMix: the track's colour settings live in the clip's Color menu too,
// the one place for colours (the track menu no longer has them).
constexpr const char* TRACK_COLOR_ITEM = "mixtape-clip-track-color";
constexpr const char* RESET_COLORS_ITEM = "mixtape-clip-reset-colors";
constexpr const char* ANCHOR_ITEM_PREFIX = "mixtape-anchor:";
constexpr const char* PHRASE_RIBBON_ITEM = "mixtape-toggle-phrase-ribbon";
// GlideMix: the track menu's Track visualization, here too (Chris, 2026-10-27).
constexpr const char* TRACK_VIEW_WAVEFORM_ACTION = "action://trackedit/track-view-waveform";
constexpr const char* TRACK_VIEW_SPECTROGRAM_ACTION = "action://trackedit/track-view-spectrogram";
constexpr const char* TRACK_VIEW_MULTI_ACTION = "action://trackedit/track-view-multi";
constexpr const char* TRACK_VIEW_HALF_WAVE_ACTION = "action://projectscene/track-view-half-wave";
constexpr const char* TRACK_VIEW_SINGLE_WAVE_ACTION = "action://projectscene/track-view-single-wave";
const char* const TRACK_VIEW_ACTIONS[] = {
    TRACK_VIEW_WAVEFORM_ACTION, TRACK_VIEW_SPECTROGRAM_ACTION, TRACK_VIEW_MULTI_ACTION,
    TRACK_VIEW_HALF_WAVE_ACTION, TRACK_VIEW_SINGLE_WAVE_ACTION,
};
//! NOTE: can be moved to the framework
bool containsAny(const ActionCodeList& list, const ActionCodeList& actionCodes)
{
    return std::any_of(actionCodes.begin(), actionCodes.end(), [&](ActionCode code) {
        return std::find(list.begin(), list.end(), code) != list.end();
    });
}

static MenuItem* makeClipColorPickerItem(QObject* parent)
{
    auto* item = new MenuItem(parent);
    item->setId(QString::fromUtf8(CLIP_COLOR_PICKER_ACTION));
    item->setTitle(muse::TranslatableString("clip", "Clip color…"));
    return item;
}
}

void ClipContextMenuModel::load()
{
    AbstractMenuModel::load();

    auto makeColorIntensityItem = [this]() {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(COLOR_INTENSITY_ITEM));
        item->setTitle(muse::TranslatableString("clip", "Intensity…"));
        item->setEnabled(true);
        return item;
    };

    // GlideMix: every colour setting in one submenu, the same as the
    // track's. Color by key is per track; picking a colour turns it off.
    auto makeColorByKeyItem = [this]() {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(COLOR_BY_KEY_ITEM));
        item->setTitle(muse::TranslatableString("clip", "Color by key"));
        item->setEnabled(true);
        item->setCheckable(true);
        item->setChecked(colorByKey());
        return item;
    };

    auto makeSimpleItem = [this](const char* id, const muse::TranslatableString& title) {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(id));
        item->setTitle(title);
        item->setEnabled(true);
        return item;
    };

    auto makeItemWithArg = [this](const ActionCode& actionCode, const muse::TranslatableString& title = {}) {
        MenuItem* item = makeMenuItem(actionCode);
        item->setArgs(ActionData::make_arg1<trackedit::ClipKey>(m_clipKey.key));
        if (!title.isEmpty()) {
            item->setTitle(title);
        }
        return item;
    };

    auto enableStretchItem = makeItemWithArg(ENABLE_STRETCH_CODE);
    updateStretchEnabledState(*enableStretchItem);

    auto renderPitchSpeedItem = makeItemWithArg(RENDER_PITCH_SPEED_CODE);
    updatePitchSpeedModifiedEnabledState(*renderPitchSpeedItem);

    auto resetPitchSpeedItem = makeItemWithArg(RESET_PITCH_SPEED_CODE);
    updatePitchSpeedModifiedEnabledState(*resetPitchSpeedItem);

    MenuItemList cutAndItems {
        makeMenuItem("cut-leave-gap",
                     muse::TranslatableString("clip", "Cut and leave gap")),
        makeMenuItem("cut-per-track-ripple",
                     muse::TranslatableString("clip", "Cut and close gap on this track")),
        makeMenuItem("cut-all-tracks-ripple",
                     muse::TranslatableString("clip", "Cut and close gap on all tracks")),
    };

    MenuItemList deleteAndItems {
        makeMenuItem("delete-leave-gap",
                     muse::TranslatableString("clip", "Delete and leave gap")),
        makeMenuItem("delete-per-track-ripple",
                     muse::TranslatableString("clip", "Delete and close gap on this track")),
        makeMenuItem("delete-all-tracks-ripple",
                     muse::TranslatableString("clip", "Delete and close gap on all tracks")),
    };

    MenuItemList items {
        makeItemWithArg("clip-properties"),
        makeItemWithArg("rename-item", muse::TranslatableString("clip", "Rename clip")),
        makeMenu(muse::TranslatableString("clip", "Color"), {
            makeColorByKeyItem(),
            makeSeparator(),
            makeClipColorPickerItem(this),
            makeSimpleItem(TRACK_COLOR_ITEM, muse::TranslatableString("clip", "Track color…")),
            makeColorIntensityItem(),
            makeSeparator(),
            makeSimpleItem(RESET_COLORS_ITEM, muse::TranslatableString("clip", "Reset colors")),
        }, "menu-clip-color"),
        makeMenu(muse::TranslatableString("clip", "Track visualization"), makeTrackVisualizationItems(),
                 "menu-clip-track-visualization"),
        makeSeparator(),
        makeItemWithArg("action://trackedit/cut"),
        makeItemWithArg("action://trackedit/copy"),
        makeItemWithArg("duplicate"),
        makeItemWithArg("action://trackedit/delete"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("clip", "Cut and…"), cutAndItems, "menu-cut-and"),
        makeMenu(muse::TranslatableString("clip", "Delete and…"), deleteAndItems, "menu-delete-and"),
        makeSeparator(),
        makeItemWithArg("split"),
        makeItemWithArg("disjoin", muse::TranslatableString("clip", "Split at silences")),
        makeMenuItem("mixhouse-split-grid-jumps"),
        makeSeparator(),
        enableStretchItem,
        makeItemWithArg("clip-pitch-speed-open"),
        resetPitchSpeedItem,
        renderPitchSpeedItem,
        // GlideMix: the library track's beat grid (MixHouseBeatGrids)
        makeSeparator(),
        makeAnchorsMenu(),
        makeMenu(muse::TranslatableString("clip", "Beat grid"), {
            makeMenuItem("mixhouse-edit-beatgrid"),
            makeSeparator(),
            makeMenuItem("mixhouse-grid-earlier"),
            makeMenuItem("mixhouse-grid-later"),
            makeSeparator(),
            makeMenuItem("mixhouse-bar-earlier"),
            makeMenuItem("mixhouse-bar-later"),
            makeSeparator(),
            makeMenuItem("mixhouse-grid-faster"),
            makeMenuItem("mixhouse-grid-slower"),
            makeSeparator(),
            makeMenuItem("mixhouse-grid-undo"),
        }, "menu-beat-grid"),
    };

    // GlideMix: the phrase ribbon (rekordbox's phrase analysis), all clips.
    {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(PHRASE_RIBBON_ITEM));
        item->setTitle(muse::TranslatableString("clip", "Show phrase detection"));
        item->setCheckable(true);
        item->setChecked(mixhouseConfiguration() && mixhouseConfiguration()->phraseRibbonVisible());
        item->setEnabled(true);
        items.push_back(makeSeparator());
        items.push_back(item);
    }

    const MenuItemList keyItems = makeKeyItems();
    if (!keyItems.empty()) {
        items.push_back(makeSeparator());
        for (MenuItem* item : keyItems) {
            items.push_back(item);
        }
    }

    const auto project = globalContext()->currentProject();
    if (project && project->viewState()) {
        muse::ValCh<trackedit::TrackViewType> valCh = project->viewState()->trackViewType(m_clipKey.trackId());

        valCh.ch.onReceive(this, [this](auto) { load(); }, Mode::SetReplace);
    }

    trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (prj) {
        prj->clipList(m_clipKey.trackId()).onItemChanged(this, [this](const trackedit::Clip& clip) {
            if (clip.key == m_clipKey.key) {
                load();
            }
        }, muse::async::Asyncable::Mode::SetReplace);

        prj->trackChanged().onReceive(this, [this](const trackedit::Track& track) {
            if (track.id == m_clipKey.trackId()) {
                updateColorCheckedState();
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    setItems(items);

    updateTrackViewCheckedState();
    updateColorCheckedState();
    updateColorMenu();

    // Color by key can also change from the track menu or a colour pick.
    if (clipColors()) {
        clipColors()->trackColorsChanged().onReceive(this, [this](const trackedit::TrackId& track) {
            if (track == m_clipKey.trackId()) {
                findItem(QString::fromUtf8(COLOR_BY_KEY_ITEM)).setChecked(colorByKey());
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    // Show phrase detection can change from any clip's menu.
    if (mixhouseConfiguration()) {
        mixhouseConfiguration()->settingsChanged().onNotify(this, [this]() {
            MenuItem& item = findItem(QString::fromUtf8(PHRASE_RIBBON_ITEM));
            if (item.id() == QString::fromUtf8(PHRASE_RIBBON_ITEM)) {
                item.setChecked(mixhouseConfiguration()->phraseRibbonVisible());
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }
}

MenuItemList ClipContextMenuModel::makeKeyItems()
{
    // Match Key, from the parked MixHouse DJ app's clip menu: transpose this
    // clip toward the track it overlaps. Menu titles are mnemonic strings, so
    // a literal '&' in a track title is doubled.
    MenuItemList result;
    if (!keyMatch()) {
        return result;
    }
    using Keys = mixhouse::IMixHouseKeyMatch;
    const Keys::ClipKeyInfo info = keyMatch()->keyInfo(m_clipKey.key);
    const Keys::Match match = keyMatch()->match(m_clipKey.key);
    // Keys written the way Preferences > Music library says.
    const mixhouse::MixHouseKeyNotation notation = mixhouseConfiguration()
                                                   ? mixhouseConfiguration()->keyNotation()
                                                   : mixhouse::MixHouseKeyNotation::Lancelot;
    auto keyText = [notation](int keyId) { return Keys::keyText(keyId, notation); };

    auto makeItem = [this](const QString& id, const QString& title, bool enabled) {
        auto* item = new MenuItem(this);
        item->setId(id);
        QString escaped = title;
        escaped.replace(u'&', QStringLiteral("&&"));
        item->setTitle(muse::TranslatableString::untranslatable(escaped));
        item->setEnabled(enabled);
        return item;
    };
    auto signedSteps = [](int steps) {
        return (steps > 0 ? QStringLiteral("+") : QString()) + QString::number(steps);
    };
    auto transposeId = [](int steps) {
        return QString::fromUtf8(KEY_TRANSPOSE_PREFIX) + QString::number(steps);
    };

    if (info.keyId == 0) {
        result.push_back(makeItem(QStringLiteral("mixtape-key-none"),
                                  muse::qtrc("clip", "Match key (this track has no analysed key)"), false));
        return result;
    }

    if (!match.hasPartner) {
        result.push_back(makeItem(QStringLiteral("mixtape-key-none"),
                                  muse::qtrc("clip", "Match key (overlap a clip on another track first)"), false));
    } else if (match.partnerKeyId == 0) {
        result.push_back(makeItem(QStringLiteral("mixtape-key-none"),
                                  muse::qtrc("clip", "Match key (“%1” has no analysed key)")
                                  .arg(match.partnerTitle.left(24).trimmed()), false));
    } else if (match.steps == info.shiftSemitones) {
        const QString text = match.steps == 0
                             ? muse::qtrc("clip", "Key already matches — %1 ↔ %2")
                             .arg(keyText(info.effectiveKeyId), keyText(match.partnerKeyId))
                             : muse::qtrc("clip", "Key matched to “%1” — %2 ↔ %3")
                             .arg(match.partnerTitle.left(24).trimmed(), keyText(info.effectiveKeyId),
                                  keyText(match.partnerKeyId));
        result.push_back(makeItem(QStringLiteral("mixtape-key-none"), text, false));
    } else {
        result.push_back(makeItem(transposeId(match.steps),
                                  muse::qtrc("clip", "Match key to “%1” — %2 → %3 (%4 st)")
                                  .arg(match.partnerTitle.left(24).trimmed(), keyText(info.effectiveKeyId),
                                       keyText(Keys::scaleKeySteps(info.keyId, match.steps)),
                                       signedSteps(match.steps)), true));
    }

    // Manual transpose, always available: +-5 semitones reaches every key
    // (MixHouse DJ used Mixxx's +-6 key potmeter minus the no-op).
    MenuItemList transposeItems;
    for (int steps = 5; steps >= -5; --steps) {
        if (steps == 0) {
            continue;
        }
        MenuItem* item = makeItem(transposeId(steps),
                                  QStringLiteral("%1 st → %2").arg(signedSteps(steps),
                                                                  keyText(Keys::scaleKeySteps(info.keyId, steps))),
                                  true);
        item->setCheckable(true);
        item->setChecked(info.shiftSemitones == steps);
        transposeItems.push_back(item);
    }
    result.push_back(makeMenu(muse::TranslatableString("clip", "Transpose"), transposeItems, "menu-mixtape-transpose"));

    if (info.shiftSemitones != 0) {
        result.push_back(makeItem(transposeId(0),
                                  muse::qtrc("clip", "Reset key (now %1 st, %2 → %3)")
                                  .arg(signedSteps(info.shiftSemitones), keyText(info.keyId),
                                       keyText(info.effectiveKeyId)), true));
    }
    return result;
}

void ClipContextMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId == QString::fromUtf8(PHRASE_RIBBON_ITEM)) {
        if (mixhouseConfiguration()) {
            const bool visible = !mixhouseConfiguration()->phraseRibbonVisible();
            mixhouseConfiguration()->setPhraseRibbonVisible(visible);
            // The menu is built once per clip; keep its tick in step.
            findItem(itemId).setChecked(visible);
        }
        return;
    }
    if (itemId == QString::fromUtf8(TRACK_COLOR_ITEM)) {
        emit trackColorPickerRequested();
        return;
    }
    if (itemId == QString::fromUtf8(RESET_COLORS_ITEM)) {
        emit resetColorsRequested();
        return;
    }
    if (itemId == QString::fromUtf8(COLOR_INTENSITY_ITEM)) {
        emit clipColorIntensityRequested();
        return;
    }
    if (itemId.startsWith(QString::fromUtf8(ANCHOR_ITEM_PREFIX))) {
        emit anchorActionRequested(itemId.mid(int(std::strlen(ANCHOR_ITEM_PREFIX))));
        return;
    }
    if (itemId == QString::fromUtf8(COLOR_BY_KEY_ITEM)) {
        setColorByKey(!colorByKey());
        findItem(itemId).setChecked(colorByKey());
        return;
    }
    if (itemId.startsWith(QString::fromUtf8(KEY_TRANSPOSE_PREFIX))) {
        if (keyMatch()) {
            keyMatch()->transpose(m_clipKey.key, itemId.mid(int(std::strlen(KEY_TRANSPOSE_PREFIX))).toInt());
        }
        return;
    }
    if (itemId == CLIP_COLOR_PICKER_ACTION) {
        emit clipColorPickerRequested();
    } else if (itemId == spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION) {
        const auto project = globalContext()->currentProject();
        IF_ASSERT_FAILED(project) {
            return;
        }
        const auto trackId = m_clipKey.trackId();
        const auto track = project->trackeditProject()->track(trackId);
        IF_ASSERT_FAILED(track) {
            return;
        }
        const muse::String trackTitle = track->title;
        auto args = muse::actions::ActionData::make_arg2(trackId, trackTitle);
        dispatcher()->dispatch(spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION, std::move(args));
    } else {
        AbstractMenuModel::handleMenuItem(itemId);
    }
}

QVariantList ClipContextMenuModel::colorPalette() const
{
    QVariantList palette;
    palette.reserve(96);
    for (int index = 1; index <= 96; ++index) {
        palette.push_back(QVariantMap { { "index", index },
                                        { "color", projectSceneConfiguration()->clipColor(index).toQColor() } });
    }
    return palette;
}

int ClipContextMenuModel::currentColorIndex() const
{
    const auto project = globalContext()->currentProject();
    const auto clip = project ? project->trackeditProject()->clip(m_clipKey.key) : trackedit::Clip {};
    return clip.isValid() && !clip.isAutoColor ? clip.colorIndex : 0;
}

void ClipContextMenuModel::setClipColor(int colorIndex)
{
    if (colorIndex < 1 || colorIndex > 96) { return; }
    dispatcher()->dispatch(makeClipColorChangeAction(colorIndex));
    setColorByKey(false);   // show the colour just picked, not the key's
    emit clipColorChanged();
}

void ClipContextMenuModel::resetClipColor()
{
    dispatcher()->dispatch("action://trackedit/clip/change-color-auto");
    setColorByKey(false);
    emit clipColorChanged();
}

ClipKey ClipContextMenuModel::clipKey() const
{
    return m_clipKey;
}

void ClipContextMenuModel::setClipKey(const ClipKey& newClipKey)
{
    m_clipKey = newClipKey;
    emit clipKeyChanged();
}

void ClipContextMenuModel::onActionsStateChanges(const muse::actions::ActionCodeList& codes)
{
    AbstractMenuModel::onActionsStateChanges(codes);

    if (containsAction(codes, ENABLE_STRETCH_CODE)) {
        MenuItem& item = findItem(ActionCode(ENABLE_STRETCH_CODE));
        updateStretchEnabledState(item);
    }

    if (containsAny(codes, PITCH_SPEED_CODES)) {
        for (const auto& code : PITCH_SPEED_CODES) {
            MenuItem& item = findItem(ActionCode(code));
            updatePitchSpeedModifiedEnabledState(item);
        }
    }

    if (containsAny(codes, m_colorChangeActionCodeList)) {
        updateColorCheckedState();
    }

    for (const char* action : TRACK_VIEW_ACTIONS) {
        if (containsAction(codes, ActionCode(action))) {
            updateTrackViewCheckedState();
            break;
        }
    }
}

//! The same items as the track menu's Track visualization, for the clip's track.
MenuItemList ClipContextMenuModel::makeTrackVisualizationItems()
{
    MenuItemList items;
    for (const char* action : TRACK_VIEW_ACTIONS) {
        muse::actions::ActionQuery query(action);
        query.addParam("trackId", muse::Val { m_clipKey.trackId() });
        items.push_back(makeMenuItem(query.toString()));
    }
    items.push_back(makeSeparator());
    items.push_back(makeMenuItem(spectrogram::TRACK_SPECTROGRAM_SETTINGS_ACTION));
    return items;
}

void ClipContextMenuModel::updateTrackViewCheckedState()
{
    const auto prj = globalContext()->currentProject();
    if (!prj || !prj->viewState()) {
        return;
    }
    const auto trackId = m_clipKey.trackId();
    const auto viewType = prj->viewState()->trackViewType(trackId).val;
    for (const char* action : TRACK_VIEW_ACTIONS) {
        MenuItem& item = findItem(ActionCode(action));
        if (!item.isValid()) {
            continue;
        }
        const std::string code(action);
        auto state = item.state();
        if (code == TRACK_VIEW_HALF_WAVE_ACTION) {
            state.checked = prj->viewState()->isHalfWave(trackId).val;
        } else if (code == TRACK_VIEW_SINGLE_WAVE_ACTION) {
            state.checked = prj->viewState()->isSingleWave(trackId).val;
        } else {
            state.checked = (code == TRACK_VIEW_WAVEFORM_ACTION && viewType == trackedit::TrackViewType::Waveform)
                            || (code == TRACK_VIEW_SPECTROGRAM_ACTION && viewType == trackedit::TrackViewType::Spectrogram)
                            || (code == TRACK_VIEW_MULTI_ACTION && viewType == trackedit::TrackViewType::WaveformAndSpectrogram);
        }
        item.setState(state);
    }
}

void ClipContextMenuModel::updateStretchEnabledState(MenuItem& item)
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }
    auto state = item.state();
    state.checked = clip.stretchToMatchTempo;
    item.setState(state);
}

void ClipContextMenuModel::updatePitchSpeedModifiedEnabledState(MenuItem& item)
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }

    const bool hasPitchOrSpeed = clip.pitch != 0 || !muse::RealIsEqual(clip.speed, 1.0);
    auto state = item.state();
    state.enabled = state.enabled && hasPitchOrSpeed;
    item.setState(state);
}

void ClipContextMenuModel::updateColorCheckedState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }

    const auto trackOpt = project->trackeditProject()->track(m_clipKey.trackId());
    if (trackOpt) {
        const muse::Color trackColor = projectSceneConfiguration()->clipColor(trackOpt->colorIndex);
        MenuItem& autoColorItem = findItem(muse::actions::ActionCode("action://trackedit/clip/change-color-auto"));
        muse::ui::UiAction action = autoColorItem.action();
        action.iconCode = muse::ui::IconCode::Code::FRETBOARD_MARKER_CIRCLE_FILLED;
        action.iconColor = QString::fromStdString(trackColor.toString());
        autoColorItem.setAction(action);
    }

    for (const auto& action : m_colorChangeActionCodeList) {
        MenuItem& item = findItem(ActionCode(action));
        ActionQuery query(action);

        bool checked = false;
        if (clip.isAutoColor && action == m_colorChangeActionCodeList.at(0)) {
            checked = true;
        } else if (!clip.isAutoColor && query.contains("colorindex") && query.param("colorindex").toInt() == clip.colorIndex) {
            checked = true;
        }

        auto state = item.state();
        state.checked = checked;
        item.setState(state);
    }
}

void ClipContextMenuModel::updateColorMenu()
{
    // The floating palette item has no nested legacy menu to update.
}

MenuItemList ClipContextMenuModel::makeClipColourItems()
{
    m_colorChangeActionCodeList.clear();
    MenuItemList items;
    items <<
        makeMenuItem("action://trackedit/clip/change-color-auto",
                     muse::TranslatableString("clip", "Same as track color"));
    items << makeSeparator();
    m_colorChangeActionCodeList.push_back("action://trackedit/clip/change-color-auto");

    const auto& colorInfos = projectSceneConfiguration()->clipColorInfos();
    for (const auto& info : colorInfos) {
        items << makeMenuItem(makeClipColorChangeAction(info.index).toString(),
                              info.title);
        m_colorChangeActionCodeList.push_back(makeClipColorChangeAction(info.index).toString());
    }

    return items;
}

//! GlideMix: MixMeister's IN / OUT anchors, placed by hand where the
//! menu was opened. A library track always has both; Reset puts them back
//! where the phrases say.
MenuItem* ClipContextMenuModel::makeAnchorsMenu()
{
    auto makeItem = [this](const char* action, const muse::TranslatableString& title) {
        auto* item = new MenuItem(this);
        item->setId(QString::fromUtf8(ANCHOR_ITEM_PREFIX) + QString::fromUtf8(action));
        item->setTitle(title);
        item->setEnabled(true);
        return item;
    };
    MenuItem* menu = makeMenu(muse::TranslatableString("clip", "Anchors"), {
        makeItem("set-in", muse::TranslatableString("clip", "Set IN anchor here")),
        makeItem("set-out", muse::TranslatableString("clip", "Set OUT anchor here")),
        makeSeparator(),
        makeItem("reset", muse::TranslatableString("clip", "Reset anchors")),
    }, "menu-mixtape-anchors");
    const bool hasAnchors = beatGrids() && beatGrids()->clipAnchors(m_clipKey.key).valid;
    menu->setEnabled(hasAnchors);
    return menu;
}

int ClipContextMenuModel::colorIntensity() const
{
    return clipColors() ? clipColors()->intensity(m_clipKey.trackId()) : mixhouse::IMixHouseClipColors::kFullIntensity;
}

void ClipContextMenuModel::setColorIntensity(int intensity)
{
    if (clipColors() && colorIntensity() != intensity) {
        clipColors()->setIntensity(m_clipKey.trackId(), intensity);
        emit colorIntensitySettingsChanged();
    }
}

bool ClipContextMenuModel::colorByKey() const
{
    return clipColors() && clipColors()->colorByKey(m_clipKey.trackId());
}

void ClipContextMenuModel::setColorByKey(bool on)
{
    if (clipColors() && colorByKey() != on) {
        clipColors()->setColorByKey(m_clipKey.trackId(), on);
        emit colorIntensitySettingsChanged();
    }
}
