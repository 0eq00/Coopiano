#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

CoopianoAudioProcessor::CoopianoAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "CoopianoState", coopiano::params::createParameterLayout())
{
    for (auto* param : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            apvts_.addParameterListener(ranged->getParameterID(), this);
    }
}

CoopianoAudioProcessor::~CoopianoAudioProcessor()
{
    cancelPendingUpdate();
    for (auto* param : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            apvts_.removeParameterListener(ranged->getParameterID(), this);
    }
}

bool CoopianoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CoopianoAudioProcessor::resetPedalState() noexcept
{
    for (auto& ch : heldKeys_) ch.fill(false);
    for (auto& ch : sostenutoLatched_) ch.fill(false);
    for (auto& ch : pendingRelease_) ch.fill(false);
    sostenutoDown_.fill(false);
    softPedalPos_.fill(0.0);
    lastUiSustain_ = false;
    lastUiSostenuto_ = false;
    midiSustainIndicator_.store(false, std::memory_order_relaxed);
    midiSostenutoIndicator_.store(false, std::memory_order_relaxed);
    midiSoftIndicator_.store(false, std::memory_order_relaxed);
    softLpStateL_ = 0.0f;
    softLpStateR_ = 0.0f;
}

void CoopianoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = std::max(8000.0, sampleRate);
    currentBlockSize_ = std::max(64, samplesPerBlock);

    midiEvents_.reserve(coopiano::EngineRuntime::kMaxEventsPerBlock);
    reverbScratchL_.assign(static_cast<size_t>(currentBlockSize_), 0.0);
    reverbScratchR_.assign(static_cast<size_t>(currentBlockSize_), 0.0);

    outputGain_.reset(currentSampleRate_, 0.02);
    const auto cfg = coopiano::params::readConfig(apvts_);
    outputGain_.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(static_cast<float>(cfg.outputGainDb)));

    softAmountSmoothed_.reset(currentSampleRate_, 0.025);
    softAmountSmoothed_.setCurrentAndTargetValue(0.0f);
    softLpCoeff_ = std::exp(-2.0f * juce::MathConstants<float>::pi * 2200.0f / static_cast<float>(currentSampleRate_));

    if (activeRuntime_)
        activeRuntime_->reset();

    resetPedalState();
    rebuildReverb();
    builder_.requestBuild(cfg, currentSampleRate_, currentBlockSize_, true);
}

void CoopianoAudioProcessor::releaseResources()
{
    if (activeRuntime_)
        activeRuntime_->reset();
    if (activeReverb_)
        activeReverb_->reset();
    keyboardState_.reset();
    resetPedalState();
}

void CoopianoAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == coopiano::params::outGainDb)
    {
        outputGain_.setTargetValue(juce::Decibels::decibelsToGain(newValue));
        return;
    }

    if (coopiano::params::isInstantOnlyParam(parameterID))
    {
        reverbRebuildPending_.store(true, std::memory_order_release);
    }
    else
    {
        engineRebuildPending_.store(true, std::memory_order_release);
    }
    triggerAsyncUpdate();
}

void CoopianoAudioProcessor::handleAsyncUpdate()
{
    if (reverbRebuildPending_.exchange(false, std::memory_order_acq_rel))
        rebuildReverb();

    if (engineRebuildPending_.exchange(false, std::memory_order_acq_rel))
    {
        const auto cfg = coopiano::params::readConfig(apvts_);
        builder_.requestBuild(cfg, currentSampleRate_, currentBlockSize_, false);
    }
}

void CoopianoAudioProcessor::rebuildReverb()
{
    if (retiredReverbs_.size() > 1 && pendingReverb_.load(std::memory_order_acquire) == nullptr)
    {
        const uint64_t epochNow = audioReverbEpoch_.load(std::memory_order_acquire);
        const uint64_t adoptedEpoch = lastReverbAdoptedEpoch_.load(std::memory_order_acquire);
        if (epochNow > adoptedEpoch + 1)
        {
            retiredReverbs_.erase(retiredReverbs_.begin(), retiredReverbs_.end() - 1);
        }
    }

    const auto cfg = coopiano::params::readConfig(apvts_);
    const double clampedRate = std::clamp(currentSampleRate_, 8000.0, 192000.0);

    auto fresh = std::make_unique<room::StereoReverb>(clampedRate, cfg.preset.reverb);
    auto* raw = fresh.get();
    retiredReverbs_.push_back(std::move(fresh));

    if (auto* displaced = pendingReverb_.exchange(raw, std::memory_order_acq_rel))
    {
        retiredReverbs_.erase(
            std::remove_if(retiredReverbs_.begin(), retiredReverbs_.end(), [displaced](const auto& entry) {
                return entry.get() == displaced;
            }),
            retiredReverbs_.end());
    }
}

