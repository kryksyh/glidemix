/*
 * GlideMix
 */
#pragma once

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"
#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

#include "playback/imetronome.h"

namespace au::projectscene {
//! The metronome button: click toggles it, the menu (right-click or the
//! corner triangle) holds the parked MixHouse DJ app's metronome menu --
//! click level, sound, and Tune (tuning, tonality, damp).
class MetronomeToolBarItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT

    Q_PROPERTY(bool isOn READ isOn NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QVariantList settingsMenu READ settingsMenu NOTIFY settingsChanged FINAL)

    muse::GlobalInject<playback::IMetronome> metronome;

public:
    explicit MetronomeToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                  QObject* parent = nullptr);

    bool isOn() const;
    QVariantList settingsMenu() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void handleSettingsMenuItem(const QString& itemId);

signals:
    void settingsChanged();

private:
    void rebuildMenu();

    muse::uicomponents::MenuItem* makeItem(const QString& id, const QString& title, bool checked);
    muse::uicomponents::MenuItem* makeHeader(const QString& title);
    muse::uicomponents::MenuItem* makeSubmenu(const QString& id, const QString& title, const muse::uicomponents::MenuItemList& items);
    muse::uicomponents::MenuItem* makeSeparator();

    QObject* m_menuOwner = nullptr;
    muse::uicomponents::MenuItemList m_menu;
};
}
