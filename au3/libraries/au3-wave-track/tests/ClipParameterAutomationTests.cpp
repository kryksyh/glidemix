/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "au3-wave-track/ClipParameterAutomation.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-components/ParameterAutomationSnapshot.h"
#include "au3-components/ParameterAutomationTimeQueue.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "au3-stretching-sequence/tests/MockSampleBlockFactory.h"
#include "AutomationTestHost.h"
#include <QUuid>
#include <QCoreApplication>
#include "au3-xml/XMLFileReader.h"
#include "au3-project-file-io/ProjectSerializer.h"
#include "au3-utility/BufferedStreamReader.h"
#include <wx/init.h>
#ifdef MIXHOUSE_TEST_VST3
#include "au3-vst3/VST3Wrapper.h"
#endif
#ifdef MIXHOUSE_TEST_AUDIO_UNIT
#include "au3-audio-unit/AudioUnitEffectBase.h"
#include "au3-audio-unit/AudioUnitInstance.h"
#include <AudioToolbox/AudioUnitParameters.h>
#endif

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void Near(double actual, double expected)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) >= 1e-8) {
        throw std::runtime_error("Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

const std::string Instance = "812c8be6-cfff-437f-bbdb-fd64737c1afd";
const std::string Plugin = "Effect__VST3_Test & Filter";

//! Silence-only storage for round-trip tests; never touches a user's database.
class SilentRoundTripFactory final : public SampleBlockFactory
{
    SampleBlockIDs GetActiveBlockIDs() override { return {}; }
    SampleBlockPtr DoCreate(constSamplePtr data, size_t length, sampleFormat format) override
    {
        return std::make_shared<MockSampleBlock>(++mId, data, length, format);
    }
    SampleBlockPtr DoCreateSilent(size_t length, sampleFormat format) override
    {
        const std::vector<char> zeros(length * SAMPLE_SIZE(format));
        return DoCreate(zeros.data(), length, format);
    }
    SampleBlockPtr DoCreateFromXML(sampleFormat format, const AttributesList& attrs) override
    {
        long long length = 0;
        for (const auto& [name, value] : attrs) {
            if (name == "length") { value.TryGet(length); }
        }
        if (!length) {
            std::string details;
            for (const auto& [name, value] : attrs) { details += std::string(name) + "=" + value.ToString() + " "; }
            throw std::runtime_error("Serialized test block has no length: " + details);
        }
        return DoCreateSilent(length, format);
    }
    SampleBlockPtr DoCreateFromId(sampleFormat, SampleBlockID) override { return {}; }
    long long mId{};
};

class SerializedInput final : public BufferedStreamReader
{
public:
    explicit SerializedInput(const ProjectSerializer& serializer)
    {
        for (const auto* stream : { &serializer.GetDict(), &serializer.GetData() }) {
            const auto* bytes = static_cast<const uint8_t*>(stream->GetData());
            mBytes.insert(mBytes.end(), bytes, bytes + stream->GetSize());
        }
    }
private:
    bool HasMoreData() const override { return mPosition < mBytes.size(); }
    size_t ReadData(void* output, size_t limit) override
    {
        const auto count = std::min(limit, mBytes.size() - mPosition);
        std::copy_n(mBytes.data() + mPosition, count, static_cast<uint8_t*>(output));
        mPosition += count;
        return count;
    }
    std::vector<uint8_t> mBytes;
    size_t mPosition{};
};

ClipParameterAutomation MakeData()
{
    ClipParameterAutomation data;
    data.SetOffset(10.0);
    data.SetTrackLen(8.0, 0.001);
    auto* curve = data.AddCurve(Instance, Plugin, "cutoff", 0.25);
    Require(curve != nullptr, "Could not create curve");
    curve->envelope.InsertOrReplace(11.0, 0.2);
    curve->envelope.InsertOrReplace(15.0, 0.8);
    return data;
}

void Serialization()
{
    auto original = MakeData();
    original.FindCurve(Instance, "cutoff")->enabled = false;
    auto* empty = original.AddCurve(Instance, Plugin, "mix", 0.75);
    Require(empty != nullptr, "Multiple parameters must coexist");
    XMLStringWriter xml;
    original.WriteXML(xml);
    ClipParameterAutomation restored;
    XMLFileReader reader;
    Require(reader.ParseString(&restored, xml), "XML parse failed");
    Require(!restored.HasLoadError(), "Round trip reported bad data");
    restored.SetTrackLen(8.0, 0.001);
    restored.SetOffset(30.0);
    Require(restored.Curves().size() == 2, "Lost parameter curves");
    const auto* curve = restored.FindCurve(Instance, "cutoff");
    Require(curve && curve->pluginId == Plugin && !curve->enabled, "Lost binding or enabled state");
    Near(curve->envelope.GetValue(33.0), 0.5);
    Near(restored.FindCurve(Instance, "mix")->envelope.GetValue(33.0), 0.75);
    Near(curve->envelope[0].GetT(), 1.0);

    XMLStringWriter emptyXml;
    ClipParameterAutomation{}.WriteXML(emptyXml);
    Require(emptyXml.empty(), "Unchanged projects must not get empty automation elements");
}

void Validation()
{
    auto data = MakeData();
    Require(!data.AddCurve(Instance, Plugin, "cutoff", 0.5), "Duplicate binding accepted");
    Require(!data.AddCurve("not-a-uuid", Plugin, "mix", 0.5), "Invalid identity accepted");
    Require(!data.AddCurve(Instance, Plugin, "mix", std::numeric_limits<double>::quiet_NaN()), "NaN accepted");
    Require(!data.AddCurve(Instance, Plugin, "mix", 1.01), "Out-of-range value accepted");

    for (const auto& bad : {
        wxString("<parameterautomation version='3'/>"),
        wxString("<parameterautomation version='1'><unknown/></parameterautomation>"),
        wxString::Format("<parameterautomation version='1'><curve instance='%s' plugin='x' parameter='y' default='0'>"
                         "<point t='1' v='2'/></curve></parameterautomation>", wxString::FromUTF8(Instance)),
        wxString::Format("<parameterautomation version='1'><curve instance='%s' plugin='x' parameter='y' default='0'>"
                         "<point t='2' v='.2'/><point t='1' v='.1'/></curve></parameterautomation>", wxString::FromUTF8(Instance))
    }) {
        ClipParameterAutomation invalid;
        XMLFileReader reader;
        reader.ParseString(&invalid, bad);
        Require(invalid.HasLoadError(), "Malformed/unsupported data not flagged");
        bool refused = false;
        try { XMLStringWriter xml; invalid.WriteXML(xml); }
        catch (const std::runtime_error&) { refused = true; }
        Require(refused, "Unreadable automation would be silently overwritten");
    }
}

void EditAndSnapshot()
{
    auto data = MakeData();
    const auto snapshot = data;
    data.SetOffset(20.0);
    Near(data.Curves()[0].envelope.GetValue(23.0), 0.5);
    data.RescaleTimesBy(2.0);
    Near(data.Curves()[0].envelope.GetValue(26.0), 0.5);
    Near(snapshot.Curves()[0].envelope.GetValue(13.0), 0.5);
    Near(snapshot.Curves()[0].envelope[0].GetT(), 1.0);
    data = snapshot;
    data.InsertSpace(12.0, 2.0);
    Near(data.Curves()[0].envelope.GetValue(17.0), 0.8);
    data.CollapseRegion(12.0, 14.0, 0.001);
    Near(data.Curves()[0].envelope.GetValue(15.0), 0.8);
}

void Paste()
{
    auto destination = MakeData();
    auto source = MakeData();
    source.SetOffset(0.0);
    Require(source.AddCurve(Instance, Plugin, "mix", 0.75), "Could not create source curve");
    destination.Paste(18.0, source, 0.001);
    Require(destination.Curves().size() == 2, "Paste lost source-only parameter");
    Near(destination.FindCurve(Instance, "cutoff")->envelope.GetValue(21.0), 0.5);
    Near(destination.FindCurve(Instance, "mix")->envelope.GetValue(21.0), 0.75);
    Near(destination.Curves()[0].envelope.GetTrackLen(), 16.0);
    auto conflict = MakeData();
    conflict.FindCurve(Instance, "cutoff")->pluginId = "different-plugin";
    Require(!destination.CanPaste(conflict), "Conflicting instance binding accepted");
}

void EffectIdentity()
{
    // Missing plugin is deliberate: saving identity must not need a loaded binary.
    auto original = std::make_shared<RealtimeEffectState>(wxString("missing-test-plugin"));
    auto separate = std::make_shared<RealtimeEffectState>(wxString("missing-test-plugin"));
    original->SetAutomationMode(RealtimeEffectState::AutomationMode::Latch);
    Require(original->GetInstanceId() != separate->GetInstanceId(), "Repeated plugin instances share an identity");
    Require(original->Clone()->GetInstanceId() == original->GetInstanceId(), "Undo snapshot changed identity");
    Require(original->Clone()->GetAutomationMode() == RealtimeEffectState::AutomationMode::Latch, "Undo snapshot changed automation mode");
    Require(original->Clone(true)->GetInstanceId() != original->GetInstanceId(), "Insertion copy reused identity");
    const auto separateId = separate->GetInstanceId();
    *separate = *original;
    Require(separate->GetInstanceId() == separateId, "Settings assignment changed target identity");
    XMLStringWriter xml;
    original->WriteXML(xml);
    XMLFileReader reader;
    auto restored = std::make_shared<RealtimeEffectState>(wxString{});
    Require(reader.ParseString(restored.get(), xml), "Effect XML parse failed");
    Require(restored->GetInstanceId() == original->GetInstanceId(), "Effect identity lost on reopen");
    Require(restored->GetAutomationMode() == RealtimeEffectState::AutomationMode::Latch, "Automation mode lost on reopen");
    ProjectSerializer serializedEffect;
    original->WriteXML(serializedEffect);
    SerializedInput effectInput(serializedEffect);
    auto binaryRestored = std::make_shared<RealtimeEffectState>(wxString{});
    Require(ProjectSerializer::Decode(effectInput, binaryRestored.get()), "Binary effect restore failed");
    Require(binaryRestored->GetInstanceId() == original->GetInstanceId(), "Binary effect identity lost");
    Require(binaryRestored->GetAutomationMode() == RealtimeEffectState::AutomationMode::Latch, "Automation mode lost in binary restore");
    auto legacy = std::make_shared<RealtimeEffectState>(wxString{});
    XMLFileReader legacyReader;
    Require(legacyReader.ParseString(legacy.get(), "<effect id='missing-test-plugin' active='1'/>"), "Legacy effect rejected");
    Require(!QUuid(QString::fromStdString(legacy->GetInstanceId())).isNull(), "Legacy effect not assigned an identity");
}

void WaveClipEdits()
{
    auto factory = std::make_shared<MockSampleBlockFactory>();
    auto clip = WaveClip::NewShared(1, factory, floatSample, 1000);
    clip->InsertSilence(0.0, 8.0);
    clip->SetSequenceStartTime(10.0);
    auto* curve = clip->GetParameterAutomation().AddCurve(Instance, Plugin, "cutoff", 0.25);
    Require(curve, "Could not bind clip parameter");
    curve->envelope.InsertOrReplace(11.0, 0.2);
    curve->envelope.InsertOrReplace(15.0, 0.8);
    clip->GetEnvelope().InsertOrReplace(12.0, 0.7);
    auto backup = WaveClip::NewSharedFrom(*clip, factory, true, true);
    clip->ShiftBy(10.0);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(23.0), 0.5);
    clip->StretchBy(2.0);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(26.0), 0.5);
    Near(backup->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
    Near(backup->GetEnvelope().GetValue(12.0), 0.7);
    Near(clip->GetEnvelope().GetValue(24.0), 0.7);

    auto split = WaveClip::NewSharedFromRange(*backup, factory, true, false, 12.0, 16.0);
    Near(split->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
    split->SetTrimLeft(0.0);
    Near(split->GetParameterAutomation().Curves()[0].envelope.GetValue(11.0), 0.2);
    backup->Clear(12.0, 14.0);
    Near(backup->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.8);
    Near(backup->GetEnvelope().GetValue(12.0), 0.7);
    auto leftStretch = WaveClip::NewSharedFrom(*split, factory, true, true);
    const auto oldStart = leftStretch->GetPlayStartTime();
    const auto oldEnd = leftStretch->GetPlayEndTime();
    const auto before = leftStretch->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0);
    leftStretch->StretchLeftTo(oldStart - 2.0);
    const auto ratio = (oldEnd - oldStart + 2.0) / (oldEnd - oldStart);
    Near(leftStretch->GetParameterAutomation().Curves()[0].envelope.GetValue(oldEnd - (oldEnd - 13.0) * ratio), before);
}

void ClipRoundTrip()
{
    auto factory = std::make_shared<SilentRoundTripFactory>();
    auto clip = WaveClip::NewShared(2, factory, floatSample, 1000);
    clip->InsertSilence(0.0, 8.0);
    clip->SetSequenceStartTime(10.0);
    clip->SetTrimLeft(1.0);
    clip->SetTrimRight(1.0);
    auto* curve = clip->GetParameterAutomation().AddCurve(Instance, Plugin, "cutoff", 0.25);
    Require(curve, "Could not create curve");
    curve->envelope.InsertOrReplace(11.0, 0.2);
    curve->envelope.InsertOrReplace(15.0, 0.8);
    clip->GetEnvelope().InsertOrReplace(12.0, 0.7);
    for (size_t channel = 0; channel < 2; ++channel) {
        auto restored = WaveClip::NewShared(1, factory, floatSample, 1000);
        XMLStringWriter xml;
        clip->WriteXML(channel, xml);
        XMLFileReader reader;
        Require(reader.ParseString(restored.get(), xml), "Full clip XML reopen failed");
        Near(restored->GetPlayStartTime(), 11.0);
        Near(restored->GetPlayEndTime(), 17.0);
        Near(restored->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
        Near(restored->GetEnvelope().GetValue(12.0), 0.7);
    }
    ProjectSerializer serializer;
    clip->WriteXML(0, serializer);
    SerializedInput input(serializer);
    auto restored = WaveClip::NewShared(1, factory, floatSample, 1000);
    Require(ProjectSerializer::Decode(input, restored.get()), "Project binary serialization failed");
    Require(!restored->GetParameterAutomation().HasLoadError(), "Binary attribute parsing failed");
    Near(restored->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
    Near(restored->GetEnvelope().GetValue(12.0), 0.7);
}

void CutRestoreAndTempo()
{
    auto factory = std::make_shared<MockSampleBlockFactory>();
    auto clip = WaveClip::NewShared(1, factory, floatSample, 1000);
    clip->InsertSilence(0.0, 8.0);
    clip->SetSequenceStartTime(10.0);
    clip->GetParameterAutomation() = MakeData();
    clip->ClearAndAddCutLine(12.0, 14.0);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.8);
    clip->ExpandCutLine(12.0);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(15.0), 0.8);
    clip->OnProjectTempoChange(120.0, 60.0);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(26.0), 0.5);
    clip->SetRate(2000);
    Near(clip->GetParameterAutomation().Curves()[0].envelope.GetValue(13.0), 0.5);
}