void CoopianoAudioProcessor::handleNoteOnEvent(size_t offset, int ch, int key, int vel) noexcept
{
    if (midiEvents_.size() >= coopiano::EngineRuntime::kMaxEventsPerBlock)
        return;
    if (key < 0 || key > 127 || ch < 0 || ch > 15)
        return;

    heldKeys_[static_cast<size_t>(ch)][static_cast<size_t>(key)] = true;
    pendingRelease_[static_cast<size_t>(ch)][static_cast<size_t>(key)] = false;

    instrument::MidiEvent ev;
    ev.offset = offset;
    ev.type = instrument::MidiEvent::Type::note_on;
    ev.key = key;
    ev.velocity = std::clamp(vel, 0, 127);
    ev.channel = ch;
    ev.seed = noteSeedCounter_++;
    midiEvents_.push_back(ev);
}

void CoopianoAudioProcessor::handleNoteOffEvent(size_t offset, int ch, int key) noexcept
{
    if (key < 0 || key > 127 || ch < 0 || ch > 15)
        return;

    const size_t c = static_cast<size_t>(ch);
    const size_t k = static_cast<size_t>(key);
    heldKeys_[c][k] = false;

    // If Sostenuto captured this key when pressed, hold its damper open until Sostenuto releases.
    if (sostenutoDown_[c] && sostenutoLatched_[c][k])
    {
        pendingRelease_[c][k] = true;
        return;
    }

    if (midiEvents_.size() >= coopiano::EngineRuntime::kMaxEventsPerBlock)
        return;

    instrument::MidiEvent ev;
    ev.offset = offset;
    ev.type = instrument::MidiEvent::Type::note_off;
    ev.key = key;
    ev.channel = ch;
    midiEvents_.push_back(ev);
}

void CoopianoAudioProcessor::handleSostenutoEvent(size_t offset, int ch, bool down) noexcept
{
    if (ch < 0 || ch > 15)
        return;

    const size_t c = static_cast<size_t>(ch);
    const bool wasDown = sostenutoDown_[c];
    sostenutoDown_[c] = down;

    if (!wasDown && down)
    {
        // Latch only keys that are physically held at the exact instant Sostenuto engages.
        for (size_t k = 0; k < 128; ++k)
        {
            sostenutoLatched_[c][k] = heldKeys_[c][k];
            pendingRelease_[c][k] = false;
        }
    }
    else if (wasDown && !down)
    {
        // Release any latched keys whose physical note_off already occurred.
        for (size_t k = 0; k < 128; ++k)
        {
            const bool shouldRelease = sostenutoLatched_[c][k] && pendingRelease_[c][k];
            sostenutoLatched_[c][k] = false;
            pendingRelease_[c][k] = false;

            if (shouldRelease && midiEvents_.size() < coopiano::EngineRuntime::kMaxEventsPerBlock)
            {
                instrument::MidiEvent ev;
                ev.offset = offset;
                ev.type = instrument::MidiEvent::Type::note_off;
                ev.key = static_cast<int>(k);
                ev.channel = ch;
                midiEvents_.push_back(ev);
            }
        }
    }
}

void CoopianoAudioProcessor::handleAllNotesOffEvent(size_t offset, int ch) noexcept
{
    if (ch < 0 || ch > 15)
        return;

    const size_t c = static_cast<size_t>(ch);
    heldKeys_[c].fill(false);
    sostenutoLatched_[c].fill(false);
    pendingRelease_[c].fill(false);

    for (int k = 21; k <= 108 && midiEvents_.size() < coopiano::EngineRuntime::kMaxEventsPerBlock; ++k)
    {
        instrument::MidiEvent ev;
        ev.offset = offset;
        ev.type = instrument::MidiEvent::Type::note_off;
        ev.key = k;
        ev.channel = ch;
        midiEvents_.push_back(ev);
    }
}

void CoopianoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // Mix on-screen / computer keyboard MIDI notes into the host MIDI buffer
    keyboardState_.processNextMidiBuffer(midiMessages, 0, numSamples, true);

    // Wait-free swap to newly constructed EngineRuntime when ready
    auto* nextRuntime = builder_.pollForAudioThread(activeRuntime_);
    if (nextRuntime != activeRuntime_)
    {
        activeRuntime_ = nextRuntime;
        resetPedalState();
        if (activeRuntime_)
        {
            const int reported = static_cast<int>(activeRuntime_->latencySamples());
            currentLatency_.store(reported, std::memory_order_relaxed);
            setLatencySamples(reported);
        }
    }

    // Wait-free swap to updated Reverb when ready
    const uint64_t currentRevEpoch = audioReverbEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (auto* nextRev = pendingReverb_.exchange(nullptr, std::memory_order_acq_rel))
    {
        activeReverb_ = nextRev;
        lastReverbAdoptedEpoch_.store(currentRevEpoch, std::memory_order_release);
    }

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    if (!activeRuntime_)
    {
        buffer.clear();
        return;
    }

    midiEvents_.clear();

    // Inject UI pedal transitions cleanly at sample offset 0 (preserves monotonic offset order)
    const bool uiSus = uiSustain_.load(std::memory_order_acquire);
    if (uiSus != lastUiSustain_)
    {
        lastUiSustain_ = uiSus;
        instrument::MidiEvent ev;
        ev.offset = 0;
        ev.type = instrument::MidiEvent::Type::pedal;
        ev.channel = 0;
        ev.position = uiSus ? 1.0 : 0.0;
        ev.down = uiSus;
        midiEvents_.push_back(ev);
    }

    const bool uiSost = uiSostenuto_.load(std::memory_order_acquire);
    if (uiSost != lastUiSostenuto_)
    {
        lastUiSostenuto_ = uiSost;
        handleSostenutoEvent(0, 0, uiSost);
    }

    const bool uiSoftActive = uiSoft_.load(std::memory_order_acquire);

    // Convert JUCE MidiBuffer to pre-allocated instrument::MidiEvent sequence
    for (const auto metadata : midiMessages)
    {
        if (midiEvents_.size() >= coopiano::EngineRuntime::kMaxEventsPerBlock)
            break;

        const auto& msg = metadata.getMessage();
        const size_t offset = static_cast<size_t>(std::clamp(metadata.samplePosition, 0, numSamples));
        const int ch = std::clamp(msg.getChannel() - 1, 0, 15);

        if (msg.isNoteOn())
        {
            const double soft = std::max(softPedalPos_[static_cast<size_t>(ch)], uiSoftActive ? 1.0 : 0.0);
            const int scaledVel = std::clamp(
                static_cast<int>(std::lround(static_cast<double>(msg.getVelocity()) * (1.0 - 0.24 * soft))),
                1,
                127);
            handleNoteOnEvent(offset, ch, msg.getNoteNumber(), scaledVel);
        }
        else if (msg.isNoteOff())
        {
            handleNoteOffEvent(offset, ch, msg.getNoteNumber());
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            if (cc == 64) // Sustain / Damper (continuous half-pedal)
            {
                const double pos = std::clamp(msg.getControllerValue() / 127.0, 0.0, 1.0);
                midiSustainIndicator_.store(msg.getControllerValue() >= 64, std::memory_order_relaxed);
                instrument::MidiEvent ev;
                ev.offset = offset;
                ev.type = instrument::MidiEvent::Type::pedal;
                ev.channel = ch;
                ev.position = (ch == 0 && uiSus) ? 1.0 : pos;
                ev.down = ev.position >= 0.5;
                midiEvents_.push_back(ev);
            }
            else if (cc == 66) // Sostenuto Pedal
            {
                const bool down = msg.getControllerValue() >= 64;
                midiSostenutoIndicator_.store(down, std::memory_order_relaxed);
                handleSostenutoEvent(offset, ch, down || (ch == 0 && uiSost));
            }
            else if (cc == 67) // Soft Pedal / Una Corda
            {
                softPedalPos_[static_cast<size_t>(ch)] = std::clamp(msg.getControllerValue() / 127.0, 0.0, 1.0);
                midiSoftIndicator_.store(msg.getControllerValue() >= 64, std::memory_order_relaxed);
            }
            else if (cc == 120 || cc == 123) // All Sound Off / All Notes Off
            {
                handleAllNotesOffEvent(offset, ch);
            }
        }
    }

    if (!activeRuntime_->process(left, right, static_cast<size_t>(numSamples),
                                 midiEvents_.data(), midiEvents_.size()))
    {
        buffer.clear();
        return;
    }

    // Apply Una Corda (soft pedal) warm spectral tilt prior to room reverb
    double maxSoft = uiSoftActive ? 1.0 : 0.0;
    for (double p : softPedalPos_)
        maxSoft = std::max(maxSoft, p);
    softAmountSmoothed_.setTargetValue(static_cast<float>(maxSoft));

    if (softAmountSmoothed_.isSmoothing() || softAmountSmoothed_.getTargetValue() > 0.0001f)
    {
        const float oneMinusC = 1.0f - softLpCoeff_;
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = softAmountSmoothed_.getNextValue();
            const float k = s * 0.34f;
            softLpStateL_ = oneMinusC * left[i] + softLpCoeff_ * softLpStateL_;
            softLpStateR_ = oneMinusC * right[i] + softLpCoeff_ * softLpStateR_;
            left[i] = left[i] * (1.0f - k) + softLpStateL_ * k;
            right[i] = right[i] * (1.0f - k) + softLpStateR_ * k;
        }
    }
    else
    {
        // Keep lowpass state silently primed with zero overhead during full bypass
        softLpStateL_ = left[numSamples - 1];
        softLpStateR_ = right[numSamples - 1];
    }

    // Apply independent host-rate StereoReverb (double precision)
    if (auto* rev = activeReverb_)
    {
        if (rev->options().mix > 0.0 && static_cast<size_t>(numSamples) <= reverbScratchL_.size())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                reverbScratchL_[static_cast<size_t>(i)] = static_cast<double>(left[i]);
                reverbScratchR_[static_cast<size_t>(i)] = static_cast<double>(right[i]);
            }
            rev->process(reverbScratchL_.data(), reverbScratchR_.data(), static_cast<size_t>(numSamples));
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] = static_cast<float>(reverbScratchL_[static_cast<size_t>(i)]);
                right[i] = static_cast<float>(reverbScratchR_[static_cast<size_t>(i)]);
            }
        }
    }

    // Apply smoothed master output gain + meter update
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain_.getNextValue();
        left[i] *= g;
        right[i] *= g;
        peakL = std::max(peakL, std::abs(left[i]));
        peakR = std::max(peakR, std::abs(right[i]));
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    activeVoices_.store(static_cast<int>(activeRuntime_->activeVoices()), std::memory_order_relaxed);
    peakVoices_.store(static_cast<int>(activeRuntime_->peakVoices()), std::memory_order_relaxed);
    peakLevelL_.store(peakL, std::memory_order_relaxed);
    peakLevelR_.store(peakR, std::memory_order_relaxed);
}

