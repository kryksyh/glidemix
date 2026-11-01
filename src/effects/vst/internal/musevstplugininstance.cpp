/*
* Audacity: A Digital Audio Editor
*/
#include "musevstplugininstance.h"
#include "guardedplugview.h"

#include "au3-vst3/VST3Instance.h"
#include "au3-vst3/VST3Wrapper.h"

// from muse
#include "vst/vsttypes.h"

#include "au3-utility/PluginCrashGuard.h"

#include "log.h"

using namespace au::effects;

MuseVstPluginInstance::MuseVstPluginInstance(const EffectId& effectId,
                                             const EffectInstanceId& instanceId,
                                             std::shared_ptr<VST3Instance> auVstInstance)
    : m_effectId(effectId),
    m_resourceId(effectId.toStdString()),
    m_instanceId(instanceId),
    m_auVstInstance(auVstInstance)
{
}

const muse::audio::AudioResourceId& MuseVstPluginInstance::resourceId() const
{
    return m_resourceId;
}

const std::string& MuseVstPluginInstance::name() const
{
    return m_auVstInstance->GetWrapper().GetModule().getName();
}

muse::vst::VstPluginInstanceId MuseVstPluginInstance::id() const
{
    return m_instanceId;
}

bool MuseVstPluginInstance::isLoaded() const
{
    return true;
}

muse::async::Notification MuseVstPluginInstance::loadingCompleted() const
{
    static muse::async::Notification n;
    return n;
}

muse::vst::PluginViewPtr MuseVstPluginInstance::createView() const
{
    auto controller = this->controller();
    if (!controller) {
        return nullptr;
    }

    // GlideMix: the plug-in's window code runs here and in the view's
    // attached(); a crash in either turns the plug-in off on the next start
    const std::string guardId = m_effectId.toStdString();
    PluginCrashGuard::MarkedScope guard(guardId);
    muse::vst::PluginViewPtr view = owned(controller->createView(muse::vst::PluginEditorViewType::kEditor));
    if (!view) {
        return nullptr;
    }
    return muse::vst::PluginViewPtr(new GuardedPlugView(view, guardId), false);
}

muse::vst::PluginControllerPtr MuseVstPluginInstance::controller() const
{
    return m_auVstInstance->vstEditController();
}

muse::vst::PluginComponentPtr MuseVstPluginInstance::component() const
{
    return m_auVstInstance->effectComponent();
}

muse::vst::PluginMidiMappingPtr MuseVstPluginInstance::midiMapping() const
{
    NOT_IMPLEMENTED;
    return nullptr;
}

void MuseVstPluginInstance::updatePluginConfig(const muse::audio::AudioUnitConfig& config)
{
    UNUSED(config);
}

void MuseVstPluginInstance::refreshConfig()
{
}

muse::async::Channel<muse::audio::AudioUnitConfig> MuseVstPluginInstance::pluginSettingsChanged() const
{
    static muse::async::Channel<muse::audio::AudioUnitConfig> n;
    return n;
}