void SnapshotEvaluation()
{
    using Snapshot = ParameterAutomationSnapshot;
    const Snapshot snapshot({{"cutoff", 11.0, 17.0, 10.0, 0.25, {{1.0, 0.2}, {5.0, 0.8}}}});
    Require(snapshot.ParameterCount() == 1 && snapshot.ParameterId(0) == "cutoff", "Parameter identity changed");
    Near(snapshot.Evaluate(0, 13.0).normalized, 0.5);
    Require(snapshot.Evaluate(0, 10.0).status == Snapshot::Status::Inactive, "Trimmed audio was automated");
    Require(snapshot.Evaluate(0, 17.0).status == Snapshot::Status::Inactive, "Right endpoint must be exclusive");
    Require(snapshot.Evaluate(0, std::numeric_limits<double>::quiet_NaN()).status == Snapshot::Status::InvalidTime, "Invalid transport time accepted");
    const double looped[] = {14.0, 15.0, 11.0, 12.0};
    Snapshot::Value output[4];
    snapshot.EvaluateBlock(0, looped, 4, output);
    Near(output[0].normalized, 0.65);
    Near(output[1].normalized, 0.8);
    Near(output[2].normalized, 0.2);
    Near(output[3].normalized, 0.35);
    snapshot.EvaluateUniformBlock(0, 14.0, -1.0, 4, output);
    Near(output[0].normalized, 0.65);
    Near(output[3].normalized, 0.2);

    const Snapshot jumps({{"mix", 0.0, 5.0, 0.0, 0.0,
                          {{0.0, 0.2}, {2.0, 0.2}, {2.0, 0.8}, {4.0, 0.8}}}});
    Near(jumps.Evaluate(0, 1.999).normalized, 0.2);
    Near(jumps.Evaluate(0, 2.0).normalized, 0.8);
    const Snapshot conflicting({{"mix", 0.0, 3.0, 0.0, 0.2, {}},
                                {"mix", 2.0, 4.0, 2.0, 0.8, {}}});
    Require(conflicting.Evaluate(0, 2.5).status == Snapshot::Status::Conflict, "Overlap silently selected a winner");
    Near(conflicting.Evaluate(0, 3.0).normalized, 0.8);
}

