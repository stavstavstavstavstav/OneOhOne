// OneOhOne : moteur DSP d'un monosynthe inspire du SH-101 (1982).
// Aucun code ni firmware Roland ici : uniquement une modelisation du comportement
// des blocs analogiques, a partir des specs publiques et de schemas publies.
//
// Chaine du signal (comme l'original) :
//   VCO (saw + pulse/PWM, coeur type CEM3340) + sub-osc (diviseurs de frequence)
//   + bruit -> mixer -> VCF 4 poles OTA type IR3109 (ecretage diodes dans la boucle
//   de resonance, compensation de niveau cote sortie) -> VCA -> couplage AC.
// Le moteur tourne en oversampling x2 puis decime.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ooo
{
constexpr double kPi = 3.14159265358979323846;

// --------------------------------------------------------------------------
// Courbes des potards : une seule source de verite, utilisee par le moteur et
// par l'affichage des valeurs dans le plugin. Plages = specs officielles SH-101.
// --------------------------------------------------------------------------
namespace mapping
{
    inline double expMap (double x, double lo, double hi) { return lo * std::pow (hi / lo, x); }

    inline double attackSeconds  (double x) { return expMap (x, 0.0015, 4.0); }  // 1.5 ms .. 4 s
    inline double decaySeconds   (double x) { return expMap (x, 0.002, 10.0); }  // 2 ms .. 10 s
    inline double releaseSeconds (double x) { return expMap (x, 0.002, 10.0); }  // 2 ms .. 10 s
    inline double lfoHz          (double x) { return expMap (x, 0.1, 30.0); }    // 0.1 .. 30 Hz
    inline double cutoffHz       (double x) { return 10.0 * std::exp2 (11.0 * x); } // 10 Hz .. 20.5 kHz
    inline double portaSeconds   (double x) { return x < 0.001 ? 0.0 : expMap (x, 0.001, 5.0); } // 0 .. 5 s
    inline double tuneCents      (double x) { return (x - 0.5) * 100.0; }        // +-50 cents
    inline double bendSemitones  (double x) { return 12.0 * x; }                 // 0 .. 12 demi-tons
    inline double levelGain      (double x) { return x * x; }                    // potard "audio"
    inline double resonanceK     (double x) { return 4.4 * x; }                  // auto-oscillation vers 0.91
}

// --------------------------------------------------------------------------
// Utilitaires
// --------------------------------------------------------------------------
inline double onePoleCoef (double seconds, double fs)
{
    return seconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * fs));
}

