#include "PluginEditor.h"

namespace colours
{
    const juce::Colour chassis  { 0xff2e3a42 };
    const juce::Colour plate    { 0xff3a4852 };
    const juce::Colour edge     { 0xff506370 };
    const juce::Colour text     { 0xffece5d3 };
    const juce::Colour muted    { 0xff9fb0ba };
    const juce::Colour cap      { 0xffe0b13f };
    const juce::Colour slot     { 0xff1c252b };
    const juce::Colour tick     { 0xff6c808c };
    const juce::Colour comboBg  { 0xff27323a };
}

namespace layout
{
    constexpr int margin = 12, header = 40, sectionTitle = 22, pad = 8, gap = 8;
    constexpr int colW = 42, minSwitchW = 100;
    constexpr int switchLabelH = 14, switchH = 22, switchGap = 6, switchSlots = 2;
    constexpr int faderH = 180, faderLabelH = 18;
    constexpr int switchBlockH = switchSlots * (switchLabelH + switchH + switchGap);
    constexpr int sectionH = sectionTitle + switchBlockH + faderH + faderLabelH + 2 * pad;
}

// --------------------------------------------------------------------------
PanelLookAndFeel::PanelLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, colours::comboBg);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::edge);
    setColour (juce::ComboBox::arrowColourId, colours::cap);
    setColour (juce::PopupMenu::backgroundColourId, colours::comboBg);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::cap);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::slot);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::BubbleComponent::backgroundColourId, colours::slot);
    setColour (juce::BubbleComponent::outlineColourId, colours::cap);
    setColour (juce::TooltipWindow::textColourId, colours::text);
}

void PanelLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                         float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    const float cx = (float) x + (float) w * 0.5f;

    g.setColour (colours::tick);
    for (int i = 0; i <= 10; ++i)
    {
        const float ty = (float) y + (float) h * (float) i / 10.0f;
        const float len = (i % 5 == 0) ? 7.0f : 4.0f;
        g.drawLine (cx - 9.0f - len, ty, cx - 9.0f, ty, 1.0f);
        g.drawLine (cx + 9.0f, ty, cx + 9.0f + len, ty, 1.0f);
    }

    g.setColour (colours::slot);
    g.fillRoundedRectangle (cx - 2.5f, (float) y - 4.0f, 5.0f, (float) h + 8.0f, 2.5f);

    const float capW = 22.0f, capH = 13.0f;
    const juce::Rectangle<float> capRect (cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
    g.setColour (s.isMouseOverOrDragging() ? colours::cap.brighter (0.15f) : colours::cap);
    g.fillRoundedRectangle (capRect, 2.0f);
    g.setColour (colours::slot);
    g.drawLine (capRect.getX() + 3.0f, capRect.getCentreY(), capRect.getRight() - 3.0f, capRect.getCentreY(), 1.5f);
}

// --------------------------------------------------------------------------
OneOhOneEditor::Fader* OneOhOneEditor::addFader (const juce::String& paramId, const juce::String& text)
{
    auto f = std::make_unique<Fader>();
    f->slider.setSliderStyle (juce::Slider::LinearVertical);
    f->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    f->slider.setPopupDisplayEnabled (true, true, this);
    if (auto* p = proc.apvts.getParameter (paramId))
        f->slider.setDoubleClickReturnValue (true, p->getDefaultValue());
    f->label.setText (text, juce::dontSendNotification);
    f->label.setJustificationType (juce::Justification::centred);
    f->label.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (f->slider);
    addAndMakeVisible (f->label);
    f->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramId, f->slider);
    faders.push_back (std::move (f));
    return faders.back().get();
}

OneOhOneEditor::Switch* OneOhOneEditor::addSwitch (const juce::String& paramId, const juce::String& text)
{
    auto s = std::make_unique<Switch>();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (paramId)))
        s->box.addItemList (choice->choices, 1);
    s->box.setJustificationType (juce::Justification::centred);
    s->label.setText (text, juce::dontSendNotification);
    s->label.setFont (juce::Font (juce::FontOptions (11.0f)));
    s->label.setColour (juce::Label::textColourId, colours::muted);
    s->label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (s->box);
    addAndMakeVisible (s->label);
    s->attachment = std::make_unique<ComboAttachment> (proc.apvts, paramId, s->box);
    switches.push_back (std::move (s));
    return switches.back().get();
}