// GlideMix: a switch's line holds each value until the next point (Ableton's
// on/off steps) and survives saving.
void SteppedSwitches()
{
    using Snapshot = ParameterAutomationSnapshot;
    Snapshot::Region region{"band1-on", 0.0, 8.0, 0.0, 1.0, {{0.0, 1.0}, {4.0, 0.0}, {6.0, 1.0}}};
    region.stepped = true;
    const Snapshot steps({region});
    Near(steps.Evaluate(0, 3.9).normalized, 1.0);   // a ramp would be 0.025 here
    Near(steps.Evaluate(0, 4.0).normalized, 0.0);
    Near(steps.Evaluate(0, 5.9).normalized, 0.0);
    Near(steps.Evaluate(0, 6.0).normalized, 1.0);

    ClipParameterAutomation data;
    auto* curve = data.AddCurve("123e4567-e89b-12d3-a456-426614174000", "Effect_Test", "band1-on", 1.0);
    Require(curve != nullptr, "Curve was not added");
    curve->stepped = true;
    XMLStringWriter writer;
    data.WriteXML(writer);
    Require(writer.find("stepped=\"1\"") != std::string::npos || writer.find("stepped=\"true\"") != std::string::npos,
            "Stepped flag was not saved");
}

void ProcessingBlockConsistency()
{
    using Snapshot = ParameterAutomationSnapshot;
    const Snapshot snapshot({{"gain", 0.0, 2.0, 0.0, 0.0, {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.0}}}});
    // Deterministic gain processor: compare an offline-sized render against
    // differently partitioned playback buffers and an analytic reference.
    constexpr size_t frames = 88200;
    constexpr double rate = 44100.0;
    std::vector<Snapshot::Value> offline(frames), playback(frames);
    snapshot.EvaluateUniformBlock(0, 0.0, 1.0 / rate, frames, offline.data());
    for (const size_t block : {size_t(1), size_t(127), size_t(512), size_t(4096)}) {
        for (size_t first = 0; first < frames; first += block) {
            snapshot.EvaluateUniformBlock(0, first / rate, 1.0 / rate,
                                          std::min(block, frames - first), playback.data() + first);
        }
        for (size_t i = 0; i < frames; ++i) {
            Require(offline[i].status == Snapshot::Status::Active && playback[i].status == Snapshot::Status::Active,
                    "Active curve disappeared at a processing boundary");
            const auto time = i / rate;
            const double signal = std::sin(time * 440.0 * 6.283185307179586);
            const double expected = signal * (time < 1.0 ? time : 2.0 - time);
            Near(offline[i].normalized * signal, expected);
            Near(playback[i].normalized * signal, expected);
        }
    }
}

