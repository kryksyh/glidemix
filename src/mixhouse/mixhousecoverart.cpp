/*
 * MixHouse DJ 2
 */
#include "mixhousecoverart.h"
#include "mixhouse/mixhousenativepath.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QThread>

#include <taglib/tfile.h>
#include <taglib/fileref.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/xiphcomment.h>
#include <taglib/vorbisfile.h>
#include <taglib/opusfile.h>
#include <taglib/oggflacfile.h>
#include <taglib/wavfile.h>
#include <taglib/aifffile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>
#include <taglib/mp4coverart.h>

namespace au::mixhouse {

namespace {

// This module's CMake build concatenates every .cpp in src/mixhouse/ into one
// unity translation unit -- anonymous-namespace/static helper names have
// collided across files here before, so every helper below gets a
// mixhouseCoverArt-prefixed name distinctive to this file.

/// Decodes one embedded picture's raw compressed bytes into a QImage. Real
/// mixxx (trackmetadata_xiph.cpp) passes the picture's own MIME-type string as
/// QImage::fromData's format hint; that string ("image/jpeg") isn't one of
/// Qt's plugin-key format names ("JPG"/"PNG"...), so this instead leaves
/// `format` null and lets QImage::fromData sniff the real magic bytes --
/// equally correct for JPEG/PNG and more robust than trusting a possibly
/// wrong or absent mime-type field in the tag itself.
QImage mixhouseCoverArtDecode(const TagLib::ByteVector& data)
{
    if (data.isEmpty()) {
        return QImage();
    }
    return QImage::fromData(reinterpret_cast<const uchar*>(data.data()),
                             static_cast<int>(data.size()));
}

/// Mixxx's real kPreferredPictureTypes order (trackmetadata_id3v2.cpp /
/// trackmetadata_xiph.cpp): front cover first, then an image of the media
/// itself, then any illustration, falling back to whatever "Other"-typed
/// picture exists rather than showing nothing.
template<typename PictureType>
QImage mixhouseCoverArtPickPreferred(const TagLib::List<PictureType*>& pictures)
{
    const typename PictureType::Type kPreferredOrder[] = {
        PictureType::FrontCover, PictureType::Media, PictureType::Illustration, PictureType::Other
    };
    for (const auto preferredType : kPreferredOrder) {
        for (PictureType* picture : pictures) {
            if (!picture || picture->type() != preferredType) {
                continue;
            }
            QImage image = mixhouseCoverArtDecode(picture->data());
            if (!image.isNull()) {
                return image;
            }
        }
    }
    return QImage();
}

/// ID3v2 APIC extraction (mp3, wav, aiff) -- trackmetadata_id3v2.cpp.
QImage mixhouseCoverArtFromId3v2(TagLib::ID3v2::Tag& tag)
{
    const auto& frameListMap = tag.frameListMap();
    const auto iterApic = frameListMap.find("APIC");
    if (iterApic == frameListMap.end()) {
        return QImage();
    }

    using AttachedPictureFrame = TagLib::ID3v2::AttachedPictureFrame;
    const AttachedPictureFrame::Type kPreferredOrder[] = {
        AttachedPictureFrame::FrontCover, AttachedPictureFrame::Media,
        AttachedPictureFrame::Illustration, AttachedPictureFrame::Other
    };
    for (const auto preferredType : kPreferredOrder) {
        for (const TagLib::ID3v2::Frame* frame : iterApic->second) {
            const auto* apicFrame = dynamic_cast<const AttachedPictureFrame*>(frame);
            if (!apicFrame || apicFrame->type() != preferredType) {
                continue;
            }
            QImage image = mixhouseCoverArtDecode(apicFrame->picture());
            if (!image.isNull()) {
                return image;
            }
        }
    }
    return QImage();
}

/// MP4 'covr' atom extraction -- trackmetadata_mp4.cpp.
QImage mixhouseCoverArtFromMp4(TagLib::MP4::Tag& tag)
{
    if (!tag.contains("covr")) {
        return QImage();
    }
    const TagLib::MP4::CoverArtList coverArtList = tag.item("covr").toCoverArtList();
    for (const auto& coverArt : coverArtList) {
        QImage image = mixhouseCoverArtDecode(coverArt.data());
        if (!image.isNull()) {
            return image;
        }
    }
    return QImage();
}

} // namespace

QImage extractEmbeddedCoverArt(const QString& filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        return QImage();
    }

    TagLib::FileRef fileRef = au::mixhouse::openTagFile(filePath);
    TagLib::File* file = fileRef.file();
    if (!file || !file->isValid()) {
        return QImage();
    }

