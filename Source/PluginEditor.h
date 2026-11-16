#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

class CoopianoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CoopianoLookAndFeel();

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
};

class CoopianoAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit CoopianoAudioProcessorEditor(CoopianoAudioProcessor&);
    ~CoopianoAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct KnobControl
    {
        juce::String paramId;
        juce::String labelText;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct ToggleControl
    {
        juce::String paramId;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void timerCallback() override;
    void selectTab(int tabIndex);
    void updateModeUi();
    void populateSection(int tabIndex, const std::vector<std::pair<const char*, const char*>>& params,
                         const std::vector<std::pair<const char*, const char*>>& toggles = {});
    void importIniFile();
    void exportIniFile();

    CoopianoAudioProcessor& processor_;
    CoopianoLookAndFeel lookAndFeel_;

    juce::TextButton modeButton_{"Play Mode"};
    juce::ComboBox presetCombo_;
    juce::TextButton loadIniButton_{"Import .ini"};
    juce::TextButton saveIniButton_{"Export .ini"};
    juce::TextButton softPedalButton_{"Soft"};
    juce::TextButton sostenutoButton_{"Sost"};
    juce::TextButton sustainButton_{"Sustain"};

    std::array<juce::TextButton, 7> tabButtons_;
    int activeTab_ = 0;

    std::array<std::vector<KnobControl>, 7> tabKnobs_;
    std::array<std::vector<ToggleControl>, 7> tabToggles_;

    juce::MidiKeyboardComponent keyboard_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    float meterL_ = 0.0f;
    float meterR_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CoopianoAudioProcessorEditor)
};
