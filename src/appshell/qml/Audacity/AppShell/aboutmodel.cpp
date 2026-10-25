/*
* Audacity: A Digital Audio Editor
*/
#include "aboutmodel.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>

#include "framework/global/translation.h"
#include "framework/global/log.h"

using namespace muse;
using namespace au::appshell;

namespace {
struct CreditEntry {
    const char* name;
    const char* role;
};

struct ThanksEntry {
    const char* name;
};

constexpr CreditEntry sTeamMembers[] =  {
    { "dozzzzer", "quality assurance" },
    { "Igor Korsukov", "developer" },
    { "Jessica Williamson", "designer" },
    { "Paul Martin", "audio developer, @luapmartin" },
    { "Alexander Dawson", "designer" },
    { "Gabriel Sartori", "developer" },
    { "Yana Larina", "project manager" },
    { "Matthieu Hodgkinson", "audio developer" },
    { "Dmitry Makarenko", "developer" },
    { "Elnur Ismailzada", "developer" },
    { "Johan Althoff", "designer" },
    { "Martin Keary", nullptr },
    { "Grzegorz Wojciechowski", "developer" },
    { "Antons \xc4\x8cinakovs", "quality assurance" }
};

constexpr CreditEntry sEmeritusTeam[] = {
    { "Gale Andrews", "quality assurance" },
    { "Richard Ash", "developer" },
    { "Christian Brochec", "documentation and support, French" },
    { "Matt Brubeck", "developer" },
    { "Arturo \"Buanzo\" Busleiman", "system administration" },
    { "Michael Chinen", "developer" },
    { "James Crook", "developer" },
    { "Roger Dannenberg", "co-founder and developer" },
    { "Steve Daulton", nullptr },
    { "Al Dimond", "developer" },
    { "Benjamin Drung", "developer" },
    { "Joshua Haberman", "developer" },
    { "Ruslan Ijbulatov", "developer" },
    { "Vaughan Johnson", "developer" },
    { "Greg Kozikowski", "documentation and support" },
    { "Paul Licameli", "developer" },
    { "Leland Lucius", "developer" },
    { "Dominic Mazzoni", "co-founder and developer" },
    { "Markus Meyer", "developer" },
    { "Monty Montgomery", "developer" },
    { "Shane Mueller", "developer" },
    { "Tony Oetzmann", "documentation and support" },
    { "Alexandre Prokoudine", "documentation and support" },
    { "Peter Sampson", "QA tester, documentation and support" },
    { "Martyn Shaw", "developer" },
    { "Dmitry Vedenko", "developer" },
    { "Bill Wharrie", "documentation and support" },
    { "Casper Jeukendrup", "developer" }
};

constexpr CreditEntry sContributors[] = {
    { "Leo Wattenberg", "designer" },
    { "Peter Jonas", "developer" },
    { "Lynn Allan", "developer" },
    { "Brian Armstrong", "developer" },
    { "David Avery", "developer" },
    { "David Bailes", "accessibility advisor" },
    { "Brian Beard (Kurtsley)", "developer" },
    { "William Bland", "developer" },
    { "Sami Boukortt", "developer" },
    { "Jeremy R. Brown", "developer" },
    { "Alex S. Brown", "developer" },
    { "David Bryant", "developer" },
    { "Chris Cannam", "developer" },
    { "Subhradeep Chakraborty", "developer" },
    { "Cory Cook", "developer" },
    { "Craig DeForest", "developer" },
    { "Edgar Franke (Edgar-RFT)", "developer" },
    { "Anton Gerasimov", "developer" },
    { "Mitch Golden", "developer" },
    { "Brian Gunlogson", "developer" },
    { "Gonzalo Guzm\xc3\xa1n", "documentation and support" },
    { "Andrew Hallendorff", "developer" },
    { "Robert H\xc3\xa4nggi", "developer" },
    { "Jouni Helminen", "designer" },
    { "Daniel Horgan", "developer" },
    { "David Hostetler", "developer" },
    { "Edward Hui", "developer" },
    { "Vladislav Isaev", "effects presets" },
    { "Marek Iwaszkiewicz", "effects presets" },
    { "Steve Jolly", "developer" },
    { "Steven Jones", "developer" },
    { "Henric Jungheim", "developer" },
    { "Myungchul Keum", "developer" },
    { "Arun Kishore", "developer" },
    { "Paul Livesey", "developer" },
    { "Harvey Lubin", "graphic artist" },
    { "Max Maisel", "developer" },
    { "Pietro Marcello", "developer" },
    { "Greg Mekkes", "developer" },
    { "Abe Milde", "developer" },
    { "Ryan Miller", "tester" },
    { "Paul Nasca", "developer" },
    { "Clayton Otey", "developer" },
    { "Pavel Penikov", "tester" },
    { "Mark Phillips", "developer" },
    { "Andr\xc3\xa9 Pinto", "developer" },
    { "Pokechu22", "developer" },
    { "Jean Claude Risset", "composer" },
    { "RuRo", "developer" },
    { "Augustus Saunders", "developer" },
    { "Benjamin Schwartz", "developer" },
    { "Cliff Scott", "tester" },
    { "David R. Sky", "Nyquist plug-ins" },
    { "Joe Souza", "developer" },
    { "K. Soze", "developer" },
    { "Rob Sykes", "developer" },
    { "Mike Underwood", "developer" },
    { "Philip Van Baren", "developer" },
    { "Salvo Ventura", "developer" },
    { "Darrell Walisser", "developer" },
    { "Jun Wan", "developer" },
    { "Daniel Winzen", "developer" },
    { "Tom Woodhams", "developer" },
    { "Mark Young", "developer" },
    { "Wing Yu", "developer" },
};

constexpr CreditEntry sGraphics[] = {
    { "Shinta Carolinasari", "web developer" },
    { "Bayu Rizaldhan Rayes", "graphics" },
};

constexpr ThanksEntry sThanks[] = {
    { "Dave Beydler" },
    { "Brian Cameron" },
    { "Jason Cohen" },
    { "Dave Fancella" },
    { "Steve Harris" },
    { "Daniel James" },
    { "Daniil Kolpakov" },
    { "Robert Leidle" },
    { "Logan Lewis" },
    { "David Luff" },
    { "Jason Pepas" },
    { "Jonathan Ryshpan" },
    { "Michael Schwendt" },
    { "Patrick Shirkey" },
    { "Tuomas Suutari" },
    { "Mark Tomlinson" },
    { "David Topper" },
    { "Rudy Trubitt" },
    { "StreetIQ.com" },
    { "UmixIt Technologies, LLC" },
    { "Verilogix, Inc." },
};

QVariantMap makeCredit(const CreditEntry& e)
{
    QVariantMap m;
    m["name"] = QString::fromUtf8(e.name);
    m["role"] = e.role ? QString::fromUtf8(e.role) : QString();
    m["url"]  = QString();
    return m;
}

QVariantMap makeCredit(const ThanksEntry& e)
{
    QVariantMap m;
    m["name"] = QString::fromUtf8(e.name);
    m["role"] = QString();
    m["url"]  = QString();
    return m;
}

template<typename ArrayTypes>
QVariantList buildCredits(const ArrayTypes& entries)
{
    QVariantList list;
    for (const auto& e : entries) {
        list.append(makeCredit(e));
    }
    return list;
}

QVariantMap makeSection(const QString& title, const QVariantList& credits)
{
    QVariantMap s;
    s["title"]   = title;
    s["credits"] = credits;
    return s;
}

QVariantMap makeRawSection(const QString& title, const QString& rawCredit)
{
    QVariantMap rawCreditMap;
    rawCreditMap["raw"] = rawCredit;
    return makeSection(title, QVariantList { rawCreditMap });
}

const QString ABOUT_RESOURCES = QStringLiteral(":/resources/about/");
const QString APP_LICENSE_FILE = QStringLiteral("gpl-30.html");
}

