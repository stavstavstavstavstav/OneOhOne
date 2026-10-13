#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using Fmt = std::function<juce::String (float, int)>;

    juce::String formatSeconds (double s)
    {
        if (s <= 0.0)  return "0 ms";
        if (s < 0.01)  return juce::String (s * 1000.0, 1) + " ms";
        if (s < 1.0)   return juce::String (juce::roundToInt (s * 1000.0)) + " ms";
        return juce::String (s, 2) + " s";
    }

    juce::String formatHz (double hz)
    {
        if (hz < 10.0)   return juce::String (hz, 2) + " Hz";
        if (hz < 1000.0) return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz / 1000.0, 2) + " kHz";
    }

    const Fmt pct      = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    const Fmt attackF  = [] (float v, int) { return formatSeconds (ooo::mapping::attackSeconds (v)); };
    const Fmt decayF   = [] (float v, int) { return formatSeconds (ooo::mapping::decaySeconds (v)); };
    const Fmt releaseF = [] (float v, int) { return formatSeconds (ooo::mapping::releaseSeconds (v)); };
    const Fmt portaF   = [] (float v, int) { return formatSeconds (ooo::mapping::portaSeconds (v)); };
    const Fmt lfoF     = [] (float v, int) { return formatHz (ooo::mapping::lfoHz (v)); };
    const Fmt cutoffF  = [] (float v, int) { return formatHz (ooo::mapping::cutoffHz (v)); };
    const Fmt tuneF    = [] (float v, int) { return juce::String (juce::roundToInt (ooo::mapping::tuneCents (v))) + " ct"; };
    const Fmt bendF    = [] (float v, int) { return juce::String (ooo::mapping::bendSemitones (v), 1) + " st"; };
}

juce::AudioProcessorValueTreeState::ParameterLayout OneOhOneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;

    auto fader = [&ps] (const char* id, const char* name, float def, const Fmt& fmt)
    {
        ps.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
    };
    auto sw = [&ps] (const char* id, const char* name, const juce::StringArray& choices, int def)
    {
        ps.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
    };

    const ooo::Params d;   // valeurs par defaut definies dans le moteur

    // MODULATOR
    fader ("lfo_rate", "LFO Rate", d.lfoRate, lfoF);
    sw    ("lfo_wave", "LFO Wave", { "Triangle", "Square", "Random", "Noise" }, d.lfoWave);
    // VCO
    sw    ("vco_range", "VCO Range", { "16'", "8'", "4'", "2'" }, d.range);
    fader ("vco_mod", "VCO LFO Mod", d.vcoMod, pct);
    fader ("pw", "Pulse Width", d.pw, pct);
    sw    ("pw_mode", "PWM Mode", { "Env", "Manual", "LFO" }, d.pwMode);
    fader ("tune", "Tune", d.tune, tuneF);
    // SOURCE MIXER
    fader ("mix_pulse", "Pulse Level", d.mixPulse, pct);
    fader ("mix_saw", "Saw Level", d.mixSaw, pct);
    fader ("mix_sub", "Sub Level", d.mixSub, pct);
    fader ("mix_noise", "Noise Level", d.mixNoise, pct);
    sw    ("sub_mode", "Sub Wave", { "-1 oct square", "-2 oct square", "-2 oct pulse" }, d.subMode);
    // VCF
    fader ("vcf_freq", "VCF Freq", d.cutoff, cutoffF);
    fader ("vcf_res", "VCF Res", d.resonance, pct);
    fader ("vcf_env", "VCF Env", d.envAmt, pct);
    fader ("vcf_mod", "VCF LFO Mod", d.lfoAmt, pct);
    fader ("vcf_kybd", "VCF Kybd", d.kybd, pct);
    // VCA / ENV
    sw    ("vca_mode", "VCA Mode", { "Env", "Gate" }, d.vcaMode);
    sw    ("env_trig", "Env Trigger", { "Gate+Trig", "Gate", "LFO" }, d.envTrig);
    fader ("env_a", "Attack", d.attack, attackF);
    fader ("env_d", "Decay", d.decay, decayF);
    fader ("env_s", "Sustain", d.sustain, pct);
    fader ("env_r", "Release", d.release, releaseF);
    // CONTROLLER
    sw    ("porta_mode", "Portamento", { "Auto", "Off", "On" }, d.portaMode);
    fader ("porta_time", "Portamento Time", d.portaTime, portaF);
    fader ("bend_vco", "Bend VCO", d.bendVco, bendF);
    fader ("bend_vcf", "Bend VCF", d.bendVcf, pct);
    fader ("lfo_mod", "Wheel LFO Mod", d.lfoMod, pct);
    fader ("volume", "Volume", d.volume, pct);

    return { ps.begin(), ps.end() };
}

OneOhOneProcessor::OneOhOneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

bool OneOhOneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void OneOhOneProcessor::prepareToPlay (double sampleRate, int)
{
    pushParams();
    synth.prepare (sampleRate);
}

void OneOhOneProcessor::pushParams()
{
    ooo::Params p;
    p.lfoRate   = get ("lfo_rate");   p.lfoWave = getChoice ("lfo_wave");
    p.range     = getChoice ("vco_range");
    p.vcoMod    = get ("vco_mod");
    p.pw        = get ("pw");         p.pwMode = getChoice ("pw_mode");
    p.tune      = get ("tune");
    p.mixPulse  = get ("mix_pulse");  p.mixSaw = get ("mix_saw");
    p.mixSub    = get ("mix_sub");    p.mixNoise = get ("mix_noise");
    p.subMode   = getChoice ("sub_mode");
    p.cutoff    = get ("vcf_freq");   p.resonance = get ("vcf_res");
    p.envAmt    = get ("vcf_env");    p.lfoAmt = get ("vcf_mod");   p.kybd = get ("vcf_kybd");
    p.vcaMode   = getChoice ("vca_mode");
    p.envTrig   = getChoice ("env_trig");
    p.attack    = get ("env_a");      p.decay = get ("env_d");
    p.sustain   = get ("env_s");      p.release = get ("env_r");
    p.portaMode = getChoice ("porta_mode");
    p.portaTime = get ("porta_time");
    p.bendVco   = get ("bend_vco");   p.bendVcf = get ("bend_vcf");  p.lfoMod = get ("lfo_mod");
    p.volume    = get ("volume");
    synth.setParams (p);
}

void OneOhOneProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                                 synth.noteOn (m.getNoteNumber());
    else if (m.isNoteOff())                           synth.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())                        synth.setPitchBend ((m.getPitchWheelValue() - 8192) / 8192.0);
    else if (m.isControllerOfType (1))                synth.setModWheel (m.getControllerValue() / 127.0);
    else if (m.isAllNotesOff() || m.isAllSoundOff())  synth.allNotesOff();
}

void OneOhOneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    pushParams();

    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || n == 0) return;
    float* out = buffer.getWritePointer (0);

    // Rendu sample-accurate : on coupe le bloc a chaque evenement MIDI
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { synth.render (out + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) synth.render (out + pos, n - pos);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
}

void OneOhOneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OneOhOneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* OneOhOneProcessor::createEditor()
{
    return new OneOhOneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OneOhOneProcessor();
}
