/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <gmock/gmock.h>

#include "automation/iclipgaininteraction.h"

namespace au::automation {
class ClipGainInteractionMock : public IClipGainInteraction
{
public:
    MOCK_METHOD(std::optional<AutomationInfo>, clipGainInfo, (const trackedit::ClipKey&), (const, override));
    MOCK_METHOD(AutomationPoints, clipGainPoints, (const trackedit::ClipKey&), (const, override));
    MOCK_METHOD(AutomationSegments, clipGainSegments, (const trackedit::ClipKey&), (const, override));
    MOCK_METHOD(bool, setClipGainSegments, (const trackedit::ClipKey&, const AutomationSegments&, bool), (override));
    MOCK_METHOD(bool, setClipGainPoint, (const trackedit::ClipKey&, double, double, bool), (override));
    MOCK_METHOD(bool, removeClipGainPoint, (const trackedit::ClipKey&, int, bool), (override));
    MOCK_METHOD(bool, setClipGainPointAtIndex, (const trackedit::ClipKey&, int, double, double, bool), (override));
    MOCK_METHOD(bool, beginClipGainPointDrag, (const trackedit::ClipKey&, int), (override));
    MOCK_METHOD(bool, updateClipGainPointDrag, (const trackedit::ClipKey&, double, double), (override));
    MOCK_METHOD(bool, endClipGainPointDrag, (const trackedit::ClipKey&, bool), (override));
    MOCK_METHOD((muse::async::Channel<trackedit::ClipKey, bool>), clipGainChanged, (), (const, override));
    MOCK_METHOD(bool, clipDisabled, (const trackedit::ClipKey&), (const, override));
    MOCK_METHOD(bool, setClipDisabled, (const trackedit::ClipKey&, bool), (override));
    MOCK_METHOD(AutomationPoints, clipBassPoints, (const trackedit::ClipKey&), (const, override));
    MOCK_METHOD(bool, setClipBassPoints, (const trackedit::ClipKey&, const AutomationPoints&), (override));
    MOCK_METHOD(AutomationPoints, clipBandPoints, (const trackedit::ClipKey&, int), (const, override));
    MOCK_METHOD(bool, setClipBandPoints, (const trackedit::ClipKey&, int, const AutomationPoints&), (override));
    MOCK_METHOD(AutomationPoints, clipEffectPoints, (const trackedit::ClipKey&, const std::string&, const std::string&),
                (const, override));
    MOCK_METHOD(bool, setClipEffectPoints, (const trackedit::ClipKey&, const std::string&, const std::string&,
                                            const AutomationPoints&, bool), (override));
    MOCK_METHOD(std::optional<bool>, pluginAutomationActive, (trackedit::TrackId, const std::string&, const std::string&),
                (const, override));
    MOCK_METHOD(bool, setPluginAutomationActive, (trackedit::TrackId, const std::string&, const std::string&, bool),
                (override));
    using AutomationStates = std::map<std::string, bool>;
    MOCK_METHOD(AutomationStates, pluginAutomationStates, (trackedit::TrackId, const std::string&), (const, override));
    MOCK_METHOD(bool, hasOverriddenPluginAutomation, (), (const, override));
    MOCK_METHOD(bool, reenableAllPluginAutomation, (), (override));
    MOCK_METHOD(bool, deletePluginAutomation, (trackedit::TrackId, const std::string&, const std::string&), (override));
    MOCK_METHOD(double, recordPluginAutomation, (trackedit::TrackId, const std::string&, const std::string&,
                                                 const std::string&, double, double, double, bool), (override));
};
}