void OneOhOneEditor::addSection (const juce::String& title, std::vector<Switch*> sw, std::vector<Fader*> fd)
{
    Section s;
    s.title = title;
    s.switches = std::move (sw);
    s.faders = std::move (fd);
    sections.push_back (std::move (s));
}

OneOhOneEditor::OneOhOneEditor (OneOhOneProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addSection ("LFO",
        { addSwitch ("lfo_wave", "Wave") },
        { addFader ("lfo_rate", "Rate") });

    addSection ("VCO",
        { addSwitch ("vco_range", "Range"), addSwitch ("pw_mode", "PWM source") },
        { addFader ("vco_mod", "Mod"), addFader ("pw", "PW"), addFader ("tune", "Tune") });

    addSection ("Mixer",
        { addSwitch ("sub_mode", "Sub wave") },
        { addFader ("mix_pulse", "Pulse"), addFader ("mix_saw", "Saw"), addFader ("mix_sub", "Sub"), addFader ("mix_noise", "Noise") });

    addSection ("VCF",
        {},
        { addFader ("vcf_freq", "Freq"), addFader ("vcf_res", "Res"), addFader ("vcf_env", "Env"),
          addFader ("vcf_mod", "Mod"), addFader ("vcf_kybd", "Kybd") });

    addSection ("Env & VCA",
        { addSwitch ("env_trig", "Trigger"), addSwitch ("vca_mode", "VCA") },
        { addFader ("env_a", "A"), addFader ("env_d", "D"), addFader ("env_s", "S"), addFader ("env_r", "R") });

    addSection ("Bender",
        {},
        { addFader ("bend_vco", "VCO"), addFader ("bend_vcf", "VCF"), addFader ("lfo_mod", "LFO") });

    addSection ("Control",
        { addSwitch ("porta_mode", "Portamento") },
        { addFader ("porta_time", "Glide"), addFader ("volume", "Volume") });

    int total = 2 * layout::margin - layout::gap;
    for (auto& s : sections)
    {
        const int fadersW = (int) s.faders.size() * layout::colW;
        const int switchW = s.switches.empty() ? 0 : layout::minSwitchW;
        s.width = juce::jmax (fadersW, switchW) + 2 * layout::pad;
        total += s.width + layout::gap;
    }

    setSize (total, layout::margin + layout::header + layout::sectionH + layout::margin);
}

OneOhOneEditor::~OneOhOneEditor()
{
    setLookAndFeel (nullptr);
}

void OneOhOneEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::chassis);

    auto head = getLocalBounds().reduced (layout::margin, 0).withTrimmedTop (layout::margin).withHeight (layout::header - 8);
    g.setColour (colours::cap);
    g.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    g.drawText ("OneOhOne", head, juce::Justification::centredLeft);
    g.setColour (colours::muted);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Monophonic synthesizer", head, juce::Justification::centredRight);

    for (const auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (colours::plate);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (colours::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
        g.drawText (s.title, s.bounds.reduced (layout::pad, 0).withHeight (layout::sectionTitle + 4),
                    juce::Justification::centredLeft);
    }
}

void OneOhOneEditor::resized()
{
    int x = layout::margin;
    const int y = layout::margin + layout::header;

    for (auto& s : sections)
    {
        s.bounds = { x, y, s.width, layout::sectionH };
        auto inner = s.bounds.reduced (layout::pad);
        inner.removeFromTop (layout::sectionTitle - layout::pad + 4);

        auto switchArea = inner.removeFromTop (layout::switchBlockH);
        for (auto* sw : s.switches)
        {
            sw->label.setBounds (switchArea.removeFromTop (layout::switchLabelH));
            sw->box.setBounds (switchArea.removeFromTop (layout::switchH));
            switchArea.removeFromTop (layout::switchGap);
        }

        const int fadersW = (int) s.faders.size() * layout::colW;
        auto faderRow = inner.withSizeKeepingCentre (fadersW, inner.getHeight());
        for (auto* f : s.faders)
        {
            auto col = faderRow.removeFromLeft (layout::colW);
            f->label.setBounds (col.removeFromBottom (layout::faderLabelH));
            f->slider.setBounds (col);
        }

        x += s.width + layout::gap;
    }
}