    // MP3 -- ID3v2 APIC.
    if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) {
        if (auto* id3v2 = mpeg->ID3v2Tag()) {
            QImage image = mixhouseCoverArtFromId3v2(*id3v2);
            if (!image.isNull()) {
                return image;
            }
        }
        return QImage();
    }

    // FLAC -- Xiph (VorbisComment) picture comment preferred, native FLAC
    // picture-block list as fallback, matching metadatasourcetaglib.cpp.
    if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
        if (auto* xiph = flac->xiphComment()) {
            QImage image = mixhouseCoverArtPickPreferred(xiph->pictureList());
            if (!image.isNull()) {
                return image;
            }
        }
        return mixhouseCoverArtPickPreferred(flac->pictureList());
    }

    // MP4/M4A -- 'covr' atom.
    if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        if (auto* tag = mp4->tag()) {
            return mixhouseCoverArtFromMp4(*tag);
        }
        return QImage();
    }

    // OGG Vorbis, OGG-FLAC, Opus -- Xiph picture comment.
    if (auto* vorbis = dynamic_cast<TagLib::Ogg::Vorbis::File*>(file)) {
        if (auto* xiph = vorbis->tag()) {
            return mixhouseCoverArtPickPreferred(xiph->pictureList());
        }
        return QImage();
    }
    if (auto* oggFlac = dynamic_cast<TagLib::Ogg::FLAC::File*>(file)) {
        if (auto* xiph = oggFlac->tag()) {
            return mixhouseCoverArtPickPreferred(xiph->pictureList());
        }
        return QImage();
    }
    if (auto* opus = dynamic_cast<TagLib::Ogg::Opus::File*>(file)) {
        if (auto* xiph = opus->tag()) {
            return mixhouseCoverArtPickPreferred(xiph->pictureList());
        }
        return QImage();
    }

    // WAV / AIFF -- ID3v2 chunk.
    if (auto* wav = dynamic_cast<TagLib::RIFF::WAV::File*>(file)) {
        if (auto* id3v2 = wav->ID3v2Tag()) {
            return mixhouseCoverArtFromId3v2(*id3v2);
        }
        return QImage();
    }
    if (auto* aiff = dynamic_cast<TagLib::RIFF::AIFF::File*>(file)) {
        if (auto* id3v2 = aiff->tag()) {
            return mixhouseCoverArtFromId3v2(*id3v2);
        }
        return QImage();
    }

    return QImage();
}

MixHouseCoverArtCache::MixHouseCoverArtCache(std::shared_ptr<IMixHouseLibrary> library)
    : m_library(std::move(library))
{
}

QImage MixHouseCoverArtCache::image(int trackId)
{
    auto it = m_cache.find(trackId);
    if (it != m_cache.end()) {
        return it.value();
    }

    QImage result;
    IMixHouseLibrary::CoverInfo info;
    if (m_library && m_library->coverInfoForTrack(trackId, info)) {
        // CoverInfoRelative::Type: 0=NONE, 1=METADATA, 2=FILE.
        if (info.type == 1) {
            result = extractEmbeddedCoverArt(info.filePath);
        } else if (info.type == 2 && !info.location.isEmpty()) {
            // A standalone sidecar image, relative to the track file's own
            // directory -- mirrors CoverInfo::loadImage()'s FILE branch.
            const QFileInfo trackFile(info.filePath);
            const QString sidecarPath = trackFile.absoluteDir().filePath(info.location);
            result.load(sidecarPath);
        } else if (info.type == 0 && !info.filePath.isEmpty()) {
            // GlideMix: the scanner never records artwork, so every track says
            // "none". Look anyway. Found or not, the answer is cached.
            result = imageForFile(info.filePath);
        }
    }

    m_cache.insert(trackId, result);
    return result;
}

QImage MixHouseCoverArtCache::imageForFile(const QString& filePath)
{
    if (const auto it = m_fileCache.constFind(filePath); it != m_fileCache.constEnd()) {
        return it.value();
    }
    QImage result = extractEmbeddedCoverArt(filePath);
    if (result.isNull()) {
        const QDir dir = QFileInfo(filePath).absoluteDir();
        for (const char* name : { "cover.jpg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "Cover.jpg",
                                  "Folder.jpg" }) {
            if (dir.exists(QString::fromLatin1(name)) && result.load(dir.filePath(QString::fromLatin1(name)))) {
                break;
            }
        }
    }
    m_fileCache.insert(filePath, result);
    return result;
}

void MixHouseCoverArtCache::invalidate(int trackId)
{
    m_cache.remove(trackId);
}

MixHouseCoverArtImageProvider::MixHouseCoverArtImageProvider(std::shared_ptr<IMixHouseLibrary> library)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_cache(std::move(library))
{
}

QImage MixHouseCoverArtImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // See the header doc comment: this must never run off the GUI thread --
    // m_library's SQL connection isn't safe from anywhere else. Fail safe
    // (null image, loud warning) rather than touch the database and risk the
    // exact memory corruption that crashed the app four times before this was
    // fixed by keeping the QML side synchronous.
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        qWarning() << "MixHouseCoverArtImageProvider::requestImage called off the main "
                      "thread for id" << id << "-- refusing to touch the database. "
                      "Check for asynchronous: true on a cover-art Image element.";
        if (size) {
            *size = QSize();
        }
        return QImage();
    }

    // "file/<percent-encoded path>": a track from rekordbox or Lexicon, which
    // is not in the library and so has no library id.
    QImage image;
    if (id.startsWith(QStringLiteral("file/"))) {
        image = m_cache.imageForFile(QUrl::fromPercentEncoding(id.mid(5).toUtf8()));
    } else {
        bool ok = false;
        const int trackId = id.toInt(&ok);
        image = ok ? m_cache.image(trackId) : QImage();
    }

    if (size) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty()) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

}
