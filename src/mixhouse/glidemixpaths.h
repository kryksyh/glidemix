/*
 * GlideMix
 */
#pragma once

#include <QDir>
#include <QString>

//! Where GlideMix and the apps it reads from keep their files, per platform.
//! Header-only, so any module can use it without linking mixhouse.
//!
//! macOS: exactly the paths the app has always used (~/Library/...).
//! Windows: the same layout under %APPDATA% (roaming) or, for Mixxx,
//! %LOCALAPPDATA%, which is where those apps put their data there.
namespace au::mixhouse::paths {
#ifdef Q_OS_WIN
namespace detail {
inline QString windowsFolder(const char* variable, const char* fallbackUnderHome)
{
    const QString value = qEnvironmentVariable(variable);
    if (!value.isEmpty()) {
        return QDir::fromNativeSeparators(value);
    }
    return QDir::homePath() + QLatin1Char('/') + QLatin1String(fallbackUnderHome);
}
}
#endif

//! The per-user folder applications keep their data in:
//! ~/Library/Application Support on macOS, %APPDATA% on Windows.
inline QString userAppDataRoot()
{
#ifdef Q_OS_WIN
    return detail::windowsFolder("APPDATA", "AppData/Roaming");
#else
    return QDir::homePath() + QStringLiteral("/Library/Application Support");
#endif
}

//! GlideMix's own data folder (library, waveforms, stems, helpers, presets):
//! ~/Library/Application Support/Glidemix on macOS, %APPDATA%\Glidemix on Windows.
inline QString glidemixDataDir()
{
    return userAppDataRoot() + QStringLiteral("/Glidemix");
}

//! A file or folder inside glidemixDataDir(); `relative` has no leading slash.
inline QString glidemixDataPath(const QString& relative)
{
    return glidemixDataDir() + QLatin1Char('/') + relative;
}

//! Mixxx's settings folder (mixxxdb.sqlite, mixxx.cfg, effects.xml):
//! its sandbox container on macOS, %LOCALAPPDATA%\Mixxx on Windows.
inline QString mixxxSettingsDir()
{
#ifdef Q_OS_WIN
    return detail::windowsFolder("LOCALAPPDATA", "AppData/Local") + QStringLiteral("/Mixxx");
#else
    return QDir::homePath() + QStringLiteral("/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx");
#endif
}

//! rekordbox's analysis folder (ANLZ files):
//! ~/Library/Pioneer/rekordbox/share/PIONEER/USBANLZ on macOS,
//! %APPDATA%\Pioneer\rekordbox\share\PIONEER\USBANLZ on Windows.
inline QString rekordboxAnalysisDir()
{
#ifdef Q_OS_WIN
    return userAppDataRoot() + QStringLiteral("/Pioneer/rekordbox/share/PIONEER/USBANLZ");
#else
    return QDir::homePath() + QStringLiteral("/Library/Pioneer/rekordbox/share/PIONEER/USBANLZ");
#endif
}
}
