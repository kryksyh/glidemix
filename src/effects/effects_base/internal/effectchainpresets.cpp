/*
 * GlideMix
 */
#include "effectchainpresets.h"

#include <algorithm>
#include <set>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "global/settings.h"

#include "au3-components/EffectAutomationParameters.h"
#include "au3-effects/Effect.h"
#include "au3-realtime-effects/RealtimeEffectState.h"

#include "au3wrap/internal/wxtypes_convert.h"

#include "mixhouse/glidemixpaths.h"

#include "log.h"

using namespace au::effects;
using namespace muse;

namespace {
const std::string moduleName("effects");
const Settings::Key CHAIN_PRESET_LIST(moduleName, "effects/chainPresetList");
const Settings::Key CHAIN_PRESETS_IMPORTED(moduleName, "effects/chainPresetsImported");

// Mixxx's BackendType strings, plus one for effects Mixxx can't load.
const std::string kBuiltIn = "Built-In";
const std::string kAudioUnit = "AudioUnit";
const std::string kMixtapeStudio = "GlideMix";

QString presetsDirectory()
{
    return au::mixhouse::paths::glidemixDataPath(QStringLiteral("Effects/chains"));
}

QString mixxxSettingsDirectory()
{
    // As MixHouseLibrary::mixxxDatabasePath (sandboxed bundle on macOS).
    return au::mixhouse::paths::mixxxSettingsDir();
}

std::optional<EffectChainPresets::Chain> readChain(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    EffectChainPresets::Chain chain;
    QXmlStreamReader xml(&file);
    EffectChainPresets::Effect* effect = nullptr;
    EffectChainPresets::Parameter* parameter = nullptr;
    bool sawChain = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("EffectChain")) {
                sawChain = true;
            } else if (name == QLatin1String("Effect")) {
                chain.effects.emplace_back();
                effect = &chain.effects.back();
                parameter = nullptr;
            } else if (name == QLatin1String("Parameter") && effect) {
                effect->parameters.emplace_back();
                parameter = &effect->parameters.back();
            } else if (name == QLatin1String("Parameters") || name == QLatin1String("Effects")) {
                // containers
            } else {
                const std::string text = xml.readElementText().toStdString();
                if (parameter) {
                    if (name == QLatin1String("Id")) { parameter->id = text; }
                    else if (name == QLatin1String("Value")) { parameter->value = text; }
                    else if (name == QLatin1String("LinkType")) { parameter->linkType = text; }
                    else if (name == QLatin1String("LinkInversion")) { parameter->linkInversion = text; }
                    else if (name == QLatin1String("Hidden")) { parameter->hidden = text; }
                } else if (effect) {
                    if (name == QLatin1String("Id")) { effect->id = text; }
                    else if (name == QLatin1String("BackendType")) { effect->backendType = text; }
                    else if (name == QLatin1String("MetaParameterValue")) { effect->metaParameterValue = text; }
                } else {
                    if (name == QLatin1String("Name")) { chain.name = text; }
                    else if (name == QLatin1String("MixMode")) { chain.mixMode = text; }
                    else if (name == QLatin1String("SuperParameterValue")) { chain.superParameterValue = text; }
                }
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("Parameter")) {
                parameter = nullptr;
            } else if (xml.name() == QLatin1String("Effect")) {
                effect = nullptr;
            }
        }
    }
    if (xml.hasError() || !sawChain) {
        LOGW() << "not a chain preset: " << path << " " << xml.errorString();
        return std::nullopt;
    }
    return chain;
}

