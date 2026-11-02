/*
 * GlideMix
 */
#include "mixxxeffectsimport.h"

#include <map>

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

#include "effects/effects_base/ieffectsconfiguration.h"
#include "effects/effects_base/internal/effectsutils.h"

#include "mixxxeffects.h"
#include "dsp/control/mixtapecontrols.h"

#include "global/settings.h"
#include "log.h"

using namespace au::effects;

namespace {
QString mixxxEffectsXmlPath()
{
    // Mixxx on macOS (sandboxed bundle), as MixHouseLibrary::mixxxDatabasePath.
    return QDir::homePath()
           + QStringLiteral("/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/effects.xml");
}

//! A value from Mixxx's mixxx.cfg ("[Group]" section, "Key value" lines).
QString mixxxConfigValue(const QString& group, const QString& key)
{
    QFile file(QDir::homePath()
               + QStringLiteral("/Library/Containers/org.mixxx.mixxx/Data/Library/Application Support/Mixxx/mixxx.cfg"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    bool inGroup = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inGroup = line == group;
        } else if (inGroup && line.startsWith(key + u' ')) {
            return line.mid(key.size() + 1).trimmed();
        }
    }
    return {};
}

//! Mixxx effect id -> GlideMix effect id.
std::map<QString, EffectId> mixxxToEffectIds()
{
    std::map<QString, EffectId> ids;
#define MIXTAPE_MIXXX_EFFECT(ClassName, MixxxId, Name, Group) \
    {                                                        \
        const ClassName effect;                              \
        ids[QStringLiteral(MixxxId)] = utils::effectId(&effect); \
    }
#include "mixxxeffectlist.h"
#undef MIXTAPE_MIXXX_EFFECT
    return ids;
}

//! The built-in effect ids under <VisibleEffects> or <HiddenEffects>, in order.
std::vector<QString> readEffectIds(const QString& path, const QString& listName)
{
    std::vector<QString> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    QXmlStreamReader xml(&file);
    bool inList = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == listName) {
            inList = true;
        } else if (xml.isEndElement() && xml.name() == listName) {
            inList = false;
        } else if (inList && xml.isStartElement() && xml.name() == QLatin1String("Id")) {
            result.push_back(xml.readElementText());
        }
    }
    return result;
}
}

void au::effects::importMixxxEffectListsOnce(IEffectsConfiguration& configuration)
{
    if (configuration.djEffectListsImported()) {
        return;
    }
    const QString path = mixxxEffectsXmlPath();
    if (QFile::exists(path)) {
        const auto ids = mixxxToEffectIds();
        auto convert = [&ids](const std::vector<QString>& mixxxIds) {
            std::vector<EffectId> result;
            for (const QString& mixxxId : mixxxIds) {
                const auto it = ids.find(mixxxId);
                if (it != ids.end()) {
                    result.push_back(it->second);   // AudioUnit and LV2 entries stay in their own menus
                }
            }
            return result;
        };
        const auto visible = convert(readEffectIds(path, QStringLiteral("VisibleEffects")));
        const auto hidden = convert(readEffectIds(path, QStringLiteral("HiddenEffects")));
        if (!visible.empty() || !hidden.empty()) {
            configuration.setDjEffectLists(visible, hidden);
        }
        LOGI() << "GlideMix: DJ Effects lists taken from Mixxx: " << visible.size() << " visible, "
               << hidden.size() << " hidden";
    }
    configuration.setDjEffectListsImported(true);
}

void au::effects::connectEqFrequencies(IEffectsConfiguration& configuration)
{
    // Taken over from Mixxx once, with its own flag (the effect lists may
    // have been imported before this existed).
    static const muse::Settings::Key imported("effects", "effects/djEqFrequenciesImported");
    if (!muse::settings()->value(imported).toBool()) {
        muse::settings()->setSharedValue(imported, muse::Val(true));
        // EQ shelves (DlgPrefMixer::slotUpdate: precise takes precedence).
        auto frequency = [](const char* key) {
            QString value = mixxxConfigValue(QStringLiteral("[Mixer Profile]"), QString::fromLatin1(key) + QStringLiteral("Precise"));
            if (value.isEmpty()) {
                value = mixxxConfigValue(QStringLiteral("[Mixer Profile]"), QString::fromLatin1(key));
            }
            return value.toDouble();
        };
        const double low = frequency("LoEQFrequency");
        const double high = frequency("HiEQFrequency");
        if (low > 0.0 && high > 0.0 && low != high) {
            configuration.setDjEqFrequencies(low, high);
        }
    }

    auto apply = [&configuration]() {
        mixtapecontrols::set("[Mixer Profile]", "LoEQFrequency", configuration.djLowEqFrequency());
        mixtapecontrols::set("[Mixer Profile]", "HiEQFrequency", configuration.djHighEqFrequency());
    };
    apply();
    configuration.djEqFrequenciesChanged().onNotify(nullptr, apply);
}
