#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "CoopianoParameters.h"
#include "Resampler.h"
#include "coopiano/instrument.hpp"
#include "coopiano/piano_host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace coopiano
{
    // Immutable audio runtime constructed off the audio thread.
    // Declares engine_ before adapter_ so adapter_ is destroyed first.
    class EngineRuntime
    {
    public:
        static constexpr size_t kMaxAdapterBlock = 4096;
        static constexpr size_t kMaxEventsPerBlock = 2048;

        static std::unique_ptr<EngineRuntime> build(
            const params::EngineConfig& config,
            double hostSampleRate,
            int maxHostBlockSize,
            const std::atomic<bool>* cancelFlag = nullptr,
            bool editMode = false)
        {
            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                return nullptr;

            auto rt = std::unique_ptr<EngineRuntime>(new EngineRuntime(config, hostSampleRate, maxHostBlockSize));

            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                return nullptr;

            rt->engine_->build_strike_cache(config.velocityGamma, cancelFlag, editMode);

            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                return nullptr;

            rt->adapter_->prepare(rt->adapterRate_, rt->adapterMaxBlock_, kMaxEventsPerBlock);
            return rt;
        }

        void reset() noexcept
        {
            if (adapter_)
                adapter_->reset();
            primeFallbackResampler();
            stagedEvents_.clear();
        }

        size_t latencySamples() const noexcept
        {
            if (directHostRate_ && adapter_)
                return adapter_->latency_samples();
            // Linear-phase causal group delay of Resampler (HALF_TAPS at 44.1 kHz)
            return static_cast<size_t>(std::llround(
                static_cast<double>(Resampler::HALF_TAPS) * (hostRate_ / adapterRate_)));
        }

        size_t activeVoices() const noexcept { return engine_ ? engine_->active_voices() : 0; }
        size_t peakVoices() const noexcept { return engine_ ? engine_->peak_voices() : 0; }
        uint64_t droppedNotes() const noexcept { return engine_ ? engine_->dropped_notes() : 0; }
        uint64_t stolenVoices() const noexcept { return engine_ ? engine_->stolen_voices() : 0; }

        // Audio thread only: zero allocations, locks, or exceptions.
        bool process(float* left,
                     float* right,
                     size_t numSamples,
                     const instrument::MidiEvent* events,
                     size_t eventCount) noexcept
        {
            if (!adapter_ || !left || !right)
                return false;

            if (numSamples == 0)
                return adapter_->process(left, right, 0, events, eventCount);

            if (directHostRate_ && numSamples <= adapterMaxBlock_)
                return adapter_->process(left, right, numSamples, events, eventCount);

            // Chunked direct path for oversized host blocks (> 4096 frames).
            if (directHostRate_)
            {
                size_t offset = 0;
                size_t eventIdx = 0;
                while (offset < numSamples)
                {
                    const size_t chunk = std::min(adapterMaxBlock_, numSamples - offset);
                    stagedEvents_.clear();
                    while (eventIdx < eventCount && events[eventIdx].offset <= offset + chunk)
                    {
                        if (stagedEvents_.size() < kMaxEventsPerBlock)
                        {
                            auto ev = events[eventIdx];
                            ev.offset = (ev.offset >= offset) ? (ev.offset - offset) : 0;
                            stagedEvents_.push_back(ev);
                        }
                        ++eventIdx;
                    }
                    if (!adapter_->process(left + offset, right + offset, chunk,
                                           stagedEvents_.data(), stagedEvents_.size()))
                        return false;
                    offset += chunk;
                }
                return true;
            }

            // Band-limited Kaiser windowed-sinc resampling (Resampler.h) for
            // arbitrary host sample rates (e.g. 176.4 / 192 / 32 kHz).
            const double ratio = adapterRate_ / hostRate_;
            size_t hostOffset = 0;
            size_t eventIdx = 0;

            while (hostOffset < numSamples)
            {
                const size_t chunk = std::min(fallbackHostChunk_, numSamples - hostOffset);
                const size_t desiredAvail = std::max(
                    carryCount_ + 1,
                    static_cast<size_t>(std::ceil(static_cast<double>(chunk) * ratio)) + 4);
                const size_t targetAvail = std::min(nativeScratchL_.size(), desiredAvail);
                const size_t renderCount = (targetAvail > carryCount_) ? (targetAvail - carryCount_) : 0;

                for (size_t i = 0; i < carryCount_; ++i)
                {
                    nativeScratchL_[i] = carryL_[i];
                    nativeScratchR_[i] = carryR_[i];
                }

                stagedEvents_.clear();
                while (eventIdx < eventCount && events[eventIdx].offset <= hostOffset + chunk)
                {
                    if (stagedEvents_.size() < kMaxEventsPerBlock)
                    {
                        auto ev = events[eventIdx];
                        const size_t relHost = (ev.offset >= hostOffset) ? (ev.offset - hostOffset) : 0;
                        ev.offset = std::min(renderCount, static_cast<size_t>(std::llround(static_cast<double>(relHost) * ratio)));
                        stagedEvents_.push_back(ev);
                    }
                    ++eventIdx;
                }

                if (!adapter_->process(nativeScratchL_.data() + carryCount_,
                                       nativeScratchR_.data() + carryCount_,
                                       renderCount,
                                       stagedEvents_.data(),
                                       stagedEvents_.size()))
                    return false;

                const size_t totalAvail = carryCount_ + renderCount;
                size_t readIdx = 0;

                for (size_t i = 0; i < chunk; ++i)
                {
                    while (fallbackResampler_.needsInput())
                    {
                        if (readIdx < totalAvail)
                        {
                            fallbackResampler_.push(nativeScratchL_[readIdx], nativeScratchR_[readIdx]);
                            ++readIdx;
                        }
                        else
                        {
                            float extraL = 0.0f, extraR = 0.0f;
                            if (!adapter_->process(&extraL, &extraR, 1, nullptr, 0))
                                return false;
                            fallbackResampler_.push(extraL, extraR);
                        }
                    }
                    fallbackResampler_.pop(left[hostOffset + i], right[hostOffset + i]);
                }

                carryCount_ = totalAvail - readIdx;
                for (size_t i = 0; i < carryCount_; ++i)
                {
                    carryL_[i] = nativeScratchL_[readIdx + i];
                    carryR_[i] = nativeScratchR_[readIdx + i];
                }

                hostOffset += chunk;
            }
            return true;
        }

    private:
        EngineRuntime(const params::EngineConfig& config, double hostSampleRate, int maxHostBlockSize)
            : hostRate_(hostSampleRate)
        {
            instrument::Settings settings;
            settings.voicing = config.preset.voicing;
            settings.voice_options = config.voiceOptions;
            // Reverb runs independently on the host rate in PluginProcessor so
            // adjusting room controls never forces a StrikeBank rebuild.
            settings.reverb.mix = 0.0;
            settings.maximum_voices = static_cast<size_t>(config.maximumVoices);
            settings.gain = 0.0011;

            engine_ = std::make_unique<instrument::Engine>(settings);
            adapter_ = std::make_unique<piano_host::Adapter>(*engine_);

            directHostRate_ = piano_host::supported_rate(hostSampleRate);
            adapterRate_ = directHostRate_ ? hostSampleRate : static_cast<double>(piano::FS);

            const size_t requested = static_cast<size_t>(std::max(64, maxHostBlockSize));
            adapterMaxBlock_ = directHostRate_
                ? std::min(kMaxAdapterBlock, requested)
                : std::min(kMaxAdapterBlock, size_t(256));

            const double ratio = adapterRate_ / hostRate_;
            fallbackHostChunk_ = std::clamp(
                static_cast<size_t>(std::floor(180.0 / std::max(0.1, ratio))),
                size_t(8),
                size_t(128));

            nativeScratchL_.assign(adapterMaxBlock_, 0.0f);
            nativeScratchR_.assign(adapterMaxBlock_, 0.0f);
            carryL_.assign(32, 0.0f);
            carryR_.assign(32, 0.0f);
            stagedEvents_.reserve(kMaxEventsPerBlock);

            if (!directHostRate_)
            {
                fallbackResampler_.prepare(adapterRate_, hostRate_);
                primeFallbackResampler();
            }
        }

        void primeFallbackResampler() noexcept
        {
            carryCount_ = 0;
            if (directHostRate_)
                return;
            fallbackResampler_.reset();
            for (int i = 0; i < Resampler::HALF_TAPS; ++i)
                fallbackResampler_.push(0.0f, 0.0f);
        }

        double hostRate_ = 44100.0;
        double adapterRate_ = 44100.0;
        bool directHostRate_ = true;
        size_t adapterMaxBlock_ = 1024;
        size_t fallbackHostChunk_ = 64;

        std::unique_ptr<instrument::Engine> engine_;
        std::unique_ptr<piano_host::Adapter> adapter_;

        Resampler fallbackResampler_;
        std::vector<float> nativeScratchL_;
        std::vector<float> nativeScratchR_;
        std::vector<float> carryL_;
        std::vector<float> carryR_;
        size_t carryCount_ = 0;
        std::vector<instrument::MidiEvent> stagedEvents_;
    };

    // Background worker that builds new EngineRuntime instances off the audio thread
    // and hands them over via a wait-free atomic slot + epoch-based garbage collection.
    class EngineBuilderThread : private juce::Thread
    {
    public:
        EngineBuilderThread() : juce::Thread("CoopianoEngineBuilder")
        {
            startThread(juce::Thread::Priority::normal);
        }

        ~EngineBuilderThread() override
        {
            cancelCurrent_.store(true, std::memory_order_release);
            signalThreadShouldExit();
            notify();
            stopThread(-1);
            retired_.clear();
        }

        void requestBuild(const params::EngineConfig& cfg, double sampleRate, int maxBlockSize, bool immediate = false, bool editMode = false)
        {
            {
                const juce::ScopedLock sl(lock_);
                requestedConfig_ = cfg;
                requestedSampleRate_ = sampleRate;
                requestedMaxBlock_ = maxBlockSize;
                requestedEditMode_ = editMode;
                requestId_.fetch_add(1, std::memory_order_release);
                hasRequest_ = true;
                immediateRequest_ = immediateRequest_ || immediate;
                cancelCurrent_.store(true, std::memory_order_release);
            }
            notify();
        }

        // Audio thread: wait-free check for a newly prepared runtime.
        EngineRuntime* pollForAudioThread(EngineRuntime* currentActive) noexcept
        {
            const uint64_t epoch = audioEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (auto* next = pendingForAudio_.exchange(nullptr, std::memory_order_acq_rel))
            {
                lastAdoptedEpoch_.store(epoch, std::memory_order_release);
                return next;
            }
            return currentActive;
        }

        bool isBuilding() const noexcept { return building_.load(std::memory_order_acquire); }
        bool isReady() const noexcept { return hasPublished_.load(std::memory_order_acquire); }

    private:
        void run() override
        {
            uint64_t lastHandledId = 0;

            while (!threadShouldExit())
            {
                wait(250);
                if (threadShouldExit())
                    break;

                reclaimSafeRetired();

                params::EngineConfig cfg;
                double sr = 44100.0;
                int block = 512;
                uint64_t currentId = 0;
                bool immediate = false;
                bool editMode = false;

                {
                    const juce::ScopedLock sl(lock_);
                    if (!hasRequest_ || requestId_.load(std::memory_order_acquire) == lastHandledId)
                        continue;
                    cfg = requestedConfig_;
                    sr = requestedSampleRate_;
                    block = requestedMaxBlock_;
                    editMode = requestedEditMode_;
                    currentId = requestId_.load(std::memory_order_acquire);
                    immediate = immediateRequest_;
                    immediateRequest_ = false;
                }

                // Debounce rapid slider drags unless initial load
                if (!immediate)
                {
                    for (int step = 0; step < 8 && !threadShouldExit(); ++step)
                    {
                        sleep(20);
                        if (requestId_.load(std::memory_order_acquire) != currentId)
                            break;
                    }
                    if (requestId_.load(std::memory_order_acquire) != currentId)
                        continue;
                }

                cancelCurrent_.store(false, std::memory_order_release);
                building_.store(true, std::memory_order_release);

                std::unique_ptr<EngineRuntime> built;
                try
                {
                    built = EngineRuntime::build(cfg, sr, block, &cancelCurrent_, editMode);
                }
                catch (...)
                {
                    built.reset();
                }

                building_.store(false, std::memory_order_release);

                if (threadShouldExit())
                    break;

                if (built && requestId_.load(std::memory_order_acquire) == currentId)
                {
                    lastHandledId = currentId;
                    auto* raw = built.get();

                    // Stage into owned pool before publishing pointer to audio thread
                    retired_.push_back({ std::move(built) });
                    if (auto* displaced = pendingForAudio_.exchange(raw, std::memory_order_acq_rel))
                    {
                        retired_.erase(
                            std::remove_if(retired_.begin(), retired_.end(), [displaced](const OwnedEntry& e) {
                                return e.ptr.get() == displaced;
                            }),
                            retired_.end());
                    }
                    hasPublished_.store(true, std::memory_order_release);
                }
            }
        }

        void reclaimSafeRetired()
        {
            if (retired_.size() <= 1)
                return;
            if (pendingForAudio_.load(std::memory_order_acquire) != nullptr)
                return;

            const uint64_t currentEpoch = audioEpoch_.load(std::memory_order_acquire);
            const uint64_t adoptedEpoch = lastAdoptedEpoch_.load(std::memory_order_acquire);
            if (currentEpoch > adoptedEpoch + 1)
            {
                retired_.erase(retired_.begin(), retired_.end() - 1);
            }
        }

        struct OwnedEntry
        {
            std::unique_ptr<EngineRuntime> ptr;
        };

        juce::CriticalSection lock_;
        params::EngineConfig requestedConfig_;
        double requestedSampleRate_ = 44100.0;
        int requestedMaxBlock_ = 512;
        bool hasRequest_ = false;
        bool immediateRequest_ = false;
        bool requestedEditMode_ = false;

        std::atomic<uint64_t> requestId_{0};
        std::atomic<bool> cancelCurrent_{false};
        std::atomic<bool> building_{false};
        std::atomic<bool> hasPublished_{false};

        std::atomic<EngineRuntime*> pendingForAudio_{nullptr};
        std::atomic<uint64_t> audioEpoch_{0};
        std::atomic<uint64_t> lastAdoptedEpoch_{0};

        std::vector<OwnedEntry> retired_;
    };
} // namespace coopiano
