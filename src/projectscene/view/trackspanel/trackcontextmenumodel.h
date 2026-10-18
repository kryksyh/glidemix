/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "audio/driver/iaudiodrivercontroller.h"
#include "context/iglobalcontext.h"
#include "iprojectsceneconfiguration.h"
#include "trackedit/iprojecthistory.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/internal/itracknavigationcontroller.h"
#include "trackedit/itrackeditinteraction.h"
#include "mixhouse/imixhouseclipcolors.h"
#include "mixhouse/imixhousestems.h"

namespace au::projectscene {
class TrackContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::GlobalInject<projectscene::IProjectSceneConfiguration> projectSceneConfiguration;
    muse::GlobalInject<audio::IAudioDriverController> audioDriverController;

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<trackedit::ITrackNavigationController> trackNavigationController{ this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction{ this };
    muse::ContextInject<mixhouse::IMixHouseClipColors> clipColors{ this };   // GlideMix: Reset colors
    muse::ContextInject<mixhouse::IMixHouseStems> stems{ this };   // GlideMix: Separate stems

    Q_PROPERTY(trackedit::TrackId trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged FINAL)
    Q_PROPERTY(QVariantList colorPalette READ colorPalette NOTIFY trackColorChanged FINAL)
    Q_PROPERTY(int currentColorIndex READ currentColorIndex NOTIFY trackColorChanged FINAL)

public:
    TrackContextMenuModel() = default;

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void setTrackColor(int colorIndex);
    //! GlideMix: the clip menu's Reset colors (the track's clips, key colour, intensity).
    Q_INVOKABLE void resetColors();

    trackedit::TrackId trackId() const;
    void setTrackId(const trackedit::TrackId& newTrackId);
    QVariantList colorPalette() const;
    int currentColorIndex() const;
    void handleMenuItem(const QString& itemId) override;

signals:
    void trackIdChanged();
    void trackRenameRequested();
    void trackColorPickerRequested();
    void trackColorChanged();
    void openRequested();

private:
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;

    muse::uicomponents::MenuItemList makeMonoTrackItems();
    muse::uicomponents::MenuItemList makeStereoTrackItems();
    muse::uicomponents::MenuItemList makeLabelTrackItems();

    muse::uicomponents::MenuItemList makeTrackColorItems();
    muse::uicomponents::MenuItemList makeTrackFormatItems();
    muse::uicomponents::MenuItemList makeTrackRateItems();
    muse::uicomponents::MenuItemList makeTrackMoveItems();
    muse::uicomponents::MenuItemList makeTrackVisualizationItems();
    muse::uicomponents::MenuItemList makeMeterMonitoringItems();

    muse::uicomponents::MenuItemList makeColorItems();
    muse::uicomponents::MenuItem* makeSeparateStemsItem();
    muse::uicomponents::MenuItem* makeItemWithArg(const muse::actions::ActionCode& actionCode);

    //! GlideMix: Audacity's own look for this track -- every clip
    //! follows the track colour again, Colour by key on (the default), full intensity.
    void resetTrackColors();

    void updateColorCheckedState();
    void updateTrackFormatState();
    void updateTrackRateState();
    void updateTrackMonoState();
    void updateTrackViewCheckedState();
    void updateRecordingState();

    trackedit::TrackId m_trackId;
    muse::actions::ActionCodeList m_colorChangeActionCodeList;
    muse::actions::ActionCodeList m_trackViewTypeChangeActionCodeList;
};
}
