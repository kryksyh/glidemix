/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "mixhouse/imixhouselibrary.h"
#include "context/iglobalcontext.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "iprojectsceneconfiguration.h"
#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/imixhouseclipcolors.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "types/projectscenetypes.h"

namespace au::projectscene {
class ClipContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::GlobalInject<projectscene::IProjectSceneConfiguration> projectSceneConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<mixhouse::IMixHouseKeyMatch> keyMatch{ this };   // GlideMix: Match Key
    muse::ContextInject<mixhouse::IMixHouseClipColors> clipColors{ this };   // GlideMix: colour intensity
    muse::ContextInject<mixhouse::IMixHouseLibrary> library{ this };   // GlideMix: Show in Library
    muse::ContextInject<mixhouse::IMixHouseBeatGrids> beatGrids{ this };   // GlideMix: IN / OUT anchors
    muse::GlobalInject<mixhouse::IMixHouseLibraryConfiguration> mixhouseConfiguration;   // GlideMix: phrase ribbon

    Q_PROPERTY(ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY clipKeyChanged FINAL)
    Q_PROPERTY(QVariantList colorPalette READ colorPalette CONSTANT FINAL)
    Q_PROPERTY(int currentColorIndex READ currentColorIndex NOTIFY clipColorChanged FINAL)
    // GlideMix: the clip's track colour intensity (0..255) and Colour by key.
    Q_PROPERTY(int colorIntensity READ colorIntensity WRITE setColorIntensity NOTIFY colorIntensitySettingsChanged FINAL)
    Q_PROPERTY(bool colorByKey READ colorByKey WRITE setColorByKey NOTIFY colorIntensitySettingsChanged FINAL)

public:
    ClipContextMenuModel() = default;

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void handleMenuItem(const QString& itemId) override;
    Q_INVOKABLE void setClipColor(int colorIndex);
    Q_INVOKABLE void resetClipColor();

    ClipKey clipKey() const;
    void setClipKey(const ClipKey& newClipKey);
    QVariantList colorPalette() const;
    int currentColorIndex() const;
    int colorIntensity() const;
    void setColorIntensity(int intensity);
    bool colorByKey() const;
    void setColorByKey(bool on);

signals:
    void clipKeyChanged();
    void clipTitleEditRequested();
    void clipColorPickerRequested();
    void trackColorPickerRequested();   // GlideMix: the clip's track's colour
    void resetColorsRequested();
    void clipColorIntensityRequested();
    //! GlideMix: "set-in" / "set-out" at the point the menu was opened,
    //! or "reset". The clip view does it: only it knows where that point is.
    void anchorActionRequested(const QString& action);
    void colorIntensitySettingsChanged();
    void clipColorChanged();

private:
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;
    void updateStretchEnabledState(muse::uicomponents::MenuItem& item);
    void updatePitchSpeedModifiedEnabledState(muse::uicomponents::MenuItem& item);

    muse::uicomponents::MenuItemList makeClipColourItems();
    muse::uicomponents::MenuItemList makeTrackVisualizationItems();
    void updateTrackViewCheckedState();

    muse::uicomponents::MenuItem* makeAnchorsMenu();
    muse::uicomponents::MenuItemList makeKeyItems();
    void updateColorCheckedState();
    void updateColorMenu();

    ClipKey m_clipKey;
    muse::actions::ActionCodeList m_colorChangeActionCodeList;
};
}
