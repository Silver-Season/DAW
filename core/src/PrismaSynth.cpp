#include "daw/synth/PrismaSynth.h"

#include <algorithm>
#include <cmath>

namespace daw::synth
{

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    inline float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
    inline float hzToNote (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    // Pitch of a note with A4 tuned to `a4` Hz. Filter cutoffs keep the 440 Hz mapping above: they are not notes.
    inline float tunedHz (float note, float a4) noexcept { return a4 * std::exp2 ((note - 69.0f) / 12.0f); }

    inline int asInt (float v) noexcept { return (int) std::lround (v); }
}

//==============================================================================
void PrismaVoice::prepare (double sr)
{
    sampleRate = sr;
    kill();
}

void PrismaVoice::start (int newNote, float vel, float glideFrom, bool legato, uint64_t newAge, uint32_t seed, const VoiceContext& ctx)
{
    const auto& p = *ctx.params;
    note = newNote;
    velocity = vel;
    age = newAge;
    released = false;
    targetPitch = (float) newNote;
    currentPitch = (glideFrom >= 0.0f && p.glide > 0.0f) ? glideFrom : targetPitch;

    if (legato && amp.isActive())
        return; // keep envelopes, phases and LFOs running, only the pitch moves

    noiseSeed = seed | 1u;
    randomValue = (float) ((seed >> 8) & 0xffff) / 32767.5f - 1.0f;

    for (int o = 0; o < 2; ++o)
        osc[o].reset (p.osc[(size_t) o].phase, p.osc[(size_t) o].randPhase, seed * (uint32_t) (o + 7));

    for (int l = 0; l < 2; ++l)
    {
        if (p.lfo[(size_t) l].retrig > 0.5f || ! amp.isActive())
            lfo[l].reset (0.0);
        lfo[l].setSeed (seed + (uint32_t) l * 977u);
    }

    if (! amp.isActive())
    {
        filterL.reset();
        filterR.reset();
        smoothedCutoff = -1.0f;
        smoothedPos[0] = smoothedPos[1] = -1.0f;
        subPhase = 0.0;
    }

    amp.noteOn();
    env2.noteOn();
    env3.noteOn();
}

void PrismaVoice::release()
{
    released = true;
    amp.noteOff();
    env2.noteOff();
    env3.noteOff();
}

void PrismaVoice::kill()
{
    amp.reset();
    env2.reset();
    env3.reset();
    note = -1;
    released = false;
}

void PrismaVoice::render (float* outL, float* outR, int numSamples, const VoiceContext& ctx)
{
    const auto& p = *ctx.params;
    const float sr = (float) sampleRate;

    amp.setParameters (p.env[0].attack, p.env[0].decay, p.env[0].sustain, p.env[0].release, sr);
    env2.setParameters (p.env[1].attack, p.env[1].decay, p.env[1].sustain, p.env[1].release, sr);
    env3.setParameters (p.env[2].attack, p.env[2].decay, p.env[2].sustain, p.env[2].release, sr);

    for (int o = 0; o < 2; ++o)
    {
        const auto& op = p.osc[(size_t) o];
        osc[o].setTable ((*ctx.tables)[(size_t) std::clamp (asInt (op.table), 0, (int) ctx.tables->size() - 1)]);
        osc[o].setUnison (asInt (op.unison), op.detune, op.spread, op.blend);
    }

    const auto filterType = (dsp::FilterType) std::clamp (asInt (p.filterType), 0, (int) dsp::FilterType::count - 1);
    const bool filterOn = p.filterOn > 0.5f;
    const bool oscOn[2] { p.osc[0].on > 0.5f, p.osc[1].on > 0.5f };
    const bool subOn = p.subOn > 0.5f;
    const float velGain = 1.0f - p.velocitySens + p.velocitySens * velocity;
    const float glideCoef = p.glide > 0.0f ? 1.0f - std::exp (-(float) kControlBlock / (p.glide * 0.25f * sr)) : 1.0f;

    std::array<float, (size_t) ModSource::count> src {};
    std::array<float, (size_t) ModDest::count> dst {};

    auto computeDestinations = [&] {
        dst.fill (0.0f);
        for (const auto& slot : p.mod)
        {
            const int s = asInt (slot.source), d = asInt (slot.dest);
            if (s <= 0 || d <= 0 || s >= (int) ModSource::count || d >= (int) ModDest::count)
                continue;
            dst[(size_t) d] += src[(size_t) s] * slot.amount;
        }
    };

    for (int pos = 0; pos < numSamples && amp.isActive();)
    {
        const int len = std::min (kControlBlock, numSamples - pos);

        // --- control rate: modulation sources ---------------------------------
        src[(size_t) ModSource::Env2] = env2.advance (len);
        src[(size_t) ModSource::Env3] = env3.advance (len);
        src[(size_t) ModSource::Lfo1] = lfoValue[0];
        src[(size_t) ModSource::Lfo2] = lfoValue[1];
        src[(size_t) ModSource::Velocity] = velocity;
        src[(size_t) ModSource::ModWheel] = ctx.modWheel;
        src[(size_t) ModSource::Aftertouch] = ctx.aftertouch;
        src[(size_t) ModSource::KeyTrack] = ((float) note - 60.0f) / 60.0f;
        src[(size_t) ModSource::Random] = randomValue;
        computeDestinations();

        for (int l = 0; l < 2; ++l)
        {
            const auto& lp = p.lfo[(size_t) l];
            double rate = lp.sync > 0.5f ? (ctx.bpm / 60.0) / syncDivisionBeats (asInt (lp.syncDiv)) : (double) lp.rate;
            rate *= std::exp2 (4.0f * dst[(size_t) (l == 0 ? ModDest::Lfo1Rate : ModDest::Lfo2Rate)]);
            lfoValue[l] = lfo[l].advance ((dsp::LfoShape) std::clamp (asInt (lp.shape), 0, (int) dsp::LfoShape::count - 1), rate, len, sampleRate);
        }
        src[(size_t) ModSource::Lfo1] = lfoValue[0];
        src[(size_t) ModSource::Lfo2] = lfoValue[1];
        computeDestinations();

        // --- control rate: derived values -------------------------------------
        currentPitch += (targetPitch - currentPitch) * glideCoef;
        const float pitch = currentPitch + ctx.pitchBendSemis;

        double freq[2];
        float wtPos[2], level[2];
        for (int o = 0; o < 2; ++o)
        {
            const auto& op = p.osc[(size_t) o];
            const auto pitchDest = o == 0 ? ModDest::OscAPitch : ModDest::OscBPitch;
            const auto posDest = o == 0 ? ModDest::OscAPos : ModDest::OscBPos;
            const auto levelDest = o == 0 ? ModDest::OscALevel : ModDest::OscBLevel;
            const float n = pitch + 12.0f * op.octave + op.semi + op.fine * 0.01f + 24.0f * dst[(size_t) pitchDest];
            freq[o] = tunedHz (n, ctx.tuneA4);
            const float targetPos = std::clamp (op.wtPos + dst[(size_t) posDest], 0.0f, 1.0f);
            smoothedPos[o] = smoothedPos[o] < 0.0f ? targetPos : smoothedPos[o] + (targetPos - smoothedPos[o]) * 0.35f;
            wtPos[o] = smoothedPos[o];
            level[o] = std::clamp (op.level + dst[(size_t) levelDest], 0.0f, 1.0f);
        }

        const float subLevel = std::clamp (p.subLevel + dst[(size_t) ModDest::SubLevel], 0.0f, 1.0f);
        const float noiseLevel = std::clamp (p.noiseLevel + dst[(size_t) ModDest::NoiseLevel], 0.0f, 1.0f);
        const double subInc = tunedHz (pitch - 12.0f * (float) (1 + asInt (p.subOctave)), ctx.tuneA4) / sampleRate;

        if (filterOn)
        {
            const float cutoffNote = hzToNote (p.cutoff) + p.keyTrack * ((float) note - 60.0f)
                                     + 60.0f * p.filterEnvAmount * src[(size_t) ModSource::Env2]
                                     + 60.0f * dst[(size_t) ModDest::FilterCutoff];
            smoothedCutoff = smoothedCutoff < 0.0f ? cutoffNote : smoothedCutoff + (cutoffNote - smoothedCutoff) * 0.4f;
            const float hz = std::clamp (noteToHz (smoothedCutoff), 15.0f, 20000.0f);
            const float res = std::clamp (p.resonance + dst[(size_t) ModDest::FilterResonance], 0.0f, 1.0f);
            const float drive = std::clamp (p.drive + dst[(size_t) ModDest::FilterDrive], 0.0f, 1.0f);
            filterL.set (filterType, hz, res, drive, sr);
            filterR.set (filterType, hz, res, drive, sr);
        }

        const float ampMod = std::clamp (1.0f + dst[(size_t) ModDest::AmpLevel], 0.0f, 2.0f);
        const float pan = std::clamp (dst[(size_t) ModDest::Pan], -1.0f, 1.0f);
        const float panL = std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        const float panR = std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        float oscPanL[2], oscPanR[2];
        for (int o = 0; o < 2; ++o)
        {
            const float op = p.osc[(size_t) o].pan;
            oscPanL[o] = level[o] * std::min (1.0f, 1.0f - op);
            oscPanR[o] = level[o] * std::min (1.0f, 1.0f + op);
        }

        // --- audio rate -------------------------------------------------------
        for (int i = pos; i < pos + len; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int o = 0; o < 2; ++o)
            {
                if (! oscOn[o])
                    continue;
                float ol, orr;
                osc[o].tick (freq[o], wtPos[o], sampleRate, ol, orr);
                l += ol * oscPanL[o];
                r += orr * oscPanR[o];
            }

            if (subOn)
            {
                const float s = std::sin (kTwoPi * (float) subPhase) * subLevel;
                subPhase += subInc;
                if (subPhase >= 1.0)
                    subPhase -= 1.0;
                l += s;
                r += s;
            }

            if (noiseLevel > 0.0f)
            {
                noiseSeed = noiseSeed * 1664525u + 1013904223u;
                const float n = ((float) (noiseSeed >> 8) / 8388608.0f - 1.0f) * noiseLevel;
                l += n;
                r += n;
            }

            if (filterOn)
            {
                l = filterL.process (l);
                r = filterR.process (r);
            }

            const float g = amp.next() * velGain * ampMod * 0.5f;
            outL[i] += l * g * panL;
            outR[i] += r * g * panR;
        }

        pos += len;
    }

