/*
 * Audacity: A Digital Audio Editor
 */

#include <algorithm>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>

#include "global/translation.h"

#include "project/internal/au3/au3metadata.h"

#include "metadatamodel.h"

using namespace au::importexport;
using namespace muse;
using namespace muse::io;

namespace {
//! Longest side of an embedded picture; players and DJ gear don't need more
constexpr int ARTWORK_MAX_SIDE = 1400;

//! A copy in the app's data folder, so the picture stays when the original moves.
//! Returns an empty string when the file is not a picture we can read.
QString storeArtwork(const QString& sourcePath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = source.readAll();

    QImage image;
    if (!image.loadFromData(bytes)) {
        return {};
    }

    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/artwork";
    if (!QDir().mkpath(dirPath)) {
        return {};
    }

    const QString name = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    const bool usable = suffix == "jpg" || suffix == "jpeg" || suffix == "png";

    if (usable && std::max(image.width(), image.height()) <= ARTWORK_MAX_SIDE) {
        const QString path = dirPath + "/" + name + "." + suffix;
        QFile target(path);
        if (target.exists()) {
            return path;
        }
        return target.open(QIODevice::WriteOnly) && target.write(bytes) == bytes.size() ? path : QString();
    }

    const QString path = dirPath + "/" + name + ".jpg";
    const QImage scaled = std::max(image.width(), image.height()) > ARTWORK_MAX_SIDE
                          ? image.scaled(ARTWORK_MAX_SIDE, ARTWORK_MAX_SIDE, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                          : image;
    return scaled.convertToFormat(QImage::Format_RGB888).save(path, "JPG", 90) ? path : QString();
}
}

MetadataModel::MetadataModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariant MetadataModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const int row = index.row();
    if (row < 0 || row >= rowCount()) {
        return {};
    }

    if (row < int(kStdTags.size())) {
        // Standard tags
        if (role == RoleTag) {
            return QString::fromStdString(project::LABEL_MAP[kStdTags[row]]);
        }
        if (role == RoleValue) {
            return QString::fromStdString(m_meta.*(project::kStdMembers[row]));
        }

        return {};
    }

    // Additional tags
    const int extra = row - int(kStdTags.size());
    const QString& key = m_additionalKeys[extra];
    if (role == RoleTag) {
        return key;
    }
    if (role == RoleValue) {
        return m_meta.additionalTags.value(key);
    }

    return {};
}

int MetadataModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return int(kStdTags.size()) + m_additionalKeys.size();
}

QHash<int, QByteArray> MetadataModel::roleNames() const
{
    static const QHash<int, QByteArray> roles {
        { RoleTag, "tag" },
        { RoleValue, "value" },
    };

    return roles;
}

void MetadataModel::load()
{
    project::ProjectMeta projectMeta = metadata()->tags();
    std::string defaultTemplate = exportConfiguration()->defaultMetadata();

    beginResetModel();

    project::ProjectMeta toLoad;
    if (isMetadataEmpty(projectMeta) && !defaultTemplate.empty()) {
        toLoad = parseXml(QString::fromStdString(defaultTemplate));
    } else {
        toLoad = metadata()->tags();
    }

    toLoad.artworkPath = projectMeta.artworkPath;

    m_meta = std::move(toLoad);
    m_additionalKeys = m_meta.additionalTags.keys();

    endResetModel();

    emit artworkChanged();
}

QUrl MetadataModel::artworkUrl() const
{
    if (m_meta.artworkPath.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(m_meta.artworkPath));
}

void MetadataModel::chooseArtwork()
{
    std::vector<std::string> filter {
        muse::trc("metadata", "Images") + " (*.jpg *.jpeg *.png *.webp *.heic *.tif *.tiff *.bmp)"
    };

    const muse::io::path_t filePath = interactive()->selectOpeningFileSync(muse::trc("metadata", "Choose artwork"),
                                                                           projectConfiguration()->userProjectsPath(), filter);
    if (filePath.empty()) {
        return;
    }

    const QString stored = storeArtwork(filePath.toQString());
    if (stored.isEmpty()) {
        interactive()->errorSync(muse::trc("metadata", "Choose artwork"),
                                 muse::trc("metadata", "This picture could not be read. Please try a JPG or PNG file."));
        return;
    }

    m_meta.artworkPath = stored.toStdString();
    emit artworkChanged();
}

void MetadataModel::removeArtwork()
{
    m_meta.artworkPath.clear();
    emit artworkChanged();
}

void MetadataModel::apply()
{
    metadata()->setTags(m_meta);
    configuration()->applySettings();
}

bool MetadataModel::isStandardTag(const int index)
{
    if (index < 0 || index >= rowCount()) {
        return false;
    }

    // first 9 tags are always standard tags
    // NOTE: this still works if you reorder tags in the UI
    // but use sourceRowIndex
    if (index < int(kStdTags.size())) {
        return true;
    }

    return false;
}

