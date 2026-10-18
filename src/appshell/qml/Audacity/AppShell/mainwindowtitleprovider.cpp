/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
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
#include "mainwindowtitleprovider.h"
#include "translation.h"

using namespace au::appshell;
using namespace au::project;

namespace {
static QString appDisplayName()
{
    // Temporary product name (2026-09-13). AU4_APP_TITLE_VERSION
    // ("Audacity 4.0 dev") is left defined for anything else that uses it.
    return QStringLiteral("GlideMix");
}
}

MainWindowTitleProvider::MainWindowTitleProvider(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString MainWindowTitleProvider::displayName() const
{
    // GlideMix: the demo version says so in the title bar.
    if (licence() && licence()->tier() == au::mixhouse::LicenceTier::Demo) {
        return appDisplayName() + QStringLiteral(" Demo");
    }
    return appDisplayName();
}

void MainWindowTitleProvider::load()
{
    update();
    if (licence()) {
        licence()->tierChanged().onNotify(this, [this]() { update(); });
    }
    context()->currentProjectChanged().onNotify(this, [this]() {
        if (auto currentProject = context()->currentProject()) {
            currentProject->displayNameChanged().onNotify(this, [this]() {
                update();
            }, muse::async::Asyncable::Mode::SetReplace);

            currentProject->needSave().notification.onNotify(this, [this]() {
                update();
            }, muse::async::Asyncable::Mode::SetReplace);
        }

        update();
    }, muse::async::Asyncable::Mode::SetReplace);
}

QString MainWindowTitleProvider::title() const
{
    return m_title;
}

QString MainWindowTitleProvider::filePath() const
{
    return m_filePath;
}

bool MainWindowTitleProvider::fileModified() const
{
    return m_fileModified;
}

void MainWindowTitleProvider::setTitle(const QString& title)
{
    if (title == m_title) {
        return;
    }

    m_title = title;
    emit titleChanged(title);
}

void MainWindowTitleProvider::setFilePath(const QString& filePath)
{
    if (filePath == m_filePath) {
        return;
    }

    m_filePath = filePath;
    emit filePathChanged(filePath);
}

void MainWindowTitleProvider::setFileModified(bool fileModified)
{
    if (fileModified == m_fileModified) {
        return;
    }

    m_fileModified = fileModified;
    emit fileModifiedChanged(fileModified);
}

void MainWindowTitleProvider::update()
{
    IAudacityProjectPtr project = context()->currentProject();

    if (!project) {
        setTitle(displayName());
        setFilePath("");
        setFileModified(false);
        return;
    }

    const QString projectTitle = project->title().toQString();
    const bool projectModified = project->hasUnsavedChanges();
    setTitle(projectTitle.isEmpty()
             ? displayName()
             //: %1 is the project title, %2 is the modified marker ("*") shown when there are unsaved changes, %3 is the app name
             : muse::qtrc("appshell", "%1 %2 - %3").arg(projectTitle, projectModified ? muse::qtrc("appshell", "*") : QString(),
                                                        displayName()));

    setFilePath(project->path().toQString());
    setFileModified(projectModified);
}