void CoopianoAudioProcessor::setCurrentProgram(int index)
{
    currentProgram_ = std::clamp(index, 0, getNumPrograms() - 1);
    const auto preset = coopiano::params::makeFactoryPreset(currentProgram_);
    coopiano::params::applyPreset(apvts_, preset);
}

const juce::String CoopianoAudioProcessor::getProgramName(int index)
{
    switch (index)
    {
        case 0: return "Calibrated Concert";
        case 1: return "Intimate Felt";
        case 2: return "Bright Concert Stage";
        case 3: return "Dry Studio Upright";
        case 4: return "Ethereal Sympathetic";
        default: return "Calibrated Concert";
    }
}

void CoopianoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void CoopianoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xmlState));
}

bool CoopianoAudioProcessor::loadPresetFromFile(const juce::File& iniFile, juce::String& errorMessage)
{
    try
    {
        const auto p = preset::load(iniFile.getFullPathName().toStdString());
        coopiano::params::applyPreset(apvts_, p);
        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = e.what();
        return false;
    }
}

bool CoopianoAudioProcessor::savePresetToFile(const juce::File& iniFile, juce::String& errorMessage) const
{
    try
    {
        const auto cfg = coopiano::params::readConfig(apvts_);
        std::ostringstream oss;
        preset::write(oss, cfg.preset);
        if (!iniFile.replaceWithText(oss.str()))
        {
            errorMessage = "Could not write preset file.";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = e.what();
        return false;
    }
}

juce::AudioProcessorEditor* CoopianoAudioProcessor::createEditor()
{
    return new CoopianoAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CoopianoAudioProcessor();
}
