/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "shortcutsmodel.h"

#include <QStringList>

#include "containers.h"
#include "translation.h"
#include "types/mnemonicstring.h"
#include "types/translatablestring.h"
#include "shortcutcontext.h"

#include "log.h"

using namespace muse::shortcuts;
using namespace muse::ui;

static std::vector<std::string> shortcutsFileFilter()
{
    return { muse::trc("shortcuts", "Shortcuts file") + " (*.xml)" };
}

ShortcutsModel::ShortcutsModel(QObject* parent)
    : QAbstractListModel(parent), Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariant ShortcutsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    const Shortcut& shortcut = m_shortcuts.at(index.row());

    switch (role) {
    case RoleTitle: return actionText(shortcut.action);
    case RoleIcon: return static_cast<int>(this->action(shortcut.action).iconCode);
    case RoleSequence: return sequencesToNativeText(shortcut.sequences);
    case RoleSearchKey: {
        const UiAction& action = this->action(shortcut.action);
        QStringList searchKeyItems;
        searchKeyItems << QString::fromStdString(action.code)
                       << action.title.qTranslatedWithoutMnemonic()
                       << action.description.qTranslated()
                       << sequencesToNativeText(shortcut.sequences);
        return searchKeyItems.join(u' ');
    }
    }

    return QVariant();
}

const UiAction& ShortcutsModel::action(const std::string& actionCode) const
{
    return uiactionsRegister()->action(actionCode);
}

QString ShortcutsModel::actionText(const std::string& actionCode) const
{
    const UiAction& action = this->action(actionCode);

    const QString title = action.title.qTranslatedWithoutMnemonic();
    if (action.description.isEmpty()) {
        return title;
    }

    // A description is usually the action's full name ("Paste (overlaps
    // other clips)"). Where it is a sentence about what the action does
    // instead, it does not say the name, so the list could not be searched
    // or read by it: show "Name — sentence".
    const QString description = action.description.qTranslated();
    QString name = title;
    name.remove(QChar(0x2026)).remove(QStringLiteral("..."));
    name = name.trimmed();
    if (name.isEmpty() || description.contains(name, Qt::CaseInsensitive)) {
        return description;
    }
    if (name.contains(description, Qt::CaseInsensitive)) {
        return title;   // the description only repeats part of the name
    }
    return title + QStringLiteral(" \u2014 ") + description;
}

int ShortcutsModel::rowCount(const QModelIndex&) const
{
    return m_shortcuts.size();
}

QHash<int, QByteArray> ShortcutsModel::roleNames() const
{
    static const QHash<int, QByteArray> roles {
        { RoleTitle, "title" },
        { RoleIcon, "icon" },
        { RoleSequence, "sequence" },
        { RoleSearchKey, "searchKey" }
    };

    return roles;
}

void ShortcutsModel::load()
{
    beginResetModel();
    m_shortcuts.clear();

    for (const UiAction& action : uiactionsRegister()->actionList()) {
        if (action.scCtx == CTX_DISABLED) {
            continue;
        }

        Shortcut shortcut = shortcutsRegister()->shortcut(action.code);
        if (!shortcut.isValid()) {
            shortcut.action = action.code;
            shortcut.context = action.scCtx;
        }

        m_shortcuts << shortcut;
    }

    shortcutsRegister()->shortcutsChanged().onNotify(this, [this]() {
        load();
    }, async::Asyncable::Mode::SetReplace);

    std::sort(m_shortcuts.begin(), m_shortcuts.end(), [this](const Shortcut& s1, const Shortcut& s2) {
        return actionText(s1.action) < actionText(s2.action);
    });

    endResetModel();
}

bool ShortcutsModel::apply()
{
    ShortcutList shortcuts;

    for (const Shortcut& shortcut : std::as_const(m_shortcuts)) {
        shortcuts.push_back(shortcut);
    }

    Ret ret = shortcutsRegister()->setShortcuts(shortcuts);

    if (!ret) {
        LOGE() << ret.toString();
    }

    return ret;
}

void ShortcutsModel::reset()
{
    shortcutsRegister()->resetShortcuts();
}

QItemSelection ShortcutsModel::selection() const
{
    return m_selection;
}

QVariant ShortcutsModel::currentShortcut() const
{
    QModelIndex index = currentShortcutIndex();
    if (!index.isValid()) {
        return QVariant();
    }

    const Shortcut& sc = m_shortcuts.at(index.row());
    return shortcutToObject(sc);
}

