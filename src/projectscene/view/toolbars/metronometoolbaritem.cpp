/*
 * GlideMix
 */
#include "metronometoolbaritem.h"

#include <cmath>

using namespace au::projectscene;
using namespace muse::ui;
using namespace muse::uicomponents;
using Voice = au::playback::IMetronome::Voice;

MetronomeToolBarItem::MetronomeToolBarItem(const UiAction& action, ToolBarItemType::Type type, QObject* parent)
    : ToolBarItem(action, type, parent)
{
    if (metronome()) {
        metronome()->settingsChanged().onNotify(this, [this]() {
            rebuildMenu();
            emit settingsChanged();
        });
    }
    rebuildMenu();
}

bool MetronomeToolBarItem::isOn() const
{
    return metronome() && metronome()->enabled();
}

QVariantList MetronomeToolBarItem::settingsMenu() const
{
    return menuItemListToVariantList(m_menu);
}

void MetronomeToolBarItem::toggle()
{
    if (metronome()) {
        metronome()->setEnabled(!metronome()->enabled());
    }
}

void MetronomeToolBarItem::handleSettingsMenuItem(const QString& itemId)
{
    const auto m = metronome();
    if (!m) {
        return;
    }
    const QStringList parts = itemId.split(':');
    const QString kind = parts.value(0);
    const double v = parts.value(1).toDouble();

    if (kind == "on") {
        m->setEnabled(!m->enabled());
    } else if (kind == "level") {
        m->setLevel(v / 100.0);
    } else if (kind == "voice") {
        m->setVoice(static_cast<Voice>(static_cast<int>(v)));
    } else if (kind == "pitch") {
        m->setTuning(static_cast<int>(v), m->tonality(), m->damp());
    } else if (kind == "tonality") {
        m->setTuning(m->pitch(), v / 4.0, m->damp());
    } else if (kind == "damp") {
        m->setTuning(m->pitch(), m->tonality(), v / 4.0);
    }
}

void MetronomeToolBarItem::rebuildMenu()
{
    // The open menu may still hold the previous items: retire them later.
    if (m_menuOwner) {
        m_menuOwner->deleteLater();
    }
    m_menuOwner = new QObject(this);
    m_menu.clear();
    const auto m = metronome();
    if (!m) {
        return;
    }

    // Same entries, order and values as the parked app's showMetronomeMenu().
    m_menu << makeItem("on", tr("Metronome"), m->enabled());
    m_menu << makeSeparator();
    m_menu << makeHeader(tr("Click level"));
    for (int pct : { 25, 50, 75, 100 }) {
        m_menu << makeItem(QString("level:%1").arg(pct), QString("%1%").arg(pct), std::lround(m->level() * 100) == pct);
    }
    m_menu << makeSeparator();
    m_menu << makeHeader(tr("Sound"));
    const QStringList voiceNames { tr("Classic"), tr("Wood"), tr("Click") };
    for (int i = 0; i < voiceNames.size(); ++i) {
        m_menu << makeItem(QString("voice:%1").arg(i), voiceNames[i], static_cast<int>(m->voice()) == i);
    }
    m_menu << makeSeparator();

    MenuItemList pitch;
    for (int st : { -12, -7, -5, -2, 0, 2, 5, 7, 12 }) {
        const QString title = st == 0 ? tr("0  (default)") : QString("%1%2 st").arg(st > 0 ? "+" : "").arg(st);
        pitch << makeItem(QString("pitch:%1").arg(st), title, m->pitch() == st);
    }
    MenuItemList tonality;
    const QStringList tonalNames { tr("Click"), tr("Mostly click"), tr("Balanced"), tr("Mostly tone"), tr("Tone") };
    for (int i = 0; i < tonalNames.size(); ++i) {
        tonality << makeItem(QString("tonality:%1").arg(i), tonalNames[i], std::lround(m->tonality() * 4) == i);
    }
    MenuItemList damp;
    const QStringList dampNames { tr("Long ring"), tr("Ring"), tr("Medium"), tr("Short"), tr("Very dry") };
    for (int i = 0; i < dampNames.size(); ++i) {
        damp << makeItem(QString("damp:%1").arg(i), dampNames[i], std::lround(m->damp() * 4) == i);
    }
    m_menu << makeSubmenu("tune", tr("Tune"), {
        makeSubmenu("tune-pitch", tr("Tuning"), pitch),
        makeSubmenu("tune-tonality", tr("Tonality"), tonality),
        makeSubmenu("tune-damp", tr("Damp"), damp),
    });
    m_menu << makeSeparator();
    m_menu << makeHeader(tr("Accent on beat 1"));
}

MenuItem* MetronomeToolBarItem::makeItem(const QString& id, const QString& title, bool checked)
{
    MenuItem* item = new MenuItem(m_menuOwner);
    item->setId(id);

    UiAction action;
    action.checkable = Checkable::Yes;
    action.title = muse::TranslatableString::untranslatable(muse::String::fromQString(title));
    item->setAction(action);

    UiActionState state;
    state.enabled = true;
    state.checked = checked;
    item->setState(state);
    return item;
}

MenuItem* MetronomeToolBarItem::makeHeader(const QString& title)
{
    MenuItem* item = new MenuItem(m_menuOwner);
    item->setId("header:" + title);

    UiAction action;
    action.title = muse::TranslatableString::untranslatable(muse::String::fromQString(title));
    item->setAction(action);

    UiActionState state;
    state.enabled = false;
    item->setState(state);
    return item;
}

MenuItem* MetronomeToolBarItem::makeSubmenu(const QString& id, const QString& title, const MenuItemList& items)
{
    MenuItem* item = new MenuItem(m_menuOwner);
    item->setId(id);
    item->setSubitems(items);

    UiAction action;
    action.title = muse::TranslatableString::untranslatable(muse::String::fromQString(title));
    item->setAction(action);

    UiActionState state;
    state.enabled = true;
    item->setState(state);
    return item;
}

MenuItem* MetronomeToolBarItem::makeSeparator()
{
    MenuItem* item = new MenuItem(m_menuOwner);
    item->setAction(UiAction());
    return item;
}