#ifdef MIXHOUSE_TEST_VST3
void Vst3ParameterQueue()
{
    const ParameterAutomationSnapshot snapshot({{"123", 1.0, 4.0, 1.0, 0.0, {{0.0, 0.0}, {2.0, 1.0}}}});
    SingleInputParameterValue queue;
    queue.SetAutomation(123, 0.3, snapshot, 0, {nullptr, 0.0, 1.0}, 5);
    Require(queue.getParameterId() == 123 && queue.getPointCount() == 5, "VST3 queue header mismatch");
    const double expected[] = {0.3, 0.0, 0.5, 1.0, 0.3};
    for (Steinberg::int32 i = 0; i < 5; ++i) {
        Steinberg::int32 offset = -1;
        double value = -1.0;
        Require(queue.getPoint(i, offset, value) == Steinberg::kResultOk && offset == i, "VST3 sample offset mismatch");
        Near(value, expected[i]);
    }
    Steinberg::int32 offset;
    double value;
    Require(queue.getPoint(-1, offset, value) == Steinberg::kResultFalse, "Negative queue index accepted");
    Require(queue.getPoint(5, offset, value) == Steinberg::kResultFalse, "Past-end queue index accepted");
    const double loop[] = {2.25, 3.5, 1.0};
    queue.SetAutomation(123, 0.3, snapshot, 0, {loop}, 3, 4);
    queue.getPoint(0, offset, value);
    Near(value, 0.75); // normalized .625 rounds to a quarter-step.
    queue.getPoint(2, offset, value);
    Near(value, 0.0);
    queue.Set(123, 0.7);
    Require(queue.getPointCount() == 1, "Ordinary setting retained automation state");
    queue.getPoint(0, offset, value);
    Near(value, 0.7);
}
#endif
}