bool writeChain(const QString& path, const EffectChainPresets::Chain& chain)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    // Mixxx's EffectChainPreset::toXml layout.
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeStartElement("EffectChain");
    xml.writeTextElement("Name", QString::fromStdString(chain.name));
    xml.writeTextElement("MixMode", QString::fromStdString(chain.mixMode));
    xml.writeTextElement("SuperParameterValue", QString::fromStdString(chain.superParameterValue));
    xml.writeStartElement("Effects");
    for (const auto& effect : chain.effects) {
        if (effect.id.empty()) {
            xml.writeEmptyElement("Effect");
            continue;
        }
        xml.writeStartElement("Effect");
        xml.writeTextElement("MetaParameterValue", QString::fromStdString(effect.metaParameterValue));
        xml.writeTextElement("Id", QString::fromStdString(effect.id));
        xml.writeTextElement("BackendType", QString::fromStdString(effect.backendType));
        xml.writeStartElement("Parameters");
        for (const auto& p : effect.parameters) {
            xml.writeStartElement("Parameter");
            xml.writeTextElement("Id", QString::fromStdString(p.id));
            xml.writeTextElement("Value", QString::fromStdString(p.value));
            xml.writeTextElement("LinkType", QString::fromStdString(p.linkType));
            xml.writeTextElement("LinkInversion", QString::fromStdString(p.linkInversion));
            xml.writeTextElement("Hidden", QString::fromStdString(p.hidden));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return file.commit();
}

//! AudioUnits exist only on macOS (EffectFamily has no AudioUnit elsewhere).
bool isAudioUnitFamily(EffectFamily family)
{
#ifdef Q_OS_MACOS
    return family == EffectFamily::AudioUnit;
#else
    Q_UNUSED(family);
    return false;
#endif
}

std::vector<std::string> splitTilde(const std::string& id)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= id.size(); ++i) {
        if (i == id.size() || id[i] == '~') {
            parts.push_back(id.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

//! An AudioUnit parameter key as au3 writes it: "<name,hexid>" or "<hexid>".
std::optional<unsigned long> audioUnitParameterId(const wxString& key)
{
    const int end = key.Find('>', true);
    if (end == wxNOT_FOUND) {
        return std::nullopt;
    }
    int begin = end - 1;
    while (begin >= 0 && key[begin] != ',' && key[begin] != '<') {
        --begin;
    }
    unsigned long value = 0;
    if (begin < 0 || !key.Mid(begin + 1, end - begin - 1).ToULong(&value, 16)) {
        return std::nullopt;
    }
    return value;
}

bool validName(const std::string& name)
{
    return !name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}
}

void EffectChainPresets::init()
{
    settings()->setDefaultValue(CHAIN_PRESET_LIST, Val(ValList {}));
    settings()->setDefaultValue(CHAIN_PRESETS_IMPORTED, Val(false));
    settings()->valueChanged(CHAIN_PRESET_LIST).onReceive(this, [this](const Val&) { m_presetsChanged.notify(); });
    importMixxxPresetsOnce();
}

void EffectChainPresets::importMixxxPresetsOnce()
{
    if (settings()->value(CHAIN_PRESETS_IMPORTED).toBool()) {
        return;
    }
    settings()->setSharedValue(CHAIN_PRESETS_IMPORTED, Val(true));

    // Mixxx keeps every chain preset as effects/chains/<name>.xml and their
    // order in effects.xml's <ChainPresetList>.
    const QString mixxx = mixxxSettingsDirectory();
    QFile effectsXml(mixxx + QStringLiteral("/effects.xml"));
    if (!effectsXml.open(QIODevice::ReadOnly)) {
        return;
    }
    std::vector<std::string> order;
    QXmlStreamReader xml(&effectsXml);
    bool inList = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("ChainPresetList")) {
            inList = true;
        } else if (xml.isEndElement() && xml.name() == QLatin1String("ChainPresetList")) {
            inList = false;
        } else if (inList && xml.isStartElement() && xml.name() == QLatin1String("ChainPresetName")) {
            order.push_back(xml.readElementText().toStdString());
        }
    }

    std::vector<std::string> imported;
    for (const auto& name : order) {
        const QString source = mixxx + QStringLiteral("/effects/chains/") + QString::fromStdString(name) + QStringLiteral(".xml");
        const QString target = QString::fromStdString(filePathFor(name));
        if (!validName(name) || !readChain(source)) {
            continue;
        }
        QDir().mkpath(presetsDirectory());
        if (QFile::exists(target) || QFile::copy(source, target)) {
            imported.push_back(name);
        }
    }
    if (!imported.empty()) {
        writeOrder(imported);
    }
    LOGI() << "GlideMix: " << imported.size() << " effect chain presets taken from Mixxx";
}