// PolyBLEP : corrige une marche de hauteur 2 pour limiter l'aliasing.
inline double polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;             return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return t * t + t + t + 1.0; }
    return 0.0;
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    double bipolar()   // uniforme dans [-1, 1)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s * (2.0 / 4294967296.0) - 1.0;
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double fc, double q, double fs)
    {
        const double w0 = 2.0 * kPi * fc / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = 0.5 * (1.0 - cw) / a0;  b1 = (1.0 - cw) / a0;  b2 = b0;
        a1 = -2.0 * cw / a0;         a2 = (1.0 - alpha) / a0;
    }
    void reset() { z1 = z2 = 0.0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// --------------------------------------------------------------------------
// Enveloppe ADSR analogique : charges/decharges RC exponentielles.
// L'attaque vise 1.3 et bascule en decay a 1.0, comme un circuit a comparateur.
// Un re-trigger repart du niveau courant, pas de zero.
// --------------------------------------------------------------------------
class Envelope
{
public:
    void setSampleRate (double newFs) { fs = newFs; }

    void setParams (double a, double d, double s, double r)
    {
        attackCoef  = onePoleCoef (mapping::attackSeconds (a) / std::log (1.3 / 0.3), fs);
        decayCoef   = onePoleCoef (mapping::decaySeconds (d) / 4.0, fs);
        releaseCoef = onePoleCoef (mapping::releaseSeconds (r) / 4.0, fs);
        sustain = s;
    }

    void gateOn()  { stage = Stage::attack; }
    void gateOff() { if (stage != Stage::idle) stage = Stage::release; }
    void reset()   { stage = Stage::idle; y = 0.0; }

    double tick()
    {
        switch (stage)
        {
            case Stage::attack:
                y += (1.3 - y) * attackCoef;
                if (y >= 1.0) { y = 1.0; stage = Stage::decay; }
                break;
            case Stage::decay:
                y += (sustain - y) * decayCoef;
                break;
            case Stage::release:
                y -= y * releaseCoef;
                if (y < 1.0e-7) { y = 0.0; stage = Stage::idle; }
                break;
            case Stage::idle:
                break;
        }
        return y;
    }

private:
    enum class Stage { idle, attack, decay, release };
    Stage stage = Stage::idle;
    double fs = 96000.0, y = 0.0, sustain = 0.5;
    double attackCoef = 1.0, decayCoef = 1.0, releaseCoef = 1.0;
};

// --------------------------------------------------------------------------
// VCF : 4 etages OTA-C en cascade (IR3109), resolus en zero-delay feedback.
// Non-linearites modelisees :
//  - diodes d'ecretage a la masse dans la boucle de resonance (limite
//    l'amplitude en auto-oscillation, distorsion refiltree par la boucle)
//  - saturation tanh de l'entree OTA (mixer pousse a fond = overdrive)
// --------------------------------------------------------------------------
class IR3109Filter
{
public:
    void reset() { for (auto& v : s) v = 0.0; lastFb = 0.0; }

    double process (double x, double fc, double k, double fs)
    {
        const double g   = std::tan (kPi * fc / fs);
        const double G   = g / (1.0 + g);
        const double inv = 1.0 / (1.0 + g);
        const double G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const double sigma = G3 * s[0] * inv + G2 * s[1] * inv + G * s[2] * inv + s[3] * inv;

        // Linearisation de l'ecretage autour du point de fonctionnement precedent
        const double ratio = std::abs (lastFb) > 1.0e-9 ? diodeClip (lastFb) / lastFb : 1.0;
        const double y4est = (G4 * x + sigma) / (1.0 + k * ratio * G4);
        const double fb    = k * y4est;
        lastFb = fb;

        double in = std::tanh (x - diodeClip (fb));
        for (auto& st : s)
        {
            const double v = (in - st) * G;
            const double y = v + st;
            st = y + v;
            in = y;
        }
        return in;
    }

private:
    static double diodeClip (double v)
    {
        constexpr double knee = 0.6;   // seuil des diodes, en unites internes
        return knee * std::asinh (v / knee);
    }

    double s[4] {};
    double lastFb = 0.0;
};

// --------------------------------------------------------------------------
// Parametres (valeurs brutes des potards 0..1 et index des switches)
// --------------------------------------------------------------------------
struct Params
{
    // MODULATOR
    float lfoRate = 0.45f;  int lfoWave = 0;    // 0 tri, 1 carre, 2 random, 3 bruit
    // VCO
    int   range = 1;                            // 0 16', 1 8', 2 4', 3 2'
    float vcoMod = 0.0f;                        // LFO -> pitch
    float pw = 0.0f;    int pwMode = 1;         // 0 ENV, 1 MAN, 2 LFO
    float tune = 0.5f;
    // SOURCE MIXER
    float mixPulse = 0.0f, mixSaw = 0.8f, mixSub = 0.0f, mixNoise = 0.0f;
    int   subMode = 0;                          // 0 -1 oct carre, 1 -2 oct carre, 2 -2 oct pulse 25%
    // VCF
    float cutoff = 0.55f, resonance = 0.2f, envAmt = 0.35f, lfoAmt = 0.0f, kybd = 0.5f;
    // VCA / ENV
    int   vcaMode = 0;                          // 0 ENV, 1 GATE
    int   envTrig = 0;                          // 0 GATE+TRIG, 1 GATE, 2 LFO
    float attack = 0.0f, decay = 0.5f, sustain = 0.3f, release = 0.3f;
    // CONTROLLER
    int   portaMode = 1;                        // 0 AUTO, 1 OFF, 2 ON
    float portaTime = 0.3f;
    float bendVco = 1.0f / 6.0f, bendVcf = 0.0f, lfoMod = 0.3f;
    float volume = 0.8f;
};

// --------------------------------------------------------------------------
// Synthe monophonique complet
// --------------------------------------------------------------------------
class Synth
{
public:
    static constexpr int kOversample = 2;

    void prepare (double sampleRate)
    {
        fsBase = sampleRate;
        fsInt  = sampleRate * kOversample;
        env.setSampleRate (fsInt);

        // Anti-repliement avant decimation : Butterworth ordre 8
        const double fcAA = std::min (0.45 * fsBase, 20000.0);
        for (int i = 0; i < 4; ++i)
        {
            const double q = 1.0 / (2.0 * std::cos ((2 * i + 1) * kPi / 16.0));
            aa[i].setLowpass (fcAA, q, fsInt);
        }

        smoothCoef = onePoleCoef (0.004, fsInt);
        gateCoef   = onePoleCoef (0.0012, fsInt);
        volCoef    = onePoleCoef (0.01, fsBase);
        driftCoef  = onePoleCoef (1.0 / (2.0 * kPi * 0.25), fsInt);
        driftGain  = 0.02 / std::sqrt (driftCoef / (2.0 - driftCoef) / 3.0);  // ~2 cents RMS
        dcR        = 1.0 - 2.0 * kPi * 8.0 / fsBase;
        fcMax      = std::min (0.42 * fsInt, 24000.0);

        reset();
        setParams (params);
        snapSmoothers();
    }

    void reset()
    {
        filter.reset();  env.reset();
        for (auto& b : aa) b.reset();
        oscPhase = lfoPhase = 0.0;  subCount = 0;
        numHeld = 0;  keyDown = false;  lfoGate = false;  gateSm = 0.0;
        dcX = dcY = 0.0;  driftLp = 0.0;
        currentNote = targetNote = 60.0;
    }

    void setParams (const Params& p)
    {
        params = p;
        env.setParams (p.attack, p.decay, p.sustain, p.release);
        lfoInc = mapping::lfoHz (p.lfoRate) / fsInt;

        const double glide = mapping::portaSeconds (p.portaTime);
        glideCoef = onePoleCoef (glide / 3.0, fsInt);

        static constexpr double rangeOffsets[] = { -12.0, 0.0, 12.0, 24.0 };
        rangeOffset  = rangeOffsets[std::clamp (p.range, 0, 3)];
        tuneSemis    = mapping::tuneCents (p.tune) / 100.0;
        vcoModSemis  = 12.0 * p.vcoMod * p.vcoMod;
        wheelSemis   = 12.0 * p.lfoMod * p.lfoMod;
        bendVcoSemis = mapping::bendSemitones (p.bendVco);

        tgt.pulse  = mapping::levelGain (p.mixPulse);
        tgt.saw    = mapping::levelGain (p.mixSaw);
        tgt.sub    = mapping::levelGain (p.mixSub);
        tgt.noise  = mapping::levelGain (p.mixNoise);
        tgt.cutoff = p.cutoff;
        tgt.k      = mapping::resonanceK (p.resonance);
        tgt.envAmt = p.envAmt;
        tgt.lfoAmt = p.lfoAmt;
        tgt.pw     = p.pw;
    }

    // ---- MIDI ----
    void noteOn (int note)
    {
        const bool legato = numHeld > 0;
        removeHeld (note);
        if (numHeld < kMaxHeld) held[numHeld++] = note;
        setTarget (note, legato);
        keyDown = true;
        if (params.envTrig == 0 || (params.envTrig == 1 && ! legato))
            env.gateOn();
    }

    void noteOff (int note)
    {
        removeHeld (note);
        if (numHeld > 0) { setTarget (held[numHeld - 1], true); return; }  // derniere note tenue
        keyDown = false;
        env.gateOff();
    }

    void allNotesOff() { numHeld = 0; keyDown = false; env.gateOff(); }
    void setPitchBend (double v) { bendTarget = std::clamp (v, -1.0, 1.0); }
    void setModWheel (double v)  { wheelTarget = std::clamp (v, 0.0, 1.0); }

    // ---- Audio ----
    void render (float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            double y = 0.0;
            for (int o = 0; o < kOversample; ++o)
            {
                y = tick();
                for (auto& b : aa) y = b.process (y);
            }
            // condensateur de sortie (couplage AC)
            const double hp = y - dcX + dcR * dcY;
            dcX = y;  dcY = hp;

            volSm += (params.volume * params.volume - volSm) * volCoef;
            out[i] = (float) (hp * volSm * kOutputGain);
        }
    }

private:
    static constexpr int kMaxHeld = 32;
    static constexpr double kMixerGain = 0.5;    // niveau mixer -> entree VCF
    static constexpr double kResComp   = 0.4;    // compensation de resonance cote sortie
    static constexpr double kOutputGain = 1.2;

    struct Smoothed { double pulse = 0, saw = 0, sub = 0, noise = 0, cutoff = 0, k = 0, envAmt = 0, lfoAmt = 0, pw = 0; };

    void snapSmoothers() { sm = tgt; volSm = params.volume * params.volume; bend = bendTarget; wheel = wheelTarget; }

    void removeHeld (int note)
    {
        int w = 0;
        for (int r = 0; r < numHeld; ++r)
            if (held[r] != note) held[w++] = held[r];
        numHeld = w;
    }

    void setTarget (int note, bool legato)
    {
        targetNote = note;
        glideActive = params.portaMode == 2 || (params.portaMode == 0 && legato);
        if (! glideActive) currentNote = targetNote;
    }

    double subValue (int c) const
    {
        switch (params.subMode)
        {
            case 0:  return (c & 1) ? -1.0 : 1.0;   // -1 octave, carre
            case 1:  return (c & 2) ? -1.0 : 1.0;   // -2 octaves, carre
            default: return c == 0 ? 1.0 : -1.0;    // -2 octaves, pulse 25 %
        }
    }

    double tick()
    {
        const Params& p = params;

        // Lissage des potards (evite le zipper noise)
        const double a = smoothCoef;
        sm.pulse  += (tgt.pulse  - sm.pulse)  * a;
        sm.saw    += (tgt.saw    - sm.saw)    * a;
        sm.sub    += (tgt.sub    - sm.sub)    * a;
        sm.noise  += (tgt.noise  - sm.noise)  * a;
        sm.cutoff += (tgt.cutoff - sm.cutoff) * a;
        sm.k      += (tgt.k      - sm.k)      * a;
        sm.envAmt += (tgt.envAmt - sm.envAmt) * a;
        sm.lfoAmt += (tgt.lfoAmt - sm.lfoAmt) * a;
        sm.pw     += (tgt.pw     - sm.pw)     * a;
        bend      += (bendTarget  - bend)     * a;
        wheel     += (wheelTarget - wheel)    * a;

        // ---- LFO (free running) ----
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) { lfoPhase -= 1.0; lfoRandom = rng.bipolar(); }
        double lfo;
        switch (p.lfoWave)
        {
            case 0:  lfo = 1.0 - 4.0 * std::abs (lfoPhase - 0.5); break;
            case 1:  lfo = lfoPhase < 0.5 ? 1.0 : -1.0;           break;
            case 2:  lfo = lfoRandom;                              break;
            default: lfo = rng.bipolar();                          break;
        }

        // ---- Trigger d'enveloppe par le LFO ----
        if (p.envTrig == 2)
        {
            const bool g = keyDown && lfoPhase < 0.5;
            if (g && ! lfoGate) env.gateOn();
            if (! g && lfoGate) env.gateOff();
            lfoGate = g;
        }
        else
        {
            lfoGate = false;
        }

        const double e = env.tick();

        // ---- Pitch : portamento = lag RC sur le CV (temps constant) ----
        if (glideActive) currentNote += (targetNote - currentNote) * glideCoef;
        else             currentNote = targetNote;

        driftLp += (rng.bipolar() - driftLp) * driftCoef;

        const double semis = currentNote + rangeOffset + tuneSemis
                           + bend * bendVcoSemis
                           + lfo * (vcoModSemis + wheel * wheelSemis)
                           + driftLp * driftGain;
        const double dt = std::min (440.0 * std::exp2 ((semis - 69.0) / 12.0) / fsInt, 0.45);

        // ---- VCO ----
        oscPhase += dt;
        if (oscPhase >= 1.0) { oscPhase -= 1.0; subCount = (subCount + 1) & 3; }
        const double t = oscPhase;

        const double saw = 2.0 * t - 1.0 - polyBlep (t, dt);

        double pwMod;
        switch (p.pwMode)
        {
            case 0:  pwMod = e;                  break;  // ENV
            case 1:  pwMod = 1.0;                break;  // MANUAL
            default: pwMod = 0.5 * (lfo + 1.0);  break;  // LFO
        }
        const double width = std::clamp (0.5 - 0.46 * sm.pw * pwMod, 0.04, 0.5);
        double t2 = t - width;
        if (t2 < 0.0) t2 += 1.0;
        const double pulse = (t < width ? 1.0 : -1.0) + polyBlep (t, dt) - polyBlep (t2, dt);

        // Sub-osc : bascules qui divisent le VCO, donc verrouille en phase
        double sub = subValue (subCount);
        if (t < dt)            sub += 0.5 * (sub - subValue ((subCount + 3) & 3)) * polyBlep (t, dt);
        else if (t > 1.0 - dt) sub += 0.5 * (subValue ((subCount + 1) & 3) - sub) * polyBlep (t, dt);

        const double noise = rng.bipolar();

        // ---- Source mixer ----
        const double mix = (sm.pulse * pulse + sm.saw * saw + sm.sub * sub + sm.noise * noise) * kMixerGain;

        // ---- VCF ----
        const double octaves = 11.0 * sm.cutoff
                             + 10.0 * sm.envAmt * e
                             + 4.0 * sm.lfoAmt * lfo
                             + p.kybd * (currentNote - 60.0) / 12.0
                             + 3.0 * p.bendVcf * bend;
        const double fc = std::clamp (10.0 * std::exp2 (octaves), 8.0, fcMax);
        const double vcf = filter.process (mix, fc, sm.k, fsInt) * (1.0 + kResComp * sm.k);

        // ---- VCA (OTA, legere saturation) ----
        gateSm += ((keyDown ? 1.0 : 0.0) - gateSm) * gateCoef;
        const double amp = p.vcaMode == 0 ? e : gateSm;
        return std::tanh (vcf * amp);
    }

    Params params;
    Envelope env;
    IR3109Filter filter;
    Biquad aa[4];
    Rng rng;

    Smoothed sm, tgt;
    double fsBase = 48000.0, fsInt = 96000.0;
    double smoothCoef = 0.01, gateCoef = 0.01, volCoef = 0.01, volSm = 0.0;
    double driftCoef = 0.0, driftGain = 0.0, driftLp = 0.0;
    double dcR = 0.999, dcX = 0.0, dcY = 0.0, fcMax = 20000.0;

    double oscPhase = 0.0;  int subCount = 0;
    double lfoPhase = 0.0, lfoInc = 0.0, lfoRandom = 0.0;
    bool   lfoGate = false;

    int  held[kMaxHeld] {};  int numHeld = 0;
    bool keyDown = false, glideActive = false;
    double currentNote = 60.0, targetNote = 60.0, glideCoef = 1.0;
    double rangeOffset = 0.0, tuneSemis = 0.0, vcoModSemis = 0.0, wheelSemis = 0.0, bendVcoSemis = 2.0;
    double bend = 0.0, bendTarget = 0.0, wheel = 0.0, wheelTarget = 0.0;
    double gateSm = 0.0;
};

} // namespace ooo