void LatencyTiming()
{
    // Two serial effects with delays of 3 and 2 frames. The input wraps
    // several times; subtracting a duration would give incorrect project time.
    for (size_t blockSize : {size_t(1), size_t(4), size_t(17)}) {
        ParameterAutomationTimeQueue first, second;
        first.Prepare(blockSize, 3);
        second.Prepare(blockSize, 2);
        std::vector<double> input(80), block(blockSize), output;
        for (size_t i = 0; i < input.size(); ++i) { input[i] = 10.0 + (i % 13) * 0.01; }
        size_t delay1 = 3, delay2 = 2;
        for (size_t i = 0; i < input.size(); i += blockSize) {
            const auto count = std::min(blockSize, input.size() - i);
            const auto discard1 = std::min(delay1, count);
            delay1 -= discard1;
            const auto* times1 = first.Process(input.data() + i, count, discard1);
            const auto produced1 = count - discard1;
            const auto discard2 = std::min(delay2, produced1);
            delay2 -= discard2;
            const auto* times2 = second.Process(times1, produced1, discard2);
            output.insert(output.end(), times2, times2 + produced1 - discard2);
        }
        Require(output.size() == input.size() - 5, "Serial latency changed the output length");
        for (size_t i = 0; i < output.size(); ++i) { Near(output[i], input[i]); }
        first.Reset();
        Near(first.Process(input.data(), 1, 0)[0], input[0]);
    }
    ParameterAutomationTimeQueue bounded;
    bounded.Prepare(4, 2);
    const double input[]{1, 2, 3, 4};
    bounded.Process(input, 4, 3);
    bool refused = false;
    try { bounded.Process(input, 4, 0); }
    catch (const std::runtime_error&) { refused = true; }
    Require(refused, "Changed latency overran the prepared timestamp buffer");
}

