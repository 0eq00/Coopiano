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

    if (activeRuntime_)
        activeRuntime_->reset();

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
    const auto cfg = coopiano::params::readConfig(apvts_);
    const double clampedRate = std::clamp(currentSampleRate_, 8000.0, 192000.0);

    auto fresh = std::make_unique<room::StereoReverb>(clampedRate, cfg.preset.reverb);
    auto* raw = fresh.get();
    if (auto* displaced = pendingReverb_.exchange(raw, std::memory_order_acq_rel))
    {
        // Keep displaced until retired cleanup
        juce::ignoreUnused(displaced);
    }
    retiredReverbs_.push_back(std::move(fresh));

    const uint64_t epochNow = audioReverbEpoch_.load(std::memory_order_acquire);
    if (retiredReverbs_.size() > 2 && epochNow > lastReverbRetiredEpoch_ + 2)
    {
        // Keep the latest two instances (pending + currently active)
        retiredReverbs_.erase(retiredReverbs_.begin(), retiredReverbs_.end() - 2);
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

    // Mix on-screen / computer keyboard MIDI into the host MIDI buffer
    keyboardState_.processNextMidiBuffer(midiMessages, 0, numSamples, true);

    // Wait-free swap to newly constructed EngineRuntime when ready
    auto* nextRuntime = builder_.pollForAudioThread(activeRuntime_);
    if (nextRuntime != activeRuntime_)
    {
        activeRuntime_ = nextRuntime;
        if (activeRuntime_)
        {
            const int reported = static_cast<int>(activeRuntime_->latencySamples());
            currentLatency_.store(reported, std::memory_order_relaxed);
            setLatencySamples(reported);
        }
    }

    // Wait-free swap to updated Reverb when ready
    audioReverbEpoch_.fetch_add(1, std::memory_order_acq_rel);
    if (auto* nextRev = pendingReverb_.exchange(nullptr, std::memory_order_acq_rel))
    {
        activeReverb_.release(); // owned by retiredReverbs_ on message thread
        activeReverb_.reset(nextRev);
        lastReverbRetiredEpoch_ = audioReverbEpoch_.load(std::memory_order_relaxed);
    }

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    if (!activeRuntime_)
    {
        buffer.clear();
        return;
    }

    // Convert JUCE MidiBuffer to pre-allocated instrument::MidiEvent sequence
    midiEvents_.clear();
    for (const auto metadata : midiMessages)
    {
        if (midiEvents_.size() >= coopiano::EngineRuntime::kMaxEventsPerBlock)
            break;

        const auto& msg = metadata.getMessage();
        const int offset = std::clamp(metadata.samplePosition, 0, numSamples);
        const int ch = std::clamp(msg.getChannel() - 1, 0, 15);

        if (msg.isNoteOn())
        {
            instrument::MidiEvent ev;
            ev.offset = static_cast<size_t>(offset);
            ev.type = instrument::MidiEvent::Type::note_on;
            ev.key = msg.getNoteNumber();
            ev.velocity = std::clamp(static_cast<int>(msg.getVelocity()), 0, 127);
            ev.channel = ch;
            ev.seed = noteSeedCounter_++;
            midiEvents_.push_back(ev);
        }
        else if (msg.isNoteOff())
        {
            instrument::MidiEvent ev;
            ev.offset = static_cast<size_t>(offset);
            ev.type = instrument::MidiEvent::Type::note_off;
            ev.key = msg.getNoteNumber();
            ev.channel = ch;
            midiEvents_.push_back(ev);
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            if (cc == 64) // Sustain / Damper (supports continuous half-pedaling)
            {
                instrument::MidiEvent ev;
                ev.offset = static_cast<size_t>(offset);
                ev.type = instrument::MidiEvent::Type::pedal;
                ev.channel = ch;
                ev.position = std::clamp(msg.getControllerValue() / 127.0, 0.0, 1.0);
                ev.down = msg.getControllerValue() >= 64;
                midiEvents_.push_back(ev);
            }
            else if (cc == 120 || cc == 123) // All Sound Off / All Notes Off
            {
                for (int k = 21; k <= 108 && midiEvents_.size() < coopiano::EngineRuntime::kMaxEventsPerBlock; ++k)
                {
                    instrument::MidiEvent ev;
                    ev.offset = static_cast<size_t>(offset);
                    ev.type = instrument::MidiEvent::Type::note_off;
                    ev.key = k;
                    ev.channel = ch;
                    midiEvents_.push_back(ev);
                }
            }
        }
    }

    if (!activeRuntime_->process(left, right, static_cast<size_t>(numSamples),
                                 midiEvents_.data(), midiEvents_.size()))
    {
        buffer.clear();
        return;
    }

    // Apply independent host-rate StereoReverb (double precision)
    if (auto* rev = activeReverb_.get())
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