    if (! amp.isActive())
        note = -1;
}

//==============================================================================
PrismaSynth::PrismaSynth()
{
    for (int i = 0; i < (int) dsp::factory::tableNames().size(); ++i)
    {
        tableRefs.push_back (dsp::factory::table (i));
        tables.push_back (tableRefs.back().get());
    }
    ctx.params = &params;
    ctx.tables = &tables;
}

const dsp::Wavetable* PrismaSynth::getTable (int index) const
{
    return tables[(size_t) std::clamp (index, 0, (int) tables.size() - 1)];
}

void PrismaSynth::prepare (double sampleRate, int)
{
    ctx.sampleRate = sampleRate;
    for (auto& v : voices)
        v.prepare (sampleRate);
    reset();
}

void PrismaSynth::reset()
{
    for (auto& v : voices)
        v.kill();
    sustainedNotes.fill (false);
    monoStackSize = 0;
    sustainPedal = false;
    lastPitch = -1.0f;
    ctx.pitchBendSemis = 0.0f;
}

void PrismaSynth::allNotesOff()
{
    for (auto& v : voices)
        if (v.isActive())
            v.release();
    sustainedNotes.fill (false);
    monoStackSize = 0;
}

int PrismaSynth::getNumActiveVoices() const
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const PrismaVoice& v) { return v.isActive(); });
}