QModelIndex ShortcutsModel::currentShortcutIndex() const
{
    if (m_selection.size() == 1) {
        return m_selection.indexes().first();
    }

    return QModelIndex();
}

void ShortcutsModel::setSelection(const QItemSelection& selection)
{
    if (m_selection == selection) {
        return;
    }

    m_selection = selection;
    emit selectionChanged();
}

void ShortcutsModel::importShortcutsFromFile()
{
    io::path_t path = interactive()->selectOpeningFileSync(
        muse::trc("shortcuts", "Import shortcuts"),
        globalConfiguration()->homePath(),
        shortcutsFileFilter());

    if (!path.empty()) {
        shortcutsRegister()->importFromFile(path);
    }
}

void ShortcutsModel::exportShortcutsToFile()
{
    io::path_t path = interactive()->selectSavingFileSync(
        muse::trc("shortcuts", "Export shortcuts"),
        globalConfiguration()->homePath(),
        shortcutsFileFilter());

    if (path.empty()) {
        return;
    }

    Ret ret = shortcutsRegister()->exportToFile(path);
    if (!ret) {
        LOGE() << ret.toString();
    }
}

void ShortcutsModel::applySequenceToCurrentShortcut(const QString& newSequence, int conflictShortcutIndex)
{
    QModelIndex currIndex = currentShortcutIndex();
    if (!currIndex.isValid()) {
        return;
    }

    int row = currIndex.row();
    m_shortcuts[row].sequences = Shortcut::sequencesFromString(newSequence.toStdString());

    if (conflictShortcutIndex >= 0 && conflictShortcutIndex < m_shortcuts.size()) {
        const std::vector<std::string>& newSequences = m_shortcuts[row].sequences;
        Shortcut& conflictShortcut = m_shortcuts[conflictShortcutIndex];

        muse::remove_if(conflictShortcut.sequences, [&newSequences](const std::string& sequence) {
            return muse::contains(newSequences, sequence);
        });

        if (conflictShortcut.sequences.empty()) {
            conflictShortcut.clear();
        }

        notifyAboutShortcutChanged(index(conflictShortcutIndex));
    }

    notifyAboutShortcutChanged(currIndex);
}

void ShortcutsModel::clearSelectedShortcuts()
{
    for (const QModelIndex& index : m_selection.indexes()) {
        Shortcut& shortcut = m_shortcuts[index.row()];
        shortcut.clear();

        notifyAboutShortcutChanged(index);
    }
}

void ShortcutsModel::notifyAboutShortcutChanged(const QModelIndex& index)
{
    emit dataChanged(index, index);
}

void ShortcutsModel::resetToDefaultSelectedShortcuts()
{
    auto resolveConflicts = [this](const Shortcut& shortcut) {
        if (shortcut.sequences.empty()) {
            return;
        }

        for (int i = 0; i < m_shortcuts.size(); ++i) {
            Shortcut& sc = m_shortcuts[i];

            if (shortcut == sc) {
                continue;
            }

            if (!areContextPrioritiesEqual(shortcut.context, sc.context)) {
                continue;
            }

            if (shortcut.sequences == sc.sequences) {
                sc.clear();
                notifyAboutShortcutChanged(index(i));
            }
        }
    };

    for (const QModelIndex& index : m_selection.indexes()) {
        Shortcut& shortcut = m_shortcuts[index.row()];

        const Shortcut& defaultShortcut = shortcutsRegister()->defaultShortcut(shortcut.action);
        if (defaultShortcut.isValid()) {
            shortcut = defaultShortcut;
        } else {
            shortcut.sequences = {};
        }

        resolveConflicts(shortcut);

        notifyAboutShortcutChanged(index);
    }
}

QVariantList ShortcutsModel::shortcuts() const
{
    QVariantList result;

    for (const Shortcut& shortcut : std::as_const(m_shortcuts)) {
        result << shortcutToObject(shortcut);
    }

    return result;
}

QVariant ShortcutsModel::shortcutToObject(const Shortcut& shortcut) const
{
    QVariantMap obj;
    obj["title"] = actionText(shortcut.action);
    obj["sequence"] = QString::fromStdString(shortcut.sequencesAsString());
    obj["context"] = QString::fromStdString(shortcut.context);
    obj["autoRepeat"] = shortcut.autoRepeat;

    return obj;
}