#ifdef MIXHOUSE_TEST_AUDIO_UNIT
void NativeAudioUnitAutomation()
{
    AudioComponentDescription description{kAudioUnitType_Effect, kAudioUnitSubType_Delay,
        kAudioUnitManufacturer_Apple, 0, 0};
    const auto component = AudioComponentFindNext(nullptr, &description);
    if (!component) {
        std::cout << "SKIP: Apple AUDelay is unavailable on this host\n";
        return;
    }
    AudioUnitEffectBase effect("mixhouse-test-audelay", "AUDelay automation test", component);
    Require(effect.InitializePlugin(), "Could not initialize native AUDelay");
    auto settings = effect.MakeSettings();
    auto instance = effect.MakeInstance();
    Require(instance != nullptr, "Could not create native AUDelay instance");
    ChannelName channels[]{ChannelNameFrontLeft, ChannelNameFrontRight, ChannelNameEOL};
    Require(instance->ProcessInitialize(settings, 44100.0, channels), "Native AUDelay processing initialization failed");
    const auto frames = std::min<size_t>(128, instance->GetBlockSize());
    std::vector<float> input(frames), left(frames), right(frames);
    for (size_t i = 0; i < frames; ++i) { input[i] = 0.25 * std::sin(i * 0.1); }
    // AU may process the supplied input buffers in place. Compare against an
    // independent reference, and never alias the two input channels.
    const auto reference = input;
    auto secondInput = input;
    const float* ins[]{input.data(), secondInput.data()};
    float* outs[]{left.data(), right.data()};
    auto snapshot = std::make_shared<const ParameterAutomationSnapshot>(
        std::vector<ParameterAutomationSnapshot::Region>{{
            std::to_string(kDelayParam_WetDryMix), 0, 1, 0, 0, {}}});
    Require(instance->ConfigureParameterAutomation(snapshot), "Native AUDelay rejected automation binding");
    instance->SetParameterAutomationTimes(ParameterAutomationTimes{nullptr, 0, 1.0 / 44100});
    Require(instance->ProcessBlock(settings, ins, outs, frames) == frames, "Automated native AUDelay render failed");
    for (size_t i = 0; i < frames; ++i) {
        Near(left[i], reference[i]);
        Near(right[i], reference[i]);
    }
    auto wet = std::make_shared<const ParameterAutomationSnapshot>(
        std::vector<ParameterAutomationSnapshot::Region>{{
            std::to_string(kDelayParam_WetDryMix), 0, 1, 0, 1, {}}});
    Require(instance->ConfigureParameterAutomation(wet), "Native AUDelay rejected wet automation");
    double difference = 0;
    // Native units may smooth their wet/dry control across several buffers.
    // Allow bounded settling time, but still require an audible output change.
    for (size_t block = 0; block < 32; ++block) {
        std::copy(reference.begin(), reference.end(), input.begin());
        std::copy(reference.begin(), reference.end(), secondInput.begin());
        instance->SetParameterAutomationTimes(ParameterAutomationTimes{nullptr, block * frames / 44100.0, 1.0 / 44100});
        Require(instance->ProcessBlock(settings, ins, outs, frames) == frames, "Wet native AUDelay render failed");
        for (size_t i = 0; i < frames; ++i) { difference += std::abs(left[i] - reference[i]); }
    }
    Require(difference > 1, "Automation did not change the native plugin audio output");
    Require(instance->ProcessFinalize(), "Native AUDelay finalization failed");
}
#endif