void MetadataModel::loadTemplate()
{
    std::vector<std::string> filter { muse::trc("metadata", "XML files") + " (*.xml)" };

    muse::io::path_t defaultDir = projectConfiguration()->lastOpenedProjectsPath();

    if (defaultDir.empty()) {
        defaultDir = projectConfiguration()->userProjectsPath();
    }

    if (defaultDir.empty()) {
        defaultDir = projectConfiguration()->defaultUserProjectsPath();
    }

    muse::io::path_t filePath = interactive()->selectOpeningFileSync(muse::trc("metadata", "Save"), defaultDir, filter);

    QFile f(filePath.toQString());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    project::ProjectMeta loaded = parseXml(QString::fromUtf8(f.readAll()));

    beginResetModel();
    m_meta = std::move(loaded);
    m_additionalKeys = m_meta.additionalTags.keys();
    endResetModel();
}

void MetadataModel::saveTemplate()
{
    std::vector<std::string> filter { muse::trc("metadata", "XML files") + " (*.xml)" };

    muse::io::path_t defaultDir = projectConfiguration()->lastOpenedProjectsPath();

    if (defaultDir.empty()) {
        defaultDir = projectConfiguration()->userProjectsPath();
    }

    if (defaultDir.empty()) {
        defaultDir = projectConfiguration()->defaultUserProjectsPath();
    }

    muse::io::path_t filePath = interactive()->selectSavingFileSync(muse::trc("metadata", "Save"), defaultDir, filter);

    QFile f(filePath.toQString());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        interactive()->errorSync(muse::trc("metadata", "Error saving template"),
                                 muse::trc("metadata", "Unable to save metadata template into given file."));
        return;
    }

    const QString xml = buildXml();
    const QByteArray utf8 = xml.toUtf8();
    f.write(utf8);
    f.close();
}

void MetadataModel::setAsDefault()
{
    const QString xml = buildXml();
    exportConfiguration()->setDefaultMetadata(xml.toStdString());
}

void MetadataModel::addTag()
{
    const int insertRow = rowCount();

    beginInsertRows(QModelIndex(), insertRow, insertRow);

    QString key = "";

    m_meta.additionalTags.insert(key, QString());
    m_additionalKeys.append(key);

    endInsertRows();
}

void MetadataModel::deleteTag(const int index)
{
    const int standardCount = static_cast<int>(project::kStdMembers.size());
    if (index < standardCount || index >= rowCount()) {
        return;
    }

    beginRemoveRows(QModelIndex(), index, index);

    const QString key = m_additionalKeys.takeAt(index - standardCount);
    m_meta.additionalTags.remove(key);

    endRemoveRows();
}

void MetadataModel::renameTag(int row, const QString& newTag)
{
    // disallow renaming standard tag
    const int offset = static_cast<int>(project::kStdMembers.size());
    if (row < offset || row >= rowCount()) {
        return;
    }

    const QString trimmed = newTag.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    // disallow renaming to any standard tag name
    if (au::project::isStandardTag(trimmed)) {
        return;
    }

    const int keyIndex = row - offset;
    if (keyIndex < 0 || keyIndex >= m_additionalKeys.size()) {
        return;
    }

    const QString oldKey = m_additionalKeys.at(keyIndex);
    if (oldKey == trimmed) {
        return;
    }

    const QVariant value = m_meta.additionalTags.value(oldKey);
    m_meta.additionalTags.remove(oldKey);
    m_meta.additionalTags.insert(trimmed, value);

    m_additionalKeys[keyIndex] = trimmed;

    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { RoleTag, RoleValue });
}

void MetadataModel::setTagValue(int row, const QString& value)
{
    const int offset = static_cast<int>(project::kStdMembers.size());
    if (row < 0 || row >= rowCount()) {
        return;
    }

    if (row < offset) {
        const auto member = project::kStdMembers[row];
        if (m_meta.*member == value) {
            return;
        }
        m_meta.*member = value.toStdString();

        const QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { RoleValue });
        return;
    }

    const int keyIndex = row - offset;
    if (keyIndex < 0 || keyIndex >= m_additionalKeys.size()) {
        return;
    }

    const QString& key = m_additionalKeys.at(keyIndex);
    const QString old = m_meta.additionalTags.value(key).toString();
    if (old == value) {
        return;
    }

    m_meta.additionalTags.insert(key, value);
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { RoleValue });
}

QString MetadataModel::buildXml() const
{
    std::string data = metadata()->buildXml(m_meta);

    return QString::fromStdString(data);
}

au::project::ProjectMeta MetadataModel::parseXml(const QString& xml) const
{
    project::ProjectMeta meta = metadata()->parseXml(xml.toStdString());

    // keep these unchanged
    meta.filePath = m_meta.filePath;
    meta.thumbnail = m_meta.thumbnail;

    return meta;
}

bool MetadataModel::isMetadataEmpty(const au::project::ProjectMeta& meta) const
{
    for (size_t i = 0; i < project::kStdMembers.size(); ++i) {
        const QString& value = QString::fromStdString(meta.*(project::kStdMembers[i]));
        if (!value.trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}
