#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 8; }
};

class OneOhOneEditor : public juce::AudioProcessorEditor
{
public:
    explicit OneOhOneEditor (OneOhOneProcessor&);
    ~OneOhOneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Fader
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Switch
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        std::vector<Switch*> switches;
        std::vector<Fader*> faders;
        juce::Rectangle<int> bounds;
        int width = 0;
    };

    Fader* addFader (const juce::String& paramId, const juce::String& text);
    Switch* addSwitch (const juce::String& paramId, const juce::String& text);
    void addSection (const juce::String& title, std::vector<Switch*> sw, std::vector<Fader*> fd);

    PanelLookAndFeel lnf;
    OneOhOneProcessor& proc;
    std::vector<std::unique_ptr<Fader>> faders;
    std::vector<std::unique_ptr<Switch>> switches;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneOhOneEditor)
};