void BezierClipRoundTrip()
{
    auto factory = std::make_shared<SilentRoundTripFactory>();
    auto clip = WaveClip::NewShared(1, factory, floatSample, 1000);
    clip->InsertSilence(0, 8);
    clip->SetSequenceStartTime(10);
    clip->SetTrimLeft(1); clip->SetTrimRight(1);
    auto* curve = clip->GetParameterAutomation().AddCurve(Instance, Plugin, "cutoff", .2);
    clipautomation::Curve shape;
    shape.segments.push_back({{0, .2}, {8, .8}, .75f});
    shape.BoxStep(3, 5, .1);
    curve->SetBezier(shape);
    clip->GetEnvelope().InsertOrReplace(12, .7);
    auto copied = WaveClip::NewSharedFrom(*clip, factory, true, true);
    copied->SetSequenceStartTime(20);
    Near(copied->GetParameterAutomation().Curves()[0].bezier.Evaluate(4), .1);
    Near(copied->GetPlayStartTime(), 21);
    auto split = WaveClip::NewSharedFromRange(*clip, factory, true, false, 12.0, 16.0);
    Near(split->GetParameterAutomation().Curves()[0].bezier.Evaluate(2.5), shape.Evaluate(2.5));
    for (bool binary : {false, true}) {
        auto restored = WaveClip::NewShared(1, factory, floatSample, 1000);
        if (binary) {
            ProjectSerializer serialized; clip->WriteXML(0, serialized);
            SerializedInput input(serialized);
            Require(ProjectSerializer::Decode(input, restored.get()), "Bezier binary reopen failed");
        } else {
            XMLStringWriter xml; clip->WriteXML(0, xml);
            XMLFileReader reader;
            Require(reader.ParseString(restored.get(), xml), "Bezier XML reopen failed");
        }
        Require(!restored->GetParameterAutomation().HasLoadError(), "Bezier schema failed");
        const auto& data = restored->GetParameterAutomation().Curves()[0];
        Require(!data.bezier.segments.empty(), "Curvature was lost");
        for (int i = 0; i < 80; ++i) { Near(data.bezier.Evaluate(i / 10.0), shape.Evaluate(i / 10.0)); }
        Near(restored->GetPlayStartTime(), 11); Near(restored->GetPlayEndTime(), 17);
        Near(restored->GetEnvelope().GetValue(12), .7);
    }
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    wxInitializer wx;
    if (!wx.IsOk()) {
        std::cerr << "Could not initialize the test runtime\n";
        return 1;
    }
    InitializeAutomationTestHost();
    const std::pair<const char*, std::function<void()>> tests[] = {
        { "serialization / missing plugin bindings", Serialization },
        { "validation / safe refusal", Validation },
        { "time transforms / deep snapshots", EditAndSnapshot },
        { "paste / multiple parameters", Paste },
        { "persistent effect identity / legacy loading", EffectIdentity },
        { "real WaveClip edits / volume isolation", WaveClipEdits },
        { "stereo clip XML / project binary round trip", ClipRoundTrip },
        { "Bezier clip storage / duplicate / split / volume isolation", BezierClipRoundTrip },
        { "cut restoration / tempo / sample-rate changes", CutRestoreAndTempo },
        { "immutable evaluator / loop / reverse / overlap", SnapshotEvaluation },
        { "stepped switches / hold until next point / saved", SteppedSwitches },
        { "deterministic audio / processing block consistency", ProcessingBlockConsistency },
        { "serial plugin latency / loop timestamps / capacity guard", LatencyTiming },
#ifdef MIXHOUSE_TEST_AUDIO_UNIT
        { "Apple AUDelay / native automation / rendered audio", NativeAudioUnitAutomation },
#endif
#ifdef MIXHOUSE_TEST_VST3
        { "native VST3 queue / offsets / baseline / stepped values", Vst3ParameterQueue },
#endif
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try { run(); std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
