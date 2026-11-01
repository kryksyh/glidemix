/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <string>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

namespace au::effects {
//! GlideMix: a VST3 plug-in's editor view, passed through as is, with every
//! call into it inside a PluginCrashGuard scope. The window is set up by
//! muse's VstView, which we don't change; with this in between, a plug-in
//! that crashes while its window opens, resizes or closes is still named on
//! the next start. Other interfaces (content scale) go straight to the
//! plug-in's own view.
class GuardedPlugView final : public Steinberg::IPlugView
{
public:
    GuardedPlugView(Steinberg::IPtr<Steinberg::IPlugView> view, std::string guardId);
    virtual ~GuardedPlugView();

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onWheel(float distance) override;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

    DECLARE_FUNKNOWN_METHODS

private:
    Steinberg::IPtr<Steinberg::IPlugView> m_view;
    const std::string m_guardId;
};
}
