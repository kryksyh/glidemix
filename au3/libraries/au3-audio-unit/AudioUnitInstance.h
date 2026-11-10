/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file AudioUnitInstance.h

  Dominic Mazzoni
  Leland Lucius

**********************************************************************/

#ifndef __AUDACITY_AUDIO_UNIT_INSTANCE__
#define __AUDACITY_AUDIO_UNIT_INSTANCE__

#include "au3-effects/PerTrackEffect.h"
#include <atomic>

#include "AudioUnitWrapper.h"
struct AudioUnitEvent;

class AudioUnitInstance : public PerTrackEffect::Instance, public AudioUnitWrapper
{
public:
    using Instance::mProcessor;

    AudioUnitInstance(const PerTrackEffect& effect, AudioComponent component, Parameters& parameters, const wxString& identifier,
                      unsigned audioIns, unsigned audioOuts, bool useLatency);
    ~AudioUnitInstance() override;

    bool Initialize();

    bool IsInitialized() const { return static_cast<bool>(mInitialization); }

    bool ConfigureParameterAutomation(std::shared_ptr<const ParameterAutomationSnapshot> snapshot) override;
    bool SupportsParameterAutomation() const noexcept override { return true; }
    std::vector<AutomationParameter> AutomationParameters() const override;

    void EventListener(const AudioUnitEvent* inEvent, AudioUnitParameterValue inParameterValue);

    // Override the virtual function to allocate an empty message
    std::unique_ptr<Message> MakeMessage() const override;

    // A non-virtual overload makes a non-empty message
    std::unique_ptr<Message>
    MakeMessage(AudioUnitParameterID id, AudioUnitParameterValue value) const;

private:
    size_t InitialBlockSize() const;
    SampleCount GetLatency(const EffectSettings& settings, double sampleRate)
    const override;

    size_t GetBlockSize() const override;
    size_t SetBlockSize(size_t maxBlockSize) override;

    unsigned GetAudioInCount() const override;
    unsigned GetAudioOutCount() const override;

    bool ProcessInitialize(EffectSettings& settings, double sampleRate, ChannelNames chanMap) override;
    bool ProcessFinalize() noexcept override;
    std::string GetLastError() const override;
    size_t ProcessBlock(EffectSettings& settings, const float* const* inBlock, float* const* outBlock, size_t blockLen)
    override;

    bool RealtimeInitialize(EffectSettings& settings, double sampleRate, size_t audioThreadBufferSize)
    override;
    bool RealtimeAddProcessor(EffectSettings& settings, EffectOutputs* pOutputs, unsigned numChannels, float sampleRate) override;
    bool RealtimeFinalize(EffectSettings& settings) noexcept override;
    bool RealtimeSuspend() override;
    bool RealtimeResume() override;

    bool UsesMessages() const noexcept override;
    bool RealtimeProcessStart(MessagePackage& package) override;
    size_t RealtimeProcess(size_t group, EffectSettings& settings, const float* const* inbuf, float* const* outbuf, size_t numSamples)
    override;
    bool RealtimeProcessEnd(EffectSettings& settings) noexcept override;

    static OSStatus RenderCallback(void* inRefCon, AudioUnitRenderActionFlags* inActionFlags, const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber, UInt32 inNumFrames, AudioBufferList* ioData);
    OSStatus Render(AudioUnitRenderActionFlags* inActionFlags, const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber, UInt32 inNumFrames,
                    AudioBufferList* ioData);

    bool BypassEffect(bool bypass);

    //! GlideMix: the host's tempo and transport, asked for by tempo-synced
    //! plug-ins during AudioUnitRender (same thread as ProcessBlock).
    static OSStatus HostBeatAndTempo(void* userData, Float64* outCurrentBeat, Float64* outCurrentTempo);
    static OSStatus HostMusicalTimeLocation(void* userData, UInt32* outDeltaSampleOffsetToNextBeat,
                                            Float32* outTimeSigNumerator, UInt32* outTimeSigDenominator,
                                            Float64* outCurrentMeasureDownBeat);
    static OSStatus HostTransportState(void* userData, Boolean* outIsPlaying, Boolean* outTransportStateChanged,
                                       Float64* outCurrentSampleInTimeLine, Boolean* outIsCycling,
                                       Float64* outCycleStartBeat, Float64* outCycleEndBeat);
    static OSStatus HostTransportState2(void* userData, Boolean* outIsPlaying, Boolean* outIsRecording,
                                        Boolean* outTransportStateChanged, Float64* outCurrentSampleInTimeLine,
                                        Boolean* outIsCycling, Float64* outCycleStartBeat, Float64* outCycleEndBeat);
    bool HostMusicalTime(double& bpm, double& beats) const;
    bool mHostTimeValid = false;
    bool mHostWasPlaying = false;
    double mHostTimelineTime = 0.0;

    static OSStatus AutomationRenderNotify(void*, AudioUnitRenderActionFlags*,
        const AudioTimeStamp*, UInt32, UInt32, AudioBufferList*);
    bool PrepareAutomationEvents(size_t frames);
    bool RestoreAutomationBaseline() noexcept;

private:
    //! Whether the master instance is now allocated to a group number
    bool mRecruited{ false };
    std::vector<std::unique_ptr<AudioUnitInstance> > mSlaves;

    AudioUnitCleanup<AudioUnit, AudioUnitUninitialize> mInitialization;
    AudioTimeStamp mTimeStamp{};
    PackedArray::Ptr<AudioBufferList> mInputList;
    PackedArray::Ptr<AudioBufferList> mOutputList;

    const wxString& mIdentifier; // for debug messages only
    const size_t mBlockSize;
    const bool mUseLatency;
    double mInitializedSampleRate = 0.0;
    std::string mLastError;
    std::atomic<bool> mRealtimeErrorReported{ false };
public:
    //! GlideMix: whether a curve drives this parameter now (UI thread, where
    //! automation is configured), so the editor can tell playback from the user.
    bool IsAutomatedParameter(AudioUnitParameterID id) const
    {
        for (const auto& b : mAutomationBindings) {
            if (b.id == id) { return true; }
        }
        return false;
    }
    //! GlideMix: counts the times the host itself reset automated parameters
    //! (RestoreAutomationBaseline), so an editor polling values doesn't take
    //! those changes for the user's.
    uint64_t HostParameterWrites() const noexcept { return mHostParameterWrites.load(std::memory_order_acquire); }
private:
    std::atomic<uint64_t> mHostParameterWrites{0};
    struct AutomationBinding {
        AudioUnitParameterID id{};
        size_t parameter{};
        AudioUnitParameterValue minimum{}, maximum{}, baseline{};
        bool discrete{};
    };
    std::shared_ptr<const ParameterAutomationSnapshot> mAutomation;
    std::vector<AutomationBinding> mAutomationBindings;
    std::vector<AudioUnitParameterEvent> mAutomationEvents;
    UInt32 mAutomationEventCount{};
    bool mAutomationNotifyRegistered{};
};
#endif
