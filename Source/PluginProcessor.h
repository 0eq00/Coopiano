#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "CoopianoParameters.h"
#include "EngineRuntime.h"
#include "coopiano/reverb.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class CoopianoAudioProcessor final
    : public juce::AudioProcessor
    , private juce::AudioProcessorValueTreeState::Listener
    , private juce::AsyncUpdater
{
public:
    CoopianoAudioProcessor();
    ~CoopianoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 5; }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }
    juce::MidiKeyboardState& keyboardState() noexcept { return keyboardState_; }

    // UI pedal controls (merged safely with host MIDI CC64/66/67 on audio thread)
    void setUiSustain(bool down) noexcept { uiSustain_.store(down, std::memory_order_release); }
    void setUiSostenuto(bool down) noexcept { uiSostenuto_.store(down, std::memory_order_release); }
    void setUiSoftPedal(bool down) noexcept { uiSoft_.store(down, std::memory_order_release); }

    bool isSustainDown() const noexcept { return uiSustain_.load(std::memory_order_acquire) || midiSustainIndicator_.load(std::memory_order_relaxed); }
    bool isSostenutoDown() const noexcept { return uiSostenuto_.load(std::memory_order_acquire) || midiSostenutoIndicator_.load(std::memory_order_relaxed); }
    bool isSoftPedalDown() const noexcept { return uiSoft_.load(std::memory_order_acquire) || midiSoftIndicator_.load(std::memory_order_relaxed); }

    // Safe telemetry for UI
    bool isEngineBuilding() const noexcept { return builder_.isBuilding(); }
    bool isEngineReady() const noexcept { return builder_.isReady(); }
    int activeVoices() const noexcept { return activeVoices_.load(std::memory_order_relaxed); }
    int peakVoices() const noexcept { return peakVoices_.load(std::memory_order_relaxed); }
    int latencySamplesReported() const noexcept { return currentLatency_.load(std::memory_order_relaxed); }
    float outputPeakL() const noexcept { return peakLevelL_.load(std::memory_order_relaxed); }
    float outputPeakR() const noexcept { return peakLevelR_.load(std::memory_order_relaxed); }

    bool loadPresetFromFile(const juce::File& iniFile, juce::String& errorMessage);
    bool savePresetToFile(const juce::File& iniFile, juce::String& errorMessage) const;

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void rebuildReverb();
    void resetPedalState() noexcept;

    void handleNoteOnEvent(size_t offset, int ch, int key, int vel) noexcept;
    void handleNoteOffEvent(size_t offset, int ch, int key) noexcept;
    void handleSostenutoEvent(size_t offset, int ch, bool down) noexcept;
    void handleAllNotesOffEvent(size_t offset, int ch) noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    juce::MidiKeyboardState keyboardState_;
    coopiano::EngineBuilderThread builder_;

    coopiano::EngineRuntime* activeRuntime_ = nullptr;
    double currentSampleRate_ = 44100.0;
    int currentBlockSize_ = 512;
    int currentProgram_ = 0;

    std::atomic<bool> engineRebuildPending_{false};
    std::atomic<bool> reverbRebuildPending_{false};

    // Host-rate StereoReverb with wait-free replacement
    std::unique_ptr<room::StereoReverb> activeReverb_;
    std::vector<std::unique_ptr<room::StereoReverb>> retiredReverbs_;
    std::atomic<room::StereoReverb*> pendingReverb_{nullptr};
    std::atomic<uint64_t> audioReverbEpoch_{0};
    uint64_t lastReverbRetiredEpoch_ = 0;

    // Pre-allocated audio buffers (resized only in prepareToPlay)
    std::vector<instrument::MidiEvent> midiEvents_;
    std::vector<double> reverbScratchL_;
    std::vector<double> reverbScratchR_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain_;
    uint64_t noteSeedCounter_ = 1;

    // Per-channel key & pedal state for Sostenuto (CC66) and Soft Pedal (CC67)
    std::array<std::array<bool, 128>, 16> heldKeys_{};
    std::array<std::array<bool, 128>, 16> sostenutoLatched_{};
    std::array<std::array<bool, 128>, 16> pendingRelease_{};
    std::array<bool, 16> sostenutoDown_{};
    std::array<double, 16> softPedalPos_{};

    // UI pedal state & edge tracking (audio thread)
    std::atomic<bool> uiSustain_{false};
    std::atomic<bool> uiSostenuto_{false};
    std::atomic<bool> uiSoft_{false};
    bool lastUiSustain_ = false;
    bool lastUiSostenuto_ = false;
    std::atomic<bool> midiSustainIndicator_{false};
    std::atomic<bool> midiSostenutoIndicator_{false};
    std::atomic<bool> midiSoftIndicator_{false};

    // Una Corda (soft pedal) smoothed warm tone filter state
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> softAmountSmoothed_;
    float softLpCoeff_ = 0.25f;
    float softLpStateL_ = 0.0f;
    float softLpStateR_ = 0.0f;

    std::atomic<int> activeVoices_{0};
    std::atomic<int> peakVoices_{0};
    std::atomic<int> currentLatency_{0};
    std::atomic<float> peakLevelL_{0.0f};
    std::atomic<float> peakLevelR_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoopianoAudioProcessor)
};