void PrismaSynth::process (float* outL, float* outR, int numSamples, const MidiEvent* events, int numEvents)
{
    std::fill (outL, outL + numSamples, 0.0f);
    std::fill (outR, outR + numSamples, 0.0f);

    int pos = 0, ev = 0;
    while (pos < numSamples)
    {
        while (ev < numEvents && events[ev].sampleOffset <= pos)
            handleEvent (events[ev++]);

        const int next = ev < numEvents ? std::min (numSamples, std::max (pos + 1, events[ev].sampleOffset)) : numSamples;
        renderVoices (outL + pos, outR + pos, next - pos);
        pos = next;
    }
    while (ev < numEvents)
        handleEvent (events[ev++]);

    // Master gain with a linear ramp across the block to avoid zipper noise.
    const float target = std::pow (10.0f, params.masterGain / 20.0f);
    const float start = currentGain;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = start + (target - start) * (float) (i + 1) / (float) numSamples;
        outL[i] *= g;
        outR[i] *= g;
    }
    currentGain = target;
}

void PrismaSynth::renderVoices (float* outL, float* outR, int numSamples)
{
    for (auto& v : voices)
        if (v.isActive())
            v.render (outL, outR, numSamples, ctx);
}

void PrismaSynth::handleEvent (const MidiEvent& e)
{
    const int type = e.status & 0xF0;
    switch (type)
    {
        case 0x90:
            if (e.data2 > 0)
                noteOn (e.data1, (float) e.data2 / 127.0f);
            else
                noteOff (e.data1);
            break;
        case 0x80: noteOff (e.data1); break;
        case 0xB0:
            if (e.data1 == 1)
                ctx.modWheel = (float) e.data2 / 127.0f;
            else if (e.data1 == 64)
            {
                sustainPedal = e.data2 >= 64;
                if (! sustainPedal)
                    for (int n = 0; n < 128; ++n)
                        if (sustainedNotes[(size_t) n])
                        {
                            sustainedNotes[(size_t) n] = false;
                            noteOff (n);
                        }
            }
            else if (e.data1 == 120 || e.data1 == 123)
                allNotesOff();
            break;
        case 0xD0: ctx.aftertouch = (float) e.data1 / 127.0f; break;
        case 0xE0:
        {
            const int value = (e.data2 << 7) | e.data1; // 0 .. 16383, centre 8192
            ctx.pitchBendSemis = (float) (value - 8192) / 8192.0f * params.bendRange;
            break;
        }
        default: break;
    }
}

