/*
 * Audacity: A Digital Audio Editor
 */

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTextCodec>

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "appfactory.h"
#include "commandlineparser.h"
#include "log.h"

#include "framework/multiwindows/singleinstance.h"

#include "muse_framework_config.h"
#include "muse_app_version.h"

#if (defined (_MSCVER) || defined (_MSC_VER))
#include <vector>
#include <algorithm>
#include <windows.h>
#include <shellapi.h>
#endif


static void app_init_qrc()
{
    Q_INIT_RESOURCE(app);
}

#ifndef MU_BUILD_CRASHPAD_CLIENT
static void crashCallback(int signum)
{
    const char* signame = "UNKNOWN SIGNAME";
    const char* sigdescript = "";
    switch (signum) {
    case SIGILL:
        signame = "SIGILL";
        sigdescript = "Illegal Instruction";
        break;
    case SIGSEGV:
        signame = "SIGSEGV";
        sigdescript =  "Invalid memory reference";
        break;
    }
    LOGE() << "Oops! Application crashed with signal: [" << signum << "] " << signame << "-" << sigdescript;
    // GlideMix: exit() ran the static destructors on a broken process; they aborted
    // and the crash report showed that abort instead of the real crash. Let the
    // system handle the signal so the report shows where it happened.
    signal(signum, SIG_DFL);
    raise(signum);
}

#endif

#ifdef Q_OS_WIN
#include "winleaktracker.h"
#endif

