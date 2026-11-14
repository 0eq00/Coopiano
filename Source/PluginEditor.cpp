#include "PluginEditor.h"
#include "CoopianoParameters.h"

namespace
{
    const juce::Colour kBgCanvas      { 0xff0b0d11 };
    const juce::Colour kBgPanel       { 0xff131720 };
    const juce::Colour kBgSurface     { 0xff1b212d };
    const juce::Colour kBorderHair    { 0xff272f3f };
    const juce::Colour kTextPrimary   { 0xfff1f5f9 };
    const juce::Colour kTextSecondary { 0xff94a3b8 };
    const juce::Colour kAccentBrass   { 0xffd99b38 };
    const juce::Colour kAccentActive  { 0xffe8b458 };
}

CoopianoLookAndFeel::CoopianoLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBgCanvas);
    setColour(juce::ComboBox::backgroundColourId, kBgSurface);
    setColour(juce::ComboBox::textColourId, kTextPrimary);
    setColour(juce::ComboBox::outlineColourId, kBorderHair);
    setColour(juce::PopupMenu::backgroundColourId, kBgPanel);
    setColour(juce::PopupMenu::textColourId, kTextPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccentBrass);
    setColour(juce::PopupMenu::highlightedTextColourId, kBgCanvas);
    setColour(juce::Slider::textBoxTextColourId, kTextPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, kBgSurface);
    setColour(juce::Slider::textBoxOutlineColourId, kBorderHair);
    setColour(juce::ToggleButton::textColourId, kTextPrimary);
    setColour(juce::ToggleButton::tickColourId, kAccentBrass);
}

void CoopianoLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                           int x, int y, int width, int height,
                                           float sliderPosProportional,
                                           float rotaryStartAngle,
                                           float rotaryEndAngle,
                                           juce::Slider&)
{
    const float radius = static_cast<float>(juce::jmin(width, height)) * 0.40f;
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.46f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Track arc
    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(kBorderHair);
    g.strokePath(bgArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Active brass arc
    juce::Path valArc;
    valArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(kAccentBrass);
    g.strokePath(valArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Machined cap
    const float capR = radius * 0.72f;
    g.setColour(kBgSurface);
    g.fillEllipse(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
    g.setColour(kBorderHair.brighter(0.25f));
    g.drawEllipse(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    // Indicator needle
    juce::Path pointer;
    const float pLen = capR * 0.78f;
    const float pThick = 2.2f;
    pointer.addRoundedRectangle(-pThick * 0.5f, -pLen, pThick, pLen * 0.55f, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
    g.setColour(kAccentActive);
    g.fillPath(pointer);
}

void CoopianoLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                               juce::Button& button,
                                               const juce::Colour&,
                                               bool highlighted,
                                               bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool toggled = button.getToggleState();

    juce::Colour fill = toggled ? kAccentBrass.withAlpha(0.18f) : kBgSurface;
    if (down) fill = fill.brighter(0.15f);
    else if (highlighted) fill = fill.brighter(0.08f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(toggled ? kAccentBrass : kBorderHair);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

CoopianoAudioProcessorEditor::CoopianoAudioProcessorEditor(CoopianoAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , processor_(p)
    , keyboard_(p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel_);

    // Preset selector
    for (int i = 0; i < processor_.getNumPrograms(); ++i)
        presetCombo_.addItem(processor_.getProgramName(i), i + 1);
    presetCombo_.setSelectedId(processor_.getCurrentProgram() + 1, juce::dontSendNotification);
    presetCombo_.onChange = [this] {
        const int idx = presetCombo_.getSelectedId() - 1;
        if (idx >= 0)
            processor_.setCurrentProgram(idx);
    };
    addAndMakeVisible(presetCombo_);

    loadIniButton_.onClick = [this] { importIniFile(); };
    saveIniButton_.onClick = [this] { exportIniFile(); };
    addAndMakeVisible(loadIniButton_);
    addAndMakeVisible(saveIniButton_);

    sustainButton_.setClickingTogglesState(true);
    sustainButton_.onClick = [this] {
        sustainLatched_ = sustainButton_.getToggleState();
        processor_.keyboardState().processNextMidiEvent(
            juce::MidiMessage::controllerEvent(1, 64, sustainLatched_ ? 127 : 0));
    };
    addAndMakeVisible(sustainButton_);

    const std::array<const char*, 6> tabTitles = {
        "Hammer", "Strings", "Tone", "Soundboard", "Resonance", "Room & Output"
    };
    for (size_t i = 0; i < tabButtons_.size(); ++i)
    {
        tabButtons_[i].setButtonText(tabTitles[i]);
        tabButtons_[i].setClickingTogglesState(false);
        tabButtons_[i].onClick = [this, idx = static_cast<int>(i)] { selectTab(idx); };
        addAndMakeVisible(tabButtons_[i]);
    }

    namespace P = coopiano::params;
    populateSection(0, {
        { P::hammerStiffness, "Stiffness" },
        { P::hammerMass,      "Mass" },
        { P::hammerFeltWidth, "Felt Width" },
        { P::hammerMaxVel,    "Max Vel (m/s)" },
        { P::hammerVelExp,    "Vel Exponent" },
        { P::perfVelGamma,    "MIDI Gamma" }
    });

    populateSection(1, {
        { P::strDecay,         "Decay Scale" },
        { P::strTrebleDecay,   "Treble Decay" },
        { P::strPartialLoss,   "Partial Loss" },
        { P::strInharmonicity, "Inharmonicity" },
        { P::strUnisonDetune,  "Unison Detune" },
        { P::strTrebleUnison,  "Treble Unison" },
        { P::strFastDecay,     "Fast/Slow Ratio" },
        { P::strHorizRatio,    "Horiz Polar" },
        { P::strLongitudinal,  "Longitudinal" },
        { P::strBridgeCoupling,"Bridge Coupling" }
    });

    populateSection(2, {
        { P::toneFundBoost,   "Fund Boost (dB)" },
        { P::toneLowerCut,    "Overtone Cut (dB)" },
        { P::toneBridgeCurve, "Bridge EQ Depth" },
        { P::toneMidGain,     "Mid Bell (dB)" },
        { P::toneMidFreq,     "Mid Freq (Hz)" },
        { P::toneMidWidth,    "Mid Width (Oct)" }
    });

    populateSection(3, {
        { P::sbBassCorner,   "Bass Corner (Hz)" },
        { P::sbTrebleCorner, "Treble Corner (Hz)" },
        { P::sbTrebleSlope,  "Treble Slope" },
        { P::sbDecay,        "Board Decay" },
        { P::sbLowDensity,   "Low Mode Density" },
        { P::sbRipple,       "Modal Ripple" },
        { P::sbSpread,       "Stereo Spread" },
        { P::sbPosDepth,     "Position Depth" }
    });

    populateSection(4, {
        { P::resGain,        "Sympathetic Gain" },
        { P::resMaxDecay,    "Open T60 Cap (s)" },
        { P::resDamperDecay, "Damper T60 (s)" },
        { P::resTransition,  "Transition (s)" },
        { P::resOwnDrive,    "Own Key Drive" },
        { P::optDamperScale, "Damper Time" }
    });

    populateSection(5, {
        { P::revMix,         "Room Mix" },
        { P::revDecay,       "Room Decay (s)" },
        { P::revPredelay,    "Predelay (ms)" },
        { P::revDamping,     "Damping (Hz)" },
        { P::outGainDb,      "Output Gain (dB)" },
        { P::optKnockGain,   "Knock Gain" },
        { P::perfMaxVoices,  "Max Polyphony" }
    }, {
        { P::optKnock,       "Soundboard Knock" },
        { P::optScrape,      "Hammer Scrape" },
        { P::optDamperNoise, "Damper Noise" },
        { P::optSoftLong,    "Soften Long. Highs" }
    });

    keyboard_.setAvailableRange(21, 108);
    keyboard_.setLowestVisibleKey(36);
    keyboard_.setKeyWidth(21.0f);
    addAndMakeVisible(keyboard_);

    selectTab(0);
    setSize(980, 560);
    setResizable(true, true);
    setResizeLimits(840, 500, 1440, 820);
    startTimerHz(24);
}

CoopianoAudioProcessorEditor::~CoopianoAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void CoopianoAudioProcessorEditor::populateSection(
    int tabIndex,
    const std::vector<std::pair<const char*, const char*>>& params,
    const std::vector<std::pair<const char*, const char*>>& toggles)
{
    auto& knobs = tabKnobs_[static_cast<size_t>(tabIndex)];
    for (const auto& [id, name] : params)
    {
        KnobControl ctrl;
        ctrl.paramId = id;
        ctrl.labelText = name;

        ctrl.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::TextBoxBelow);
        ctrl.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 18);
        ctrl.slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addChildComponent(*ctrl.slider);

        ctrl.label = std::make_unique<juce::Label>(juce::String(), name);
        ctrl.label->setJustificationType(juce::Justification::centred);
        ctrl.label->setFont(juce::FontOptions(12.5f));
        ctrl.label->setColour(juce::Label::textColourId, kTextSecondary);
        addChildComponent(*ctrl.label);

        ctrl.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.apvts(), id, *ctrl.slider);

        knobs.push_back(std::move(ctrl));
    }

    auto& tgs = tabToggles_[static_cast<size_t>(tabIndex)];
    for (const auto& [id, name] : toggles)
    {
        ToggleControl tc;
        tc.paramId = id;
        tc.button = std::make_unique<juce::ToggleButton>(name);
        addChildComponent(*tc.button);
        tc.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor_.apvts(), id, *tc.button);
        tgs.push_back(std::move(tc));
    }
}

void CoopianoAudioProcessorEditor::selectTab(int tabIndex)
{
    activeTab_ = juce::jlimit(0, 5, tabIndex);
    for (size_t i = 0; i < tabButtons_.size(); ++i)
    {
        const bool active = static_cast<int>(i) == activeTab_;
        tabButtons_[i].setToggleState(active, juce::dontSendNotification);
        tabButtons_[i].setColour(juce::TextButton::textColourOffId, active ? kAccentActive : kTextSecondary);
        tabButtons_[i].setColour(juce::TextButton::textColourOnId, kAccentActive);

        for (auto& k : tabKnobs_[i])
        {
            k.slider->setVisible(active);
            k.label->setVisible(active);
        }
        for (auto& t : tabToggles_[i])
            t.button->setVisible(active);
    }
    resized();
    repaint();
}

void CoopianoAudioProcessorEditor::timerCallback()
{
    meterL_ = std::max(processor_.outputPeakL(), meterL_ * 0.84f);
    meterR_ = std::max(processor_.outputPeakR(), meterR_ * 0.84f);
    repaint(0, 0, getWidth(), 64);
}

void CoopianoAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBgCanvas);

    // Header bar
    auto header = getLocalBounds().removeFromTop(58);
    g.setColour(kBgPanel);
    g.fillRect(header);
    g.setColour(kBorderHair);
    g.drawLine(0.0f, 58.0f, static_cast<float>(getWidth()), 58.0f, 1.0f);

    g.setColour(kTextPrimary);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("COOPIANO", 20, 10, 160, 24, juce::Justification::centredLeft);

    g.setColour(kTextSecondary);
    g.setFont(juce::FontOptions(11.5f));
    const juce::String statusText = processor_.isEngineBuilding()
        ? "Preparing Strike Bank..."
        : (processor_.isEngineReady() ? "Ready" : "Initializing");
    const juce::String telemetry = statusText
        + "  |  Voices: " + juce::String(processor_.activeVoices())
        + "  |  PDC: " + juce::String(processor_.latencySamplesReported()) + " smp";
    g.drawText(telemetry, 20, 33, 340, 18, juce::Justification::centredLeft);

    // Output peak bars
    const int meterX = getWidth() - 120;
    g.setColour(kBgSurface);
    g.fillRoundedRectangle(static_cast<float>(meterX), 18.0f, 96.0f, 8.0f, 2.0f);
    g.fillRoundedRectangle(static_cast<float>(meterX), 31.0f, 96.0f, 8.0f, 2.0f);
    g.setColour(kAccentBrass);
    g.fillRoundedRectangle(static_cast<float>(meterX), 18.0f,
                           96.0f * juce::jlimit(0.0f, 1.0f, meterL_), 8.0f, 2.0f);
    g.fillRoundedRectangle(static_cast<float>(meterX), 31.0f,
                           96.0f * juce::jlimit(0.0f, 1.0f, meterR_), 8.0f, 2.0f);

    // Main parameter rack surface
    auto rackArea = getLocalBounds().reduced(16);
    rackArea.removeFromTop(58);
    rackArea.removeFromBottom(108);
    rackArea.removeFromTop(40);
    g.setColour(kBgPanel);
    g.fillRoundedRectangle(rackArea.toFloat(), 8.0f);
    g.setColour(kBorderHair);
    g.drawRoundedRectangle(rackArea.toFloat(), 8.0f, 1.0f);
}

void CoopianoAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header controls
    auto header = bounds.removeFromTop(58);
    auto rightControls = header.removeFromRight(580).reduced(12, 14);
    rightControls.removeFromRight(118); // reserve stereo meter space
    sustainButton_.setBounds(rightControls.removeFromRight(76).reduced(2, 0));
    saveIniButton_.setBounds(rightControls.removeFromRight(88).reduced(2, 0));
    loadIniButton_.setBounds(rightControls.removeFromRight(88).reduced(2, 0));
    presetCombo_.setBounds(rightControls.reduced(4, 0));

    // Bottom keyboard
    auto kbArea = bounds.removeFromBottom(104).reduced(16, 8);
    keyboard_.setBounds(kbArea);

    auto work = bounds.reduced(16, 10);

    // Section tabs
    auto tabRow = work.removeFromTop(34);
    const int tabW = tabRow.getWidth() / static_cast<int>(tabButtons_.size());
    for (size_t i = 0; i < tabButtons_.size(); ++i)
        tabButtons_[i].setBounds(tabRow.removeFromLeft(tabW).reduced(3, 0));

    work.removeFromTop(8);
    auto rack = work.reduced(14, 14);

    auto& knobs = tabKnobs_[static_cast<size_t>(activeTab_)];
    auto& toggles = tabToggles_[static_cast<size_t>(activeTab_)];

    if (!toggles.empty())
    {
        auto toggleRow = rack.removeFromBottom(32);
        const int tw = toggleRow.getWidth() / static_cast<int>(toggles.size());
        for (auto& t : toggles)
            t.button->setBounds(toggleRow.removeFromLeft(tw));
        rack.removeFromBottom(8);
    }

    const int cols = 5;
    const int rows = std::max(1, (static_cast<int>(knobs.size()) + cols - 1) / cols);
    const int cellW = rack.getWidth() / cols;
    const int cellH = rack.getHeight() / rows;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int r = static_cast<int>(i) / cols;
        const int c = static_cast<int>(i) % cols;
        auto cell = juce::Rectangle<int>(rack.getX() + c * cellW, rack.getY() + r * cellH, cellW, cellH).reduced(6);
        knobs[i].label->setBounds(cell.removeFromTop(20));
        knobs[i].slider->setBounds(cell);
    }
}

void CoopianoAudioProcessorEditor::importIniFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Load Coopiano Voicing Preset (.ini)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.ini");

    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
                return;
            juce::String err;
            if (!processor_.loadPresetFromFile(file, err))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Preset Load Failed",
                    err);
            }
        });
}

void CoopianoAudioProcessorEditor::exportIniFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export Coopiano Voicing Preset (.ini)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("coopiano_voicing.ini"),
        "*.ini");

    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File{})
                return;
            juce::String err;
            if (!processor_.savePresetToFile(file, err))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Preset Export Failed",
                    err);
            }
        });
}