std::string EffectChainPresets::filePathFor(const std::string& name) const
{
    return (presetsDirectory() + QStringLiteral("/") + QString::fromStdString(name) + QStringLiteral(".xml")).toStdString();
}

std::vector<std::string> EffectChainPresets::storedOrder() const
{
    std::vector<std::string> names;
    for (const auto& item : settings()->value(CHAIN_PRESET_LIST).toList()) {
        names.push_back(item.toString());
    }
    return names;
}

void EffectChainPresets::writeOrder(const std::vector<std::string>& names)
{
    ValList list;
    for (const auto& name : names) {
        list.push_back(Val(name));
    }
    settings()->setSharedValue(CHAIN_PRESET_LIST, Val(list));
    m_presetsChanged.notify();
}

std::vector<std::string> EffectChainPresets::presetNames() const
{
    // The stored order first, then any preset files not in it yet.
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const auto& name : storedOrder()) {
        if (QFile::exists(QString::fromStdString(filePathFor(name))) && seen.insert(name).second) {
            names.push_back(name);
        }
    }
    std::vector<std::string> extra;
    for (const QFileInfo& info : QDir(presetsDirectory()).entryInfoList({ QStringLiteral("*.xml") }, QDir::Files, QDir::Name)) {
        const std::string name = info.completeBaseName().toStdString();
        if (!seen.count(name)) {
            extra.push_back(name);
        }
    }
    names.insert(names.end(), extra.begin(), extra.end());
    return names;
}

bool EffectChainPresets::hasPreset(const std::string& name) const
{
    return validName(name) && QFile::exists(QString::fromStdString(filePathFor(name)));
}

EffectId EffectChainPresets::resolve(const Effect& effect) const
{
    if (effect.id.empty()) {
        return {};
    }
    if (effect.backendType == kBuiltIn) {
        return mixxxEffectIds() ? mixxxEffectIds()->effectIdForMixxxId(effect.id) : EffectId {};
    }
    if (effect.backendType == kAudioUnit) {
        // Mixxx names an AudioUnit "Manufacturer~Name~version".
        const auto parts = splitTilde(effect.id);
        if (parts.size() < 2) {
            return {};
        }
        for (const EffectMeta& meta : effectsProvider()->effectMetaList()) {
            if (isAudioUnitFamily(meta.family) && meta.vendor.toStdString() == parts[0]
                && meta.title.toStdString() == parts[1] && meta.isLoadable()) {
                return meta.id;
            }
        }
        return {};
    }
    const EffectId id = String::fromStdString(effect.id);
    return effectsProvider()->meta(id).isValid() ? id : EffectId {};
}

std::vector<IEffectChainPresets::PresetEffect> EffectChainPresets::presetEffects(const std::string& name) const
{
    std::vector<PresetEffect> result;
    const auto chain = hasPreset(name) ? readChain(QString::fromStdString(filePathFor(name))) : std::nullopt;
    if (!chain) {
        return result;
    }
    for (const auto& effect : chain->effects) {
        if (effect.id.empty()) {
            continue;
        }
        PresetEffect entry;
        const EffectId id = resolve(effect);
        entry.available = !id.empty();
        if (entry.available) {
            entry.title = effectsProvider()->meta(id).title.toStdString();
        } else {
            const auto parts = splitTilde(effect.id);
            entry.title = parts.size() >= 2 ? parts[1] : effect.id;
        }
        result.push_back(entry);
    }
    return result;
}