AboutModel::AboutModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString AboutModel::appName() const
{
    return QStringLiteral("GlideMix");
}

QString AboutModel::appVersion() const
{
    // GlideMix: "Alpha v1.0.0 Early Preview", and "(dev build)" on builds
    // made from the working tree rather than a release.
    const muse::Version v = application()->version();
    const QString number = QStringLiteral("%1.%2.%3").arg(v.major()).arg(v.minor()).arg(v.patch());
    const QString text = muse::qtrc("appshell/about", "Beta v%1").arg(number);
    return application()->unstable() ? muse::qtrc("appshell/about", "%1 (dev build)").arg(text) : text;
}

QString AboutModel::appRevision() const
{
    return QString::fromStdString(configuration()->appRevision());
}

void AboutModel::copyRevisionToClipboard() const
{
    QApplication::clipboard()->setText(
        QString("OS: %1, Arch.: %2, %3 version (%4-bit): %5-%6, revision: %7")
        .arg(QSysInfo::prettyProductName()
             + ((QSysInfo::productType() == "windows" && (QSysInfo::productVersion() == "10" || QSysInfo::productVersion() == "11"))
                ? " or later" : ""))
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(appName())
        .arg(QSysInfo::WordSize)
        .arg(application()->fullVersion().toString())
        .arg(application()->build())
        .arg(application()->revision()));
}

void AboutModel::toggleDevMode()
{
    globalConfiguration()->setDevModeEnabled(!globalConfiguration()->devModeEnabled());
}

QString AboutModel::readResource(const QString& path) const
{
    auto result = fileSystem()->readFile(muse::io::path_t(path));
    if (!result.ret) {
        LOGE() << "About: missing resource " << path;
        return QString {};
    }
    return QString::fromUtf8(result.val.constChar(), static_cast<qsizetype>(result.val.size()));
}

