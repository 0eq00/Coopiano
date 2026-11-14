#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "coopiano/preset.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace coopiano::params
{
    // Hammer
    inline constexpr const char* hammerStiffness    = "hammer_stiffness";
    inline constexpr const char* hammerMass         = "hammer_mass";
    inline constexpr const char* hammerFeltWidth    = "hammer_felt_width";
    inline constexpr const char* hammerMaxVel       = "hammer_max_vel";
    inline constexpr const char* hammerVelExp       = "hammer_vel_exp";

    // Strings
    inline constexpr const char* strDecay           = "str_decay";
    inline constexpr const char* strTrebleDecay     = "str_treble_decay";
    inline constexpr const char* strPartialLoss     = "str_partial_loss";
    inline constexpr const char* strInharmonicity   = "str_inharmonicity";
    inline constexpr const char* strUnisonDetune    = "str_unison_detune";
    inline constexpr const char* strTrebleUnison    = "str_treble_unison";
    inline constexpr const char* strFastDecay       = "str_fast_decay";
    inline constexpr const char* strHorizRatio      = "str_horiz_ratio";
    inline constexpr const char* strLongitudinal    = "str_longitudinal";
    inline constexpr const char* strBridgeCoupling  = "str_bridge_coupling";

    // Tone
    inline constexpr const char* toneFundBoost      = "tone_fund_boost";
    inline constexpr const char* toneLowerCut       = "tone_lower_cut";
    inline constexpr const char* toneBridgeCurve    = "tone_bridge_curve";
    inline constexpr const char* toneMidGain        = "tone_mid_gain";
    inline constexpr const char* toneMidFreq        = "tone_mid_freq";
    inline constexpr const char* toneMidWidth       = "tone_mid_width";

    // Soundboard
    inline constexpr const char* sbBassCorner       = "sb_bass_corner";
    inline constexpr const char* sbTrebleCorner     = "sb_treble_corner";
    inline constexpr const char* sbTrebleSlope      = "sb_treble_slope";
    inline constexpr const char* sbDecay            = "sb_decay";
    inline constexpr const char* sbLowDensity       = "sb_low_density";
    inline constexpr const char* sbRipple           = "sb_ripple";
    inline constexpr const char* sbSpread           = "sb_spread";
    inline constexpr const char* sbPosDepth         = "sb_pos_depth";

    // Sympathetic Resonance
    inline constexpr const char* resGain            = "res_gain";
    inline constexpr const char* resMaxDecay        = "res_max_decay";
    inline constexpr const char* resDamperDecay     = "res_damper_decay";
    inline constexpr const char* resTransition      = "res_transition";
    inline constexpr const char* resOwnDrive        = "res_own_drive";

    // Room Reverb (host-side zero-rebuild processing)
    inline constexpr const char* revMix             = "rev_mix";
    inline constexpr const char* revDecay           = "rev_decay";
    inline constexpr const char* revPredelay        = "rev_predelay";
    inline constexpr const char* revDamping         = "rev_damping";

    // Output & Host Options
    inline constexpr const char* outGainDb          = "out_gain_db";
    inline constexpr const char* perfVelGamma       = "perf_vel_gamma";
    inline constexpr const char* perfMaxVoices      = "perf_max_voices";
    inline constexpr const char* optDamperScale     = "opt_damper_scale";
    inline constexpr const char* optKnockGain       = "opt_knock_gain";
    inline constexpr const char* optKnock           = "opt_knock";
    inline constexpr const char* optScrape          = "opt_scrape";
    inline constexpr const char* optDamperNoise     = "opt_damper_noise";
    inline constexpr const char* optSoftLong        = "opt_soft_long";

    struct EngineConfig
    {
        preset::Preset preset;
        piano::Options voiceOptions;
        double velocityGamma = 1.0;
        int maximumVoices = 160;
        double outputGainDb = 0.0;

        bool engineEqual(const EngineConfig& other) const noexcept
        {
            const auto& a = preset.voicing;
            const auto& b = other.preset.voicing;
            return a.hammer.stiffness_scale == b.hammer.stiffness_scale
                && a.hammer.mass_scale == b.hammer.mass_scale
                && a.hammer.felt_width_scale == b.hammer.felt_width_scale
                && a.hammer.maximum_velocity_mps == b.hammer.maximum_velocity_mps
                && a.hammer.velocity_exponent == b.hammer.velocity_exponent
                && a.strings.decay_scale == b.strings.decay_scale
                && a.strings.treble_decay_scale == b.strings.treble_decay_scale
                && a.strings.partial_loss_scale == b.strings.partial_loss_scale
                && a.strings.inharmonicity_scale == b.strings.inharmonicity_scale
                && a.strings.unison_detune_scale == b.strings.unison_detune_scale
                && a.strings.treble_unison_detune == b.strings.treble_unison_detune
                && a.strings.fast_decay_scale == b.strings.fast_decay_scale
                && a.strings.horizontal_ratio == b.strings.horizontal_ratio
                && a.strings.longitudinal_gain == b.strings.longitudinal_gain
                && a.strings.bridge_coupling == b.strings.bridge_coupling
                && a.tone.fundamental_boost_db == b.tone.fundamental_boost_db
                && a.tone.lower_overtone_cut_db == b.tone.lower_overtone_cut_db
                && a.tone.bridge_curve_scale == b.tone.bridge_curve_scale
                && a.tone.mid_gain_db == b.tone.mid_gain_db
                && a.tone.mid_frequency_hz == b.tone.mid_frequency_hz
                && a.tone.mid_width_octaves == b.tone.mid_width_octaves
                && a.soundboard.bass_corner_hz == b.soundboard.bass_corner_hz
                && a.soundboard.treble_corner_hz == b.soundboard.treble_corner_hz
                && a.soundboard.treble_slope == b.soundboard.treble_slope
                && a.soundboard.decay_scale == b.soundboard.decay_scale
                && a.soundboard.low_mode_density == b.soundboard.low_mode_density
                && a.soundboard.ripple_fraction == b.soundboard.ripple_fraction
                && a.soundboard.spread == b.soundboard.spread
                && a.soundboard.position_depth == b.soundboard.position_depth
                && a.resonance.gain == b.resonance.gain
                && a.resonance.maximum_decay_seconds == b.resonance.maximum_decay_seconds
                && a.resonance.damper_decay_seconds == b.resonance.damper_decay_seconds
                && a.resonance.transition_seconds == b.resonance.transition_seconds
                && a.resonance.own_drive == b.resonance.own_drive
                && voiceOptions.damper_scale == other.voiceOptions.damper_scale
                && voiceOptions.knock_gain == other.voiceOptions.knock_gain
                && voiceOptions.knock == other.voiceOptions.knock
                && voiceOptions.scrape == other.voiceOptions.scrape
                && voiceOptions.damper_noise == other.voiceOptions.damper_noise
                && voiceOptions.soften_longitudinal_highs == other.voiceOptions.soften_longitudinal_highs
                && velocityGamma == other.velocityGamma
                && maximumVoices == other.maximumVoices;
        }
    };

    inline bool isInstantOnlyParam(const juce::String& id) noexcept
    {
        return id == revMix || id == revDecay || id == revPredelay || id == revDamping || id == outGainDb;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using R = juce::NormalisableRange<float>;
        using P = juce::AudioParameterFloat;
        using PB = juce::AudioParameterBool;
        using PI = juce::AudioParameterInt;
        auto pid = [](const char* id) { return juce::ParameterID{ id, 1 }; };

        preset::Preset def; // default calibrated voicing

        auto hammerGroup = std::make_unique<juce::AudioProcessorParameterGroup>("hammer", "Hammer", "|");
        hammerGroup->addChild(
            std::make_unique<P>(pid(hammerStiffness), "Hammer Stiffness",   R(0.25f, 4.0f, 0.01f, 0.6f), float(def.voicing.hammer.stiffness_scale)),
            std::make_unique<P>(pid(hammerMass),      "Hammer Mass",        R(0.5f,  2.0f, 0.01f),       float(def.voicing.hammer.mass_scale)),
            std::make_unique<P>(pid(hammerFeltWidth), "Felt Width",         R(0.5f,  2.0f, 0.01f),       float(def.voicing.hammer.felt_width_scale)),
            std::make_unique<P>(pid(hammerMaxVel),    "Max Velocity (m/s)", R(2.0f,  8.0f, 0.05f),       float(def.voicing.hammer.maximum_velocity_mps)),
            std::make_unique<P>(pid(hammerVelExp),    "Velocity Exponent",  R(0.75f, 2.5f, 0.01f),       float(def.voicing.hammer.velocity_exponent))
        );

        auto stringsGroup = std::make_unique<juce::AudioProcessorParameterGroup>("strings", "Strings", "|");
        stringsGroup->addChild(
            std::make_unique<P>(pid(strDecay),         "String Decay",      R(0.25f, 2.0f, 0.01f),       float(def.voicing.strings.decay_scale)),
            std::make_unique<P>(pid(strTrebleDecay),   "Treble Decay",      R(0.10f, 2.0f, 0.01f),       float(def.voicing.strings.treble_decay_scale)),
            std::make_unique<P>(pid(strPartialLoss),   "Partial Loss",      R(0.25f, 4.0f, 0.01f, 0.6f), float(def.voicing.strings.partial_loss_scale)),
            std::make_unique<P>(pid(strInharmonicity), "Inharmonicity",     R(0.0f,  3.0f, 0.01f),       float(def.voicing.strings.inharmonicity_scale)),
            std::make_unique<P>(pid(strUnisonDetune),  "Unison Detune",     R(0.0f,  3.0f, 0.01f),       float(def.voicing.strings.unison_detune_scale)),
            std::make_unique<P>(pid(strTrebleUnison),  "Treble Unison",     R(0.0f,  3.0f, 0.01f),       float(def.voicing.strings.treble_unison_detune)),
            std::make_unique<P>(pid(strFastDecay),     "Fast/Slow Decay",   R(0.75f, 2.0f, 0.01f),       float(def.voicing.strings.fast_decay_scale)),
            std::make_unique<P>(pid(strHorizRatio),    "Horizontal Ratio",  R(0.0f,  1.0f, 0.01f),       float(def.voicing.strings.horizontal_ratio)),
            std::make_unique<P>(pid(strLongitudinal),  "Longitudinal Gain", R(0.0f,  0.5f, 0.005f),      float(def.voicing.strings.longitudinal_gain)),
            std::make_unique<P>(pid(strBridgeCoupling),"Bridge Coupling",   R(0.0f,  3.0f, 0.01f),       float(def.voicing.strings.bridge_coupling))
        );

        auto toneGroup = std::make_unique<juce::AudioProcessorParameterGroup>("tone", "Tone", "|");
        toneGroup->addChild(
            std::make_unique<P>(pid(toneFundBoost),   "Fundamental Boost (dB)",  R(-6.0f,  12.0f,   0.1f),       float(def.voicing.tone.fundamental_boost_db)),
            std::make_unique<P>(pid(toneLowerCut),    "Lower Overtone Cut (dB)", R(0.0f,   12.0f,   0.1f),       float(def.voicing.tone.lower_overtone_cut_db)),
            std::make_unique<P>(pid(toneBridgeCurve), "Bridge Curve Scale",      R(0.0f,   2.0f,    0.01f),      float(def.voicing.tone.bridge_curve_scale)),
            std::make_unique<P>(pid(toneMidGain),     "Mid Bell Gain (dB)",      R(-9.0f,  9.0f,    0.1f),       float(def.voicing.tone.mid_gain_db)),
            std::make_unique<P>(pid(toneMidFreq),     "Mid Bell Freq (Hz)",      R(200.0f, 5000.0f, 1.0f, 0.4f), float(def.voicing.tone.mid_frequency_hz)),
            std::make_unique<P>(pid(toneMidWidth),    "Mid Bell Width (Oct)",    R(0.2f,   2.0f,    0.01f),      float(def.voicing.tone.mid_width_octaves))
        );

        auto soundboardGroup = std::make_unique<juce::AudioProcessorParameterGroup>("soundboard", "Soundboard", "|");
        soundboardGroup->addChild(
            std::make_unique<P>(pid(sbBassCorner),   "Bass Corner (Hz)",   R(30.0f,   250.0f,   1.0f,  0.6f), float(def.voicing.soundboard.bass_corner_hz)),
            std::make_unique<P>(pid(sbTrebleCorner), "Treble Corner (Hz)", R(1500.0f, 10000.0f, 10.0f, 0.5f), float(def.voicing.soundboard.treble_corner_hz)),
            std::make_unique<P>(pid(sbTrebleSlope),  "Treble Slope",       R(0.5f,    3.0f,     0.01f),       float(def.voicing.soundboard.treble_slope)),
            std::make_unique<P>(pid(sbDecay),        "Board Decay",        R(0.25f,   2.0f,     0.01f),       float(def.voicing.soundboard.decay_scale)),
            std::make_unique<P>(pid(sbLowDensity),   "Low Mode Density",   R(0.0f,    1.0f,     0.01f),       float(def.voicing.soundboard.low_mode_density)),
            std::make_unique<P>(pid(sbRipple),       "Modal Ripple",       R(0.0f,    1.0f,     0.01f),       float(def.voicing.soundboard.ripple_fraction)),
            std::make_unique<P>(pid(sbSpread),       "Stereo Spread",      R(0.0f,    1.0f,     0.01f),       float(def.voicing.soundboard.spread)),
            std::make_unique<P>(pid(sbPosDepth),     "Position Depth",     R(0.0f,    1.0f,     0.01f),       float(def.voicing.soundboard.position_depth))
        );

        auto resonanceGroup = std::make_unique<juce::AudioProcessorParameterGroup>("resonance", "Resonance", "|");
        resonanceGroup->addChild(
            std::make_unique<P>(pid(resGain),        "Sympathetic Gain",      R(0.0f,   1.0f,  0.01f),       float(def.voicing.resonance.gain)),
            std::make_unique<P>(pid(resMaxDecay),    "Open T60 Cap (s)",      R(1.0f,   30.0f, 0.1f,  0.6f), float(def.voicing.resonance.maximum_decay_seconds)),
            std::make_unique<P>(pid(resDamperDecay), "Closed Damper T60 (s)", R(0.03f,  2.0f,  0.01f, 0.5f), float(def.voicing.resonance.damper_decay_seconds)),
            std::make_unique<P>(pid(resTransition),  "Damper Transition (s)", R(0.001f, 0.2f,  0.001f,0.5f), float(def.voicing.resonance.transition_seconds)),
            std::make_unique<P>(pid(resOwnDrive),    "Own Key Drive",         R(0.0f,   1.0f,  0.01f),       float(def.voicing.resonance.own_drive))
        );

        auto reverbGroup = std::make_unique<juce::AudioProcessorParameterGroup>("reverb", "Reverb", "|");
        reverbGroup->addChild(
            std::make_unique<P>(pid(revMix),      "Room Mix",       R(0.0f,   1.0f,     0.01f),      float(def.reverb.mix)),
            std::make_unique<P>(pid(revDecay),    "Room Decay (s)", R(0.2f,   4.0f,     0.01f),      float(def.reverb.decay_seconds)),
            std::make_unique<P>(pid(revPredelay), "Predelay (ms)",  R(0.0f,   80.0f,    0.5f),       float(def.reverb.predelay_ms)),
            std::make_unique<P>(pid(revDamping),  "Damping (Hz)",   R(500.0f, 12000.0f, 10.0f, 0.5f),float(def.reverb.damping_hz))
        );

        auto perfGroup = std::make_unique<juce::AudioProcessorParameterGroup>("performance", "Performance", "|");
        perfGroup->addChild(
            std::make_unique<P>(pid(outGainDb),      "Output Gain (dB)",        R(-24.0f, 12.0f, 0.1f),  0.0f),
            std::make_unique<P>(pid(perfVelGamma),   "Velocity Curve",          R(0.4f,   2.2f,  0.01f), 1.0f),
            std::make_unique<PI>(pid(perfMaxVoices), "Max Polyphony",           32, 256, 160),
            std::make_unique<P>(pid(optDamperScale), "Damper Time Scale",       R(0.25f,  2.5f,  0.01f), 1.0f),
            std::make_unique<P>(pid(optKnockGain),   "Key Knock Gain",          R(0.0f,   2.5f,  0.01f), 1.0f),
            std::make_unique<PB>(pid(optKnock),      "Soundboard Knock",        true),
            std::make_unique<PB>(pid(optScrape),     "Hammer Scrape",           true),
            std::make_unique<PB>(pid(optDamperNoise),"Damper Noise",            true),
            std::make_unique<PB>(pid(optSoftLong),   "Soften Longitudinal Highs", true)
        );

        return {
            std::move(hammerGroup),
            std::move(stringsGroup),
            std::move(toneGroup),
            std::move(soundboardGroup),
            std::move(resonanceGroup),
            std::move(reverbGroup),
            std::move(perfGroup)
        };
    }

    inline EngineConfig readConfig(const juce::AudioProcessorValueTreeState& apvts)
    {
        auto val = [&apvts](const char* id, double fallback) -> double {
            if (auto* p = apvts.getRawParameterValue(id))
                return static_cast<double>(p->load());
            return fallback;
        };

        EngineConfig cfg;
        auto& v = cfg.preset.voicing;
        v.hammer.stiffness_scale      = std::clamp(val(hammerStiffness, 0.90), 0.25, 4.0);
        v.hammer.mass_scale           = std::clamp(val(hammerMass, 1.3), 0.5, 2.0);
        v.hammer.felt_width_scale     = std::clamp(val(hammerFeltWidth, 1.4), 0.5, 2.0);
        v.hammer.maximum_velocity_mps = std::clamp(val(hammerMaxVel, 6.0), 2.0, 8.0);
        v.hammer.velocity_exponent    = std::clamp(val(hammerVelExp, 1.8), 0.75, 2.5);

        v.strings.decay_scale          = std::clamp(val(strDecay, 1.10), 0.25, 2.0);
        v.strings.treble_decay_scale   = std::clamp(val(strTrebleDecay, 0.38), 0.1, 2.0);
        v.strings.partial_loss_scale   = std::clamp(val(strPartialLoss, 1.0), 0.25, 4.0);
        v.strings.inharmonicity_scale  = std::clamp(val(strInharmonicity, 0.85), 0.0, 3.0);
        v.strings.unison_detune_scale  = std::clamp(val(strUnisonDetune, 0.35), 0.0, 3.0);
        v.strings.treble_unison_detune = std::clamp(val(strTrebleUnison, 0.875), 0.0, 3.0);
        v.strings.fast_decay_scale     = std::clamp(val(strFastDecay, 0.95), 0.75, 2.0);
        v.strings.horizontal_ratio     = std::clamp(val(strHorizRatio, 0.4), 0.0, 1.0);
        v.strings.longitudinal_gain    = std::clamp(val(strLongitudinal, 0.22), 0.0, 0.5);
        v.strings.bridge_coupling      = std::clamp(val(strBridgeCoupling, 1.0), 0.0, 3.0);

        v.tone.fundamental_boost_db  = std::clamp(val(toneFundBoost, 0.0), -6.0, 12.0);
        v.tone.lower_overtone_cut_db = std::clamp(val(toneLowerCut, 0.0), 0.0, 12.0);
        v.tone.bridge_curve_scale    = std::clamp(val(toneBridgeCurve, 1.0), 0.0, 2.0);
        v.tone.mid_gain_db           = std::clamp(val(toneMidGain, -1.5), -9.0, 9.0);
        v.tone.mid_frequency_hz      = std::clamp(val(toneMidFreq, 1200.0), 200.0, 5000.0);
        v.tone.mid_width_octaves     = std::clamp(val(toneMidWidth, 0.9), 0.2, 2.0);

        v.soundboard.bass_corner_hz   = std::clamp(val(sbBassCorner, 70.0), 30.0, 250.0);
        v.soundboard.treble_corner_hz = std::clamp(val(sbTrebleCorner, 3800.0), 1500.0, 10000.0);
        v.soundboard.treble_slope     = std::clamp(val(sbTrebleSlope, 1.8), 0.5, 3.0);
        v.soundboard.decay_scale      = std::clamp(val(sbDecay, 1.10), 0.25, 2.0);
        v.soundboard.low_mode_density = std::clamp(val(sbLowDensity, 1.0), 0.0, 1.0);
        v.soundboard.ripple_fraction  = std::clamp(val(sbRipple, 0.25), 0.0, 1.0);
        v.soundboard.spread           = std::clamp(val(sbSpread, 0.7), 0.0, 1.0);
        v.soundboard.position_depth   = std::clamp(val(sbPosDepth, 0.5), 0.0, 1.0);

        v.resonance.gain                  = std::clamp(val(resGain, 0.18), 0.0, 1.0);
        v.resonance.maximum_decay_seconds = std::clamp(val(resMaxDecay, 12.0), 1.0, 30.0);
        v.resonance.damper_decay_seconds  = std::clamp(val(resDamperDecay, 0.18), 0.03, 2.0);
        v.resonance.transition_seconds    = std::clamp(val(resTransition, 0.04), 0.001, 0.2);
        v.resonance.own_drive             = std::clamp(val(resOwnDrive, 0.0), 0.0, 1.0);

        cfg.preset.reverb.mix           = std::clamp(val(revMix, 0.3), 0.0, 1.0);
        cfg.preset.reverb.decay_seconds = std::clamp(val(revDecay, 1.1), 0.2, 4.0);
        cfg.preset.reverb.predelay_ms   = std::clamp(val(revPredelay, 12.0), 0.0, 80.0);
        cfg.preset.reverb.damping_hz    = std::clamp(val(revDamping, 5500.0), 500.0, 12000.0);

        cfg.voiceOptions.damper_scale = std::clamp(val(optDamperScale, 1.0), 0.25, 2.5);
        cfg.voiceOptions.knock_gain   = std::clamp(val(optKnockGain, 1.0), 0.0, 2.5);
        cfg.voiceOptions.knock        = val(optKnock, 1.0) > 0.5;
        cfg.voiceOptions.scrape       = val(optScrape, 1.0) > 0.5;
        cfg.voiceOptions.damper_noise = val(optDamperNoise, 1.0) > 0.5;
        cfg.voiceOptions.soften_longitudinal_highs = val(optSoftLong, 1.0) > 0.5;

        cfg.velocityGamma = std::clamp(val(perfVelGamma, 1.0), 0.4, 2.2);
        cfg.maximumVoices = std::clamp(static_cast<int>(std::lround(val(perfMaxVoices, 160.0))), 32, 256);
        cfg.outputGainDb  = std::clamp(val(outGainDb, 0.0), -24.0, 12.0);
        return cfg;
    }

    inline void applyPreset(juce::AudioProcessorValueTreeState& apvts, const preset::Preset& p)
    {
        auto set = [&apvts](const char* id, double value) {
            if (auto* param = apvts.getParameter(id))
                param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(value)));
        };

        const auto& v = p.voicing;
        set(hammerStiffness, v.hammer.stiffness_scale);
        set(hammerMass, v.hammer.mass_scale);
        set(hammerFeltWidth, v.hammer.felt_width_scale);
        set(hammerMaxVel, v.hammer.maximum_velocity_mps);
        set(hammerVelExp, v.hammer.velocity_exponent);

        set(strDecay, v.strings.decay_scale);
        set(strTrebleDecay, v.strings.treble_decay_scale);
        set(strPartialLoss, v.strings.partial_loss_scale);
        set(strInharmonicity, v.strings.inharmonicity_scale);
        set(strUnisonDetune, v.strings.unison_detune_scale);
        set(strTrebleUnison, v.strings.treble_unison_detune);
        set(strFastDecay, v.strings.fast_decay_scale);
        set(strHorizRatio, v.strings.horizontal_ratio);
        set(strLongitudinal, v.strings.longitudinal_gain);
        set(strBridgeCoupling, v.strings.bridge_coupling);

        set(toneFundBoost, v.tone.fundamental_boost_db);
        set(toneLowerCut, v.tone.lower_overtone_cut_db);
        set(toneBridgeCurve, v.tone.bridge_curve_scale);
        set(toneMidGain, v.tone.mid_gain_db);
        set(toneMidFreq, v.tone.mid_frequency_hz);
        set(toneMidWidth, v.tone.mid_width_octaves);

        set(sbBassCorner, v.soundboard.bass_corner_hz);
        set(sbTrebleCorner, v.soundboard.treble_corner_hz);
        set(sbTrebleSlope, v.soundboard.treble_slope);
        set(sbDecay, v.soundboard.decay_scale);
        set(sbLowDensity, v.soundboard.low_mode_density);
        set(sbRipple, v.soundboard.ripple_fraction);
        set(sbSpread, v.soundboard.spread);
        set(sbPosDepth, v.soundboard.position_depth);

        set(resGain, v.resonance.gain);
        set(resMaxDecay, v.resonance.maximum_decay_seconds);
        set(resDamperDecay, v.resonance.damper_decay_seconds);
        set(resTransition, v.resonance.transition_seconds);
        set(resOwnDrive, v.resonance.own_drive);

        set(revMix, p.reverb.mix);
        set(revDecay, p.reverb.decay_seconds);
        set(revPredelay, p.reverb.predelay_ms);
        set(revDamping, p.reverb.damping_hz);
    }

    inline preset::Preset makeFactoryPreset(int index)
    {
        preset::Preset p; // 0: Calibrated Concert (default.ini)
        switch (index)
        {
            case 1: // Intimate Felt
                p.voicing.hammer.stiffness_scale = 0.52;
                p.voicing.hammer.mass_scale = 1.45;
                p.voicing.hammer.felt_width_scale = 1.75;
                p.voicing.tone.mid_gain_db = -3.8;
                p.voicing.tone.mid_frequency_hz = 1650.0;
                p.voicing.soundboard.treble_corner_hz = 2600.0;
                p.voicing.resonance.gain = 0.24;
                p.reverb.mix = 0.36;
                p.reverb.decay_seconds = 1.45;
                p.reverb.damping_hz = 3800.0;
                break;
            case 2: // Bright Concert Stage
                p.voicing.hammer.stiffness_scale = 1.38;
                p.voicing.hammer.felt_width_scale = 1.05;
                p.voicing.strings.decay_scale = 1.25;
                p.voicing.tone.mid_gain_db = 1.8;
                p.voicing.tone.mid_frequency_hz = 2100.0;
                p.voicing.soundboard.treble_corner_hz = 5200.0;
                p.voicing.soundboard.spread = 0.88;
                p.reverb.mix = 0.42;
                p.reverb.decay_seconds = 1.95;
                p.reverb.predelay_ms = 22.0;
                break;
            case 3: // Dry Studio Upright
                p.voicing.hammer.stiffness_scale = 1.10;
                p.voicing.strings.decay_scale = 0.78;
                p.voicing.strings.inharmonicity_scale = 1.35;
                p.voicing.strings.unison_detune_scale = 0.62;
                p.voicing.soundboard.bass_corner_hz = 105.0;
                p.voicing.soundboard.spread = 0.38;
                p.voicing.resonance.gain = 0.12;
                p.reverb.mix = 0.12;
                p.reverb.decay_seconds = 0.55;
                break;
            case 4: // Ethereal Sympathetic
                p.voicing.hammer.stiffness_scale = 0.72;
                p.voicing.strings.decay_scale = 1.55;
                p.voicing.strings.longitudinal_gain = 0.34;
                p.voicing.resonance.gain = 0.48;
                p.voicing.resonance.maximum_decay_seconds = 22.0;
                p.reverb.mix = 0.52;
                p.reverb.decay_seconds = 2.6;
                p.reverb.predelay_ms = 28.0;
                break;
            default:
                break;
        }
        return p;
    }
} // namespace coopiano::params
