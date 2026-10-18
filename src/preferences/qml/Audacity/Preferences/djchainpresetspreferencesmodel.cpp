/*
 * GlideMix
 */
#include "djchainpresetspreferencesmodel.h"

#include <QDir>

#include "global/translation.h"

using namespace au::appshell;

DjChainPresetsPreferencesModel::DjChainPresetsPreferencesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DjChainPresetsPreferencesModel::load()
{
    auto reload = [this]() {
        m_presets.clear();
        if (chainPresets()) {
            for (const auto& name : chainPresets()->presetNames()) {
                m_presets << QString::fromStdString(name);
            }
        }
        emit presetsChanged();
    };
    reload();
    if (chainPresets()) {
        chainPresets()->presetsChanged().onNotify(this, reload);
    }
}

QStringList DjChainPresetsPreferencesModel::presets() const
{
    return m_presets;
}

QVariantList DjChainPresetsPreferencesModel::effectsIn(const QString& name) const
{
    QVariantList result;
    if (!chainPresets()) {
        return result;
    }
    for (const auto& effect : chainPresets()->presetEffects(name.toStdString())) {
        result.push_back(QVariantMap { { "title", QString::fromStdString(effect.title) }, { "available", effect.available } });
    }
    return result;
}

void DjChainPresetsPreferencesModel::movePreset(const QString& name, int toIndex)
{
    QStringList order = m_presets;
    const int from = order.indexOf(name);
    if (from < 0) {
        return;
    }
    if (toIndex > from) {
        --toIndex;   // counted with the preset still in place
    }
    order.removeAt(from);
    order.insert(std::clamp(toIndex, 0, int(order.size())), name);
    std::vector<std::string> names;
    for (const QString& n : order) {
        names.push_back(n.toStdString());
    }
    chainPresets()->setPresetOrder(names);
}

QString DjChainPresetsPreferencesModel::importPresets()
{
    const auto paths = interactive()->selectOpeningFilesSync(muse::trc("preferences", "Import chain presets"),
                                                             muse::io::path_t(QDir::homePath()),
                                                             { muse::trc("preferences", "Chain presets") + " (*.xml)" });
    QString last;
    for (const auto& path : paths) {
        const auto rv = chainPresets()->importPreset(path);
        if (rv.ret) {
            last = QString::fromStdString(rv.val);
        } else {
            interactive()->error(muse::trc("preferences", "Import chain presets"),
                                 muse::qtrc("preferences", "“%1” is not an effect chain preset.")
                                 .arg(path.toQString()).toStdString());
        }
    }
    return last;
}

void DjChainPresetsPreferencesModel::exportPreset(const QString& name)
{
    const auto path = interactive()->selectSavingFileSync(muse::trc("preferences", "Export chain preset"),
                                                          muse::io::path_t(QDir::homePath() + "/" + name + ".xml"),
                                                          { muse::trc("preferences", "Chain presets") + " (*.xml)" });
    if (path.empty()) {
        return;
    }
    if (!chainPresets()->exportPreset(name.toStdString(), path)) {
        interactive()->error(muse::trc("preferences", "Export chain preset"),
                             muse::trc("preferences", "The chain preset could not be written."));
    }
}

QString DjChainPresetsPreferencesModel::renamePreset(const QString& name)
{
    muse::UriQuery query("audacity://effects/presets/input_name");
    query.addParam("title", muse::Val(muse::qtrc("preferences", "Rename chain preset").toStdString()));
    const auto rv = interactive()->openSync(query);
    const std::string newName = rv.val.toString();
    if (rv.ret.code() == static_cast<int>(muse::Ret::Code::Cancel) || newName.empty() || newName == name.toStdString()) {
        return {};
    }
    if (chainPresets()->hasPreset(newName) || !chainPresets()->renamePreset(name.toStdString(), newName)) {
        interactive()->error(muse::trc("preferences", "Rename chain preset"),
                             muse::qtrc("preferences", "A chain preset named “%1” already exists.")
                             .arg(QString::fromStdString(newName)).toStdString());
        return {};
    }
    return QString::fromStdString(newName);
}

bool DjChainPresetsPreferencesModel::deletePreset(const QString& name)
{
    const auto answer = interactive()->questionSync(
        muse::trc("preferences", "Delete chain preset"),
        muse::qtrc("preferences", "Move chain preset “%1” to the Trash?").arg(name).toStdString(),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Yes });
    if (answer.button() != static_cast<int>(muse::IInteractive::Button::Yes)) {
        return false;
    }
    return bool(chainPresets()->removePreset(name.toStdString()));
}
