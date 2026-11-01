/*
* Audacity: A Digital Audio Editor
*/
#include "guardedplugview.h"

#include "au3-utility/PluginCrashGuard.h"

using namespace au::effects;
using namespace Steinberg;

GuardedPlugView::GuardedPlugView(IPtr<IPlugView> view, std::string guardId)
    : m_view(std::move(view)), m_guardId(std::move(guardId))
{
    FUNKNOWN_CTOR
}

GuardedPlugView::~GuardedPlugView()
{
    // the plug-in's view goes away here when this held the last reference
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    m_view = nullptr;
    FUNKNOWN_DTOR
}

IMPLEMENT_REFCOUNT(GuardedPlugView)

tresult PLUGIN_API GuardedPlugView::queryInterface(const TUID _iid, void** obj)
{
    QUERY_INTERFACE(_iid, obj, FUnknown::iid, IPlugView)
    QUERY_INTERFACE(_iid, obj, IPlugView::iid, IPlugView)
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->queryInterface(_iid, obj);
}

tresult PLUGIN_API GuardedPlugView::isPlatformTypeSupported(FIDString type)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->isPlatformTypeSupported(type);
}

tresult PLUGIN_API GuardedPlugView::attached(void* parent, FIDString type)
{
    // the plug-in builds its whole interface here: rare, and where
    // plug-ins that don't like our host tend to fall over
    PluginCrashGuard::MarkedScope guard(m_guardId);
    return m_view->attached(parent, type);
}

tresult PLUGIN_API GuardedPlugView::removed()
{
    PluginCrashGuard::MarkedScope guard(m_guardId);
    return m_view->removed();
}

tresult PLUGIN_API GuardedPlugView::onWheel(float distance)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->onWheel(distance);
}

tresult PLUGIN_API GuardedPlugView::onKeyDown(char16 key, int16 keyCode, int16 modifiers)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->onKeyDown(key, keyCode, modifiers);
}

tresult PLUGIN_API GuardedPlugView::onKeyUp(char16 key, int16 keyCode, int16 modifiers)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->onKeyUp(key, keyCode, modifiers);
}

tresult PLUGIN_API GuardedPlugView::getSize(ViewRect* size)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->getSize(size);
}

tresult PLUGIN_API GuardedPlugView::onSize(ViewRect* newSize)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->onSize(newSize);
}

tresult PLUGIN_API GuardedPlugView::onFocus(TBool state)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->onFocus(state);
}

tresult PLUGIN_API GuardedPlugView::setFrame(IPlugFrame* frame)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->setFrame(frame);
}

tresult PLUGIN_API GuardedPlugView::canResize()
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->canResize();
}

tresult PLUGIN_API GuardedPlugView::checkSizeConstraint(ViewRect* rect)
{
    PluginCrashGuard::CallScope guard(m_guardId.c_str());
    return m_view->checkSizeConstraint(rect);
}
