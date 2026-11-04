/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Audacity-CLA-applies
 *
 * Audacity
 * A Digital Audio Editor
 *
 * Copyright (C) 2024 Audacity BVBA and others
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
#include "recentprojectsmodel.h"

#include "framework/global/translation.h"
#include "framework/global/dataformatter.h"
#include "framework/global/io/fileinfo.h"

#include "view/recentprojectcontextmenumodel.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>

using namespace muse;
using namespace au::project;

RecentProjectsModel::RecentProjectsModel(QObject* parent)
    : AbstractItemModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void RecentProjectsModel::load()
{
    updateRecentProjects();

    recentFilesController()->recentFilesListChanged().onNotify(this, [this]() {
        updateRecentProjects();
    });
}

void RecentProjectsModel::setRecentProjects(const std::vector<QVariantMap>& items)
{
    if (m_items == items) {
        return;
    }

    beginResetModel();
    clearContextMenuModels();
    m_items = items;
    endResetModel();
}

void RecentProjectsModel::clearContextMenuModels()
{
    for (QVariantMap& item : m_items) {
        auto* model = item[CONTEXT_MENU_MODEL_KEY].value<RecentProjectContextMenuModel*>();
        item[CONTEXT_MENU_MODEL_KEY] = QVariant();
        if (model != nullptr) {
            model->deleteLater();
        }
    }
}

void RecentProjectsModel::updateRecentProjects()
{
    const RecentFilesList& recentProjects = recentFilesController()->recentFilesList();

    std::vector<QVariantMap> items;
    items.reserve(recentProjects.size());

    QVariantMap addItem;
    addItem[NAME_KEY] = muse::qtrc("project", "New project");
    addItem[IS_CREATE_NEW_KEY] = true;
    addItem[IS_NO_RESULTS_FOUND_KEY] = false;
    addItem[IS_CLOUD_KEY] = false;
    items.push_back(addItem);

    for (const RecentFile& file : recentProjects) {
        QVariantMap obj;

        std::string suffix = io::suffix(file.path);

        RetVal<uint64_t> fileSize = fileSystem()->fileSize(file.path);
        QString fileSizeString = (fileSize.ret && fileSize.val > 0) ? DataFormatter::formatFileSize(fileSize.val).toQString() : QString();

        const bool isCloud = file.cloudRecord.has_value();
        const QString cloudProjectId = isCloud ? QString::fromStdString(file.cloudRecord->projectId) : QString();

        obj[NAME_KEY] = file.displayName(false);
        obj[PATH_KEY] = file.path.toQString();
        obj[THUMBNAIL_URL_KEY] = obj[PATH_KEY];
        obj[FILE_SIZE_KEY] = fileSizeString;
        obj[IS_CLOUD_KEY] = isCloud;
        obj[CLOUD_ITEM_ID_KEY] = cloudProjectId;
        obj[SHOW_INDICATOR_KEY] = obj[IS_CLOUD_KEY];
        obj[TIME_SINCE_MODIFIED_KEY] = DataFormatter::formatTimeSince(io::FileInfo(file.path).lastModified().date()).toQString();
        obj[IS_CREATE_NEW_KEY] = false;
        obj[IS_NO_RESULTS_FOUND_KEY] = false;

        // GlideMix: read from the saved file, so projects made earlier show it too.
        if (const std::optional<size_t> songs = au3ProjectReader()->readProjectSongCount(file.path)) {
            obj[TRACK_COUNT_KEY] = *songs == 1
                                   ? muse::qtrc("project", "1 track")
                                   : muse::qtrc("project", "%1 tracks").arg(static_cast<qulonglong>(*songs));
        }

        auto* menuModel = new RecentProjectContextMenuModel(isCloud, file.path.toQString(), file.displayNameOverride,
                                                            cloudProjectId, this);
        menuModel->setDeleteHandler([this](const QString& path) { deleteProject(path); });
        obj[CONTEXT_MENU_MODEL_KEY] = QVariant::fromValue(menuModel);

        items.push_back(obj);
    }

    QVariantMap noResultsFoundItem;
    noResultsFoundItem[NAME_KEY] = "";
    noResultsFoundItem[IS_CREATE_NEW_KEY] = false;
    noResultsFoundItem[IS_NO_RESULTS_FOUND_KEY] = true;
    noResultsFoundItem[IS_CLOUD_KEY] = false;
    items.push_back(noResultsFoundItem);

    setRecentProjects(items);
}

QList<int> RecentProjectsModel::nonProjectItemIndices() const
{
    return { 0, rowCount() - 1 };
}

void RecentProjectsModel::deleteProject(const QString& path)
{
    // GlideMix: a saved project can be deleted from Home. The file goes
    // to the Trash (so it can be put back); the tracks it used stay in the
    // music library, which is a separate database.
    if (globalContext() && globalContext()->currentProject()
        && globalContext()->currentProject()->path().toQString() == path) {
        interactive()->infoSync(muse::trc("project", "This project is open"),
                                muse::trc("project", "Close the project before deleting it."));
        return;
    }

    const QString name = QFileInfo(path).completeBaseName();
    auto trashBtn = interactive()->buttonData(muse::IInteractive::Button::Ok);
    trashBtn.text = muse::trc("project", "Move to Trash");
    trashBtn.accent = true;
    const auto cancelBtn = interactive()->buttonData(muse::IInteractive::Button::Cancel);
    const auto result = interactive()->questionSync(
        muse::qtrc("project", "Delete “%1”?").arg(name).toStdString(),
        muse::trc("project", "The project file is moved to the Trash. The tracks stay in your music library."),
        { cancelBtn, trashBtn }, trashBtn.btn);
    if (result.button() != trashBtn.btn) {
        return;
    }

    if (QFileInfo::exists(path) && !QFile::moveToTrash(path)) {
        interactive()->errorSync(muse::trc("project", "Couldn’t delete the project"),
                                 muse::qtrc("project", "“%1” could not be moved to the Trash.").arg(name).toStdString());
        return;
    }
    // The project database's side files, if SQLite left any.
    for (const char* suffix : { "-wal", "-shm", "-journal" }) {
        const QString side = path + QString::fromUtf8(suffix);
        if (QFileInfo::exists(side)) {
            QFile::moveToTrash(side);
        }
    }

    // The menu that asked belongs to the list about to be rebuilt.
    QTimer::singleShot(0, this, [this, path]() {
        recentFilesController()->removeRecentFile(muse::io::path_t(path));
    });
}