int main(int argc, char** argv)
{
#ifndef MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT
    signal(SIGSEGV, crashCallback);
    signal(SIGILL, crashCallback);
    signal(SIGFPE, crashCallback);
#endif

    // ====================================================
    // Setup global Qt application variables
    // ====================================================
    app_init_qrc();

    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qputenv("QML_DISABLE_DISK_CACHE", "true");

    // HACK: Workaround for crash MuseScore #28840. This disables the incremental GC
    if (!qEnvironmentVariableIsSet("QV4_GC_TIMELIMIT")) {
        qputenv("QV4_GC_TIMELIMIT", "0");
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (!qEnvironmentVariableIsSet("QT_QUICK_FLICKABLE_WHEEL_DECELERATION")) {
        qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "5000");
    }
#endif

#ifdef Q_OS_LINUX
    if (qEnvironmentVariable("AU_QT_QPA_PLATFORM") != "offscreen") {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }

    //! NOTE Forced X11, with Wayland there are a number of problems now
    if (qEnvironmentVariable("AU_QT_QPA_PLATFORM") == "") {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // GlideMix: one name for dev and release builds, so both use the same
    // settings (Audacity keeps nightly and stable apart; we don't need to).
    // Internal name: the settings domain (com.glidemix.Glidemix) and data
    // folder (Application Support/Glidemix/Glidemix) are keyed by it. The
    // name people see is "GlideMix" (bundle name, titles, texts); keep this
    // one as it is, or everyone's settings move again.
    const char* appName = "Glidemix";

#ifdef Q_OS_WIN
    // NOTE: There are some problems with rendering the application window on some integrated graphics processors
    //       see https://github.com/musescore/MuseScore/issues/8270
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);

    if (!qEnvironmentVariableIsSet("QT_OPENGL_BUGLIST")) {
        qputenv("QT_OPENGL_BUGLIST", ":/resources/win_opengl_buglist.json");
    }
#endif
    QGuiApplication::styleHints()->setMousePressAndHoldInterval(250);

    QCoreApplication::setApplicationName(appName);
    QCoreApplication::setOrganizationName("Glidemix");
    QCoreApplication::setOrganizationDomain("glidemix.com");
    QCoreApplication::setApplicationVersion(MUSE_APP_VERSION);

#if !defined(Q_OS_WIN) && !defined(Q_OS_DARWIN) && !defined(Q_OS_WASM)
#ifndef MUSE_APP_INSTALL_SUFFIX
#define MUSE_APP_INSTALL_SUFFIX ""
#endif
    // Any OS that uses Freedesktop.org Desktop Entry Specification (e.g. Linux, BSD)
    QGuiApplication::setDesktopFileName("com.glidemix.GlideMix" MUSE_APP_INSTALL_SUFFIX ".desktop");
#endif

    // Force the 8-bit text encoding to UTF-8. This is the default encoding on all supported platforms except for MSVC under Windows, which
    // would otherwise default to the local ANSI code page and cause corruption of any non-ANSI Unicode characters in command-line arguments.
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

#if (defined (_MSCVER) || defined (_MSC_VER))
    // On MSVC under Windows, we need to manually retrieve the command-line arguments and convert them from UTF-16 to UTF-8.
    // This prevents data loss if there are any characters that wouldn't fit in the local ANSI code page.
    int argcUTF16 = 0;
    LPWSTR* argvUTF16 = CommandLineToArgvW(GetCommandLineW(), &argcUTF16);

    std::vector<QByteArray> argvUTF8Q;
    std::for_each(argvUTF16, argvUTF16 + argcUTF16, [&argvUTF8Q](const auto& arg) {
        argvUTF8Q.emplace_back(QString::fromUtf16(reinterpret_cast<const char16_t*>(arg), -1).toUtf8());
    });

    LocalFree(argvUTF16);

    std::vector<char*> argvUTF8;
    for (auto& arg : argvUTF8Q) {
        argvUTF8.push_back(arg.data());
    }

    // Don't use the arguments passed to main(), because they're in the local ANSI code page.
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    int argcFinal = argcUTF16;
    char** argvFinal = argvUTF8.data();
#else

    int argcFinal = argc;
    char** argvFinal = argv;

#endif

    // ====================================================
    // Parse command line options
    // ====================================================
    au::app::CommandLineParser commandLineParser;
    commandLineParser.init();
    commandLineParser.parse(argcFinal, argvFinal);

    if (commandLineParser.options()->app.version) {
        commandLineParser.printVersion();
        return 0;
    }

    if (commandLineParser.options()->app.longVersion) {
        commandLineParser.printLongVersion();
        return 0;
    }

#ifdef Q_OS_WIN
    if (commandLineParser.options()->app.memoryLeakReport) {
        au::app::winleaktracker::install();
    }
#endif

    // ====================================================
    // Create QApplication based on run mode
    // ====================================================
    QCoreApplication* qApplication = nullptr;

    if (commandLineParser.runMode() == muse::IApplication::RunMode::AudioPluginRegistration) {
        qApplication = new QCoreApplication(argcFinal, argvFinal);
    } else {
        if (!qEnvironmentVariableIsSet("AU_ALLOW_MULTIPLE_PROCESSES")) {
            QStringList forwardedArgs;
            forwardedArgs.reserve(argcFinal - 1);
            for (int i = 1; i < argcFinal; ++i) {
                forwardedArgs.append(QString::fromUtf8(argvFinal[i]));
            }
            if (muse::mi::activateExistingInstance(QString::fromLatin1(appName), forwardedArgs)) {
                LOGI() << "existing GlideMix instance activated";
                LOGI() << "exiting";
                return 0;
            }
        }

        QApplication* guiApp = new QApplication(argcFinal, argvFinal);
        guiApp->setQuitOnLastWindowClosed(false);
        qApplication = guiApp;
    }

    commandLineParser.processBuiltinArgs(*qApplication);

    // ====================================================
    // Create and run the application
    // ====================================================
    au::app::AppFactory factory;
    std::shared_ptr<muse::IApplication> app = factory.newApp(commandLineParser.options());

    app->setup();

    app->setupNewContext();

    int code = qApplication->exec();

    app->finish();

    delete qApplication;

    LOGI() << "Goodbye!! code: " << code;

    // GlideMix: everything is saved and shut down. Leave now, without running
    // the static destructors of every loaded library: plug-ins (TimeStop,
    // Hyperlift, The Drop -- JUCE timers and licence clocks) keep their own
    // threads running, and tearing down their statics under them crashed
    // GlideMix on quit (crash reports 2026-11-08 and 2026-11-09).
    std::fflush(nullptr);
    std::_Exit(code);
}