Ret EffectChainPresets::saveTrackAsPreset(TrackId trackId, const std::string& name)
{
    if (!validName(name)) {
        return make_ret(Ret::Code::UnknownError);
    }
    const auto stack = realtimeEffectService()->effectStack(trackId);
    if (!stack) {
        return make_ret(Ret::Code::UnknownError);
    }
    Chain chain;
    chain.name = name;
    for (const auto& state : *stack) {
        if (!state) {
            continue;
        }
        const EffectId effectId = au3::wxToString(state->GetID());
        const ::Effect* effect = effectsProvider()->effect(effectId);
        const EffectMeta meta = effectsProvider()->meta(effectId);
        if (!effect || !meta.isValid()) {
            continue;
        }
        CommandParameters parms;
        effect->SaveSettings(state->GetSettings(), parms);

        Effect entry;
        const bool isAudioUnit = isAudioUnitFamily(meta.family);
        const std::string mixxxId = mixxxEffectIds() ? mixxxEffectIds()->mixxxIdForEffectId(effectId) : std::string();
        if (!mixxxId.empty()) {
            entry.id = mixxxId;
            entry.backendType = kBuiltIn;
        } else if (isAudioUnit) {
            entry.id = meta.vendor.toStdString() + "~" + meta.title.toStdString() + "~" + meta.version.toStdString();
            entry.backendType = kAudioUnit;
        } else {
            entry.id = effectId.toStdString();
            entry.backendType = kMixtapeStudio;
        }

        wxString key;
        long index = 0;
        for (bool more = parms.GetFirstEntry(key, index); more; more = parms.GetNextEntry(key, index)) {
            wxString value;
            if (!parms.Read(key, &value)) {
                continue;
            }
            if (entry.backendType == kBuiltIn && key == wxT("meta")) {
                entry.metaParameterValue = au3::wxToStdString(value);   // Mixxx keeps the metaknob apart
                continue;
            }
            Parameter p;
            if (isAudioUnit) {
                const auto id = audioUnitParameterId(key);
                if (!id) {
                    continue;   // e.g. a factory preset number
                }
                p.id = std::to_string(*id);
            } else {
                p.id = au3::wxToStdString(key);
            }
            p.value = au3::wxToStdString(value);
            entry.parameters.push_back(std::move(p));
        }
        chain.effects.push_back(std::move(entry));
    }

    if (!writeChain(QString::fromStdString(filePathFor(name)), chain)) {
        return make_ret(Ret::Code::UnknownError);
    }
    auto order = presetNames();
    if (std::find(order.begin(), order.end(), name) == order.end()) {
        order.push_back(name);
    }
    writeOrder(order);
    return make_ok();
}

Ret EffectChainPresets::loadPresetOntoTrack(const std::string& name, TrackId trackId)
{
    const auto chain = hasPreset(name) ? readChain(QString::fromStdString(filePathFor(name))) : std::nullopt;
    if (!chain) {
        return make_ret(Ret::Code::UnknownError);
    }
    // Mixxx loads a chain preset into the whole effect unit: the track's
    // current effects make way for the preset's.
    if (const auto stack = realtimeEffectService()->effectStack(trackId)) {
        for (auto it = stack->rbegin(); it != stack->rend(); ++it) {
            realtimeEffectService()->removeRealtimeEffect(trackId, *it);
        }
    }
    for (const auto& entry : chain->effects) {
        const EffectId effectId = resolve(entry);
        const ::Effect* effect = effectId.empty() ? nullptr : effectsProvider()->effect(effectId);
        if (!effect) {
            if (!entry.id.empty()) {
                LOGW() << "chain preset " << name << ": effect not available: " << entry.id;
            }
            continue;
        }
        const auto state = realtimeEffectService()->addRealtimeEffect(trackId, effectId);
        if (!state) {
            continue;
        }
        CommandParameters parms;
        const bool isAudioUnit = entry.backendType == kAudioUnit;
        if (entry.backendType == kBuiltIn) {
            parms.Write(wxT("meta"), au3::wxFromStdString(entry.metaParameterValue));
        }
        for (const auto& p : entry.parameters) {
            wxString key = au3::wxFromStdString(p.id);
            if (isAudioUnit) {
                unsigned long id = 0;
                if (!key.ToULong(&id)) {
                    continue;
                }
                key = wxString::Format(wxT("<%lx>"), id);
            }
            parms.Write(key, au3::wxFromStdString(p.value));
        }
        if (const auto access = state->GetAccess()) {
            access->ModifySettings([&](EffectSettings& settings) {
                effect->LoadSettings(parms, settings);
                return nullptr;
            });
            access->Flush();
            if (const auto instance = std::dynamic_pointer_cast<EffectInstance>(state->GetInstance())) {
                instancesRegister()->notifyAboutSettingsChanged(instancesRegister()->instanceIdOf(instance));
            }
        }
    }
    return make_ok();
}

