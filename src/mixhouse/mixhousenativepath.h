/*
 * GlideMix
 */
#pragma once

// Opening files by name on Windows: libsndfile and TagLib take a char* path
// in the ANSI code page there, so a UTF-8 path with "·", "é" or any other
// character outside it is "not found". Their UTF-16 entry points take any
// name. Elsewhere UTF-8 is the native encoding.

#include <QString>
#include <QtGlobal>

#include <sndfile.h>
#include <taglib/fileref.h>

#ifdef Q_OS_WIN
extern "C" SNDFILE* sf_wchar_open(const wchar_t* wpath, int mode, SF_INFO* sfinfo);
#endif

namespace au::mixhouse {
inline SNDFILE* openSndfile(const QString& path, int mode, SF_INFO* info)
{
#ifdef Q_OS_WIN
    return sf_wchar_open(reinterpret_cast<const wchar_t*>(path.utf16()), mode, info);
#else
    return sf_open(path.toUtf8().constData(), mode, info);
#endif
}

/// TagLib::FileRef(path, ...) for any file name.
template<typename ... Args>
TagLib::FileRef openTagFile(const QString& path, Args... args)
{
#ifdef Q_OS_WIN
    return TagLib::FileRef(reinterpret_cast<const wchar_t*>(path.utf16()), args ...);
#else
    const QByteArray utf8 = path.toUtf8();
    return TagLib::FileRef(utf8.constData(), args ...);
#endif
}
}
