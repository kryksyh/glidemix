/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "au3-module-manager/PluginManager.h"
#include "au3-module-manager/IEffectIdResolver.h"
#include "au3-preferences/BasicSettings.h"

// Isolated native-plugin tests must not use the user's preferences or plugin
// database. An unregistered ID deliberately resolves to no persisted settings.
class AutomationTestSettings final : public audacity::BasicSettings
{
public:
    wxString GetGroup() const override { return {}; }
    wxArrayString GetChildGroups() const override { return {}; }
    wxArrayString GetChildKeys() const override { return {}; }
    bool HasEntry(const wxString&) const override { return false; }
    bool HasGroup(const wxString&) const override { return false; }
    bool Remove(const wxString&) override { return true; }
    void Clear() override {}
    bool Flush() noexcept override { return true; }
#define TEST_SETTING_TYPE(T) \
    bool Read(const wxString&, T*) const override { return false; } \
    bool Write(const wxString&, T) override { return true; }
    TEST_SETTING_TYPE(bool)
    TEST_SETTING_TYPE(int)
    TEST_SETTING_TYPE(long)
    TEST_SETTING_TYPE(long long)
    TEST_SETTING_TYPE(double)
#undef TEST_SETTING_TYPE
    bool Read(const wxString&, wxString*) const override { return false; }
    bool Write(const wxString&, const wxString&) override { return true; }
protected:
    void DoBeginGroup(const wxString&) override {}
    void DoEndGroup() noexcept override {}
};

class AutomationTestResolver final : public IEffectIdResolver
{
public:
    PluginID EffectId(const EffectDefinitionInterface*) const override { return {}; }
    bool IsEffectId(const PluginID&) const override { return false; }
    wxString EffectFamily(const PluginID&) const override { return {}; }
    wxString EffectVendor(const PluginID&) const override { return {}; }
    wxString EffectName(const PluginID&) const override { return {}; }
    wxString EffectPath(const PluginID&) const override { return {}; }
};

inline void InitializeAutomationTestHost()
{
    PluginManager::Get().Initialize([](const auto&) {
        return std::make_unique<AutomationTestSettings>();
    }, std::make_unique<AutomationTestResolver>());
}