Ret EffectChainPresets::renamePreset(const std::string& name, const std::string& newName)
{
    if (!hasPreset(name) || !validName(newName) || hasPreset(newName)) {
        return make_ret(Ret::Code::UnknownError);
    }
    auto chain = readChain(QString::fromStdString(filePathFor(name)));
    if (!chain) {
        return make_ret(Ret::Code::UnknownError);
    }
    chain->name = newName;
    if (!writeChain(QString::fromStdString(filePathFor(newName)), *chain)) {
        return make_ret(Ret::Code::UnknownError);
    }
    auto order = presetNames();
    QFile::remove(QString::fromStdString(filePathFor(name)));
    std::replace(order.begin(), order.end(), name, newName);
    writeOrder(order);
    return make_ok();
}

Ret EffectChainPresets::removePreset(const std::string& name)
{
    if (!hasPreset(name)) {
        return make_ret(Ret::Code::UnknownError);
    }
    auto order = presetNames();
    // Deleted presets go to the Trash, like deleted projects.
    const QString path = QString::fromStdString(filePathFor(name));
    if (!QFile::moveToTrash(path) && !QFile::remove(path)) {
        return make_ret(Ret::Code::UnknownError);
    }
    order.erase(std::remove(order.begin(), order.end(), name), order.end());
    writeOrder(order);
    return make_ok();
}

void EffectChainPresets::setPresetOrder(const std::vector<std::string>& names)
{
    writeOrder(names);
}

RetVal<std::string> EffectChainPresets::importPreset(const io::path_t& filePath)
{
    auto chain = readChain(filePath.toQString());
    if (!chain) {
        return RetVal<std::string>::make_ret(make_ret(Ret::Code::UnknownError));
    }
    std::string base = chain->name.empty() ? QFileInfo(filePath.toQString()).completeBaseName().toStdString() : chain->name;
    std::replace(base.begin(), base.end(), '/', '_');
    std::replace(base.begin(), base.end(), '\\', '_');
    std::string name = base;
    for (int n = 2; hasPreset(name); ++n) {
        name = base + " (" + std::to_string(n) + ")";
    }
    chain->name = name;
    if (!writeChain(QString::fromStdString(filePathFor(name)), *chain)) {
        return RetVal<std::string>::make_ret(make_ret(Ret::Code::UnknownError));
    }
    auto order = presetNames();
    if (std::find(order.begin(), order.end(), name) == order.end()) {
        order.push_back(name);
    }
    writeOrder(order);
    return RetVal<std::string>::make_ok(name);
}

Ret EffectChainPresets::exportPreset(const std::string& name, const io::path_t& filePath)
{
    const auto chain = hasPreset(name) ? readChain(QString::fromStdString(filePathFor(name))) : std::nullopt;
    if (!chain || !writeChain(filePath.toQString(), *chain)) {
        return make_ret(Ret::Code::UnknownError);
    }
    return make_ok();
}

async::Notification EffectChainPresets::presetsChanged() const
{
    return m_presetsChanged;
}