PrismaVoice* PrismaSynth::findVoiceToUse()
{
    const int poly = std::clamp (asInt (params.polyphony), 1, kMaxVoices);

    int active = 0;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[(size_t) i].isActive())
            ++active;

    if (active < poly)
        for (auto& v : voices)
            if (! v.isActive())
                return &v;

    // Steal: the oldest released voice first, then the oldest voice.
    PrismaVoice* best = nullptr;
    for (auto& v : voices)
        if (v.isActive() && v.isReleased() && (best == nullptr || v.getAge() < best->getAge()))
            best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (v.isActive() && (best == nullptr || v.getAge() < best->getAge()))
                best = &v;
    return best;
}

void PrismaSynth::noteOn (int note, float velocity)
{
    sustainedNotes[(size_t) note] = false;
    seedCounter = seedCounter * 1664525u + 1013904223u;
    const uint32_t seed = seedCounter;

    if (params.mono > 0.5f)
    {
        auto& v = voices[0];
        const bool wasHeld = monoStackSize > 0;
        // Move the note to the top of the stack.
        auto end = monoStack.begin() + monoStackSize;
        auto it = std::remove (monoStack.begin(), end, note);
        monoStackSize = (int) (it - monoStack.begin());
        if (monoStackSize < (int) monoStack.size())
            monoStack[(size_t) monoStackSize++] = note;

        const float from = v.isActive() ? v.getCurrentPitch() : -1.0f;
        v.start (note, velocity, from, params.legato > 0.5f && wasHeld, ++ageCounter, seed, ctx);
        lastPitch = (float) note;
        return;
    }

    // Retrigger the same note instead of stacking duplicates.
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note)
        {
            v.start (note, velocity, -1.0f, false, ++ageCounter, seed, ctx);
            return;
        }

    if (auto* v = findVoiceToUse())
    {
        v->start (note, velocity, lastPitch, false, ++ageCounter, seed, ctx);
        lastPitch = (float) note;
    }
}

void PrismaSynth::noteOff (int note)
{
    if (sustainPedal)
    {
        sustainedNotes[(size_t) note] = true;
        return;
    }

    if (params.mono > 0.5f)
    {
        auto end = monoStack.begin() + monoStackSize;
        monoStackSize = (int) (std::remove (monoStack.begin(), end, note) - monoStack.begin());
        auto& v = voices[0];
        if (! v.isActive() || v.getNote() != note)
            return;
        if (monoStackSize > 0)
            v.start (monoStack[(size_t) monoStackSize - 1], 0.8f, v.getCurrentPitch(), true, ++ageCounter, seedCounter, ctx);
        else
            v.release();
        return;
    }

    for (auto& v : voices)
        if (v.isActive() && ! v.isReleased() && v.getNote() == note)
            v.release();
}

} // namespace daw::synth