QVariantList AboutModel::creditList() const
{
    static const QVariantList cached = [this] {
        const QJsonArray components = QJsonDocument::fromJson(readResource(ABOUT_RESOURCES + "components.json").toUtf8()).array();

        auto componentsIn = [&components](const QString& group) {
            QVariantList credits;
            for (const QJsonValue& v : components) {
                const QJsonObject c = v.toObject();
                if (c.value("group").toString() == group) {
                    QVariantMap m;
                    m["name"] = c.value("name").toString();
                    m["url"] = c.value("url").toString();
                    m["role"] = c.value("license").toString();
                    credits.append(m);
                }
            }
            return credits;
        };

        QVariantList creditList;

        //! NOTE Titles are English on purpose: they name other projects' teams.
        QVariantMap builtOn = makeSection(muse::qtrc("appshell/about", "Built on"), componentsIn("Built on"));
        builtOn["subtitle"] = muse::qtrc("appshell/about",
                                         "GlideMix is a fork that stands on the work of these free software projects. "
                                         "Thank you to everyone who built them.");
        creditList.append(builtOn);

        creditList.append(makeSection(QStringLiteral("Audacity team members"), buildCredits(sTeamMembers)));
        QVariantMap emeritus = makeSection(QStringLiteral("Audacity emeritus"), buildCredits(sEmeritusTeam));
        emeritus["subtitle"] = QStringLiteral("Distinguished Audacity Team members, not currently active");
        creditList.append(emeritus);
        creditList.append(makeSection(QStringLiteral("Audacity contributors"), buildCredits(sContributors)));
        creditList.append(makeSection(QStringLiteral("Audacity website and graphics"), buildCredits(sGraphics)));
        creditList.append(makeSection(QStringLiteral("Audacity special thanks"), buildCredits(sThanks)));

        // GlideMix: the DJ effects come from Mixxx; one line instead of its
        // ~400 contributor names (Mixxx stays under "Built on" with its licence).
        creditList.append(makeRawSection(QStringLiteral("Mixxx"),
                                         QStringLiteral("The built-in DJ effects come from Mixxx. Thank you to the Mixxx Development Team "
                                                        "and all its contributors: <a href=\"https://mixxx.org\">mixxx.org</a>")));

        /* i18n-hint: Replace this with the names of the translators for your language.
            This will appear in the Translators section of the About dialog.
            For example: "French translation by Jane Doe<br>John Smith"
            Leave untranslated to hide the Translators section. */
        QString translationCredits = muse::qtrc("appshell/about", "translator_credits");
        if (translationCredits != "translator_credits") {
            creditList.append(makeRawSection(muse::qtrc("appshell/about", "Translators"), translationCredits));
        }

        //! GlideMix reads these apps' libraries (read-only); none of their code is included.
        QVariantMap worksWith = makeRawSection(muse::qtrc("appshell/about", "Works with"),
                                               QStringLiteral("rekordbox (AlphaTheta / Pioneer DJ)<br>Lexicon DJ"));
        worksWith["subtitle"] = muse::qtrc("appshell/about",
                                           "GlideMix reads your rekordbox XML and Lexicon libraries: grids, keys, cues, "
                                           "phrases and playlists. rekordbox is a trademark of AlphaTheta Corporation; "
                                           "Lexicon is a product of Lexicon DJ. GlideMix is not affiliated with or "
                                           "endorsed by them.");
        creditList.append(worksWith);

        const std::pair<const char*, const char*> libraryGroups[] = {
            { "DJ analysis", QT_TRANSLATE_NOOP("appshell/about", "Libraries: DJ analysis") },
            { "Audio engine and file formats", QT_TRANSLATE_NOOP("appshell/about", "Libraries: audio engine and file formats") },
            { "Application framework", QT_TRANSLATE_NOOP("appshell/about", "Libraries: application framework") },
        };
        for (const auto& [group, title] : libraryGroups) {
            creditList.append(makeSection(muse::qtrc("appshell/about", title), componentsIn(QString::fromUtf8(group))));
        }
        return creditList;
    }();
    return cached;
}

QVariantList AboutModel::legalList() const
{
    static const QVariantList cached = [this] {
        QVariantMap app;
        app["name"] = appName();
        app["license"] = QStringLiteral("GPL-3.0");
        app["copyright"] = muse::qtrc("appshell/about", "Free software: you can redistribute it and/or modify it under the terms of the GNU General Public License version 3.");
        app["url"] = QString();
        app["files"] = QStringList { APP_LICENSE_FILE };

        QVariantList list { app };
        const QJsonArray components = QJsonDocument::fromJson(readResource(ABOUT_RESOURCES + "components.json").toUtf8()).array();
        for (const QJsonValue& v : components) {
            const QJsonObject c = v.toObject();
            QVariantMap m;
            m["name"] = c.value("name").toString();
            m["license"] = c.value("license").toString();
            m["copyright"] = c.value("copyright").toString();
            m["url"] = c.value("url").toString();
            QStringList files;
            for (const QJsonValue& f : c.value("licenseFiles").toArray()) {
                files << f.toString();
            }
            m["files"] = files;
            list.append(m);
        }
        return list;
    }();
    return cached;
}

QString AboutModel::licenseText(const QString& file) const
{
    if (file == APP_LICENSE_FILE) {
        return readResource(":/resources/" + APP_LICENSE_FILE);
    }
    if (file.contains('/') || file.contains("..")) {
        return QString {};
    }
    return readResource(ABOUT_RESOURCES + "licenses/" + file);
}
