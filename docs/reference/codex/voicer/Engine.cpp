#include "Engine.h"
namespace voice
{
void
Engine::prepare (double sr)
{
    sr = sr > 0 ? sr : 48000;
    rate = sr;
    detector.prepare (sr);
    correction = envelope = 0;
    wetMix.reset (sr, .015);
    wetMix.setCurrentAndTargetValue (1);
    outputLevel.reset (sr, .015);
    outputLevel.setCurrentAndTargetValue (1);
    essEnvelope.fill (0);
    preLow.fill (0);
    dryPos = fxPos = 0;
    // Explicit live-sized window, rather than the library's 120 ms default preset.
    const int window = std::max (512, (int)(sr * .040));
    shift.configure (2, window, std::max (64, window / 8));
    latencySamples = shift.inputLatency () + shift.outputLatency ();
    for (auto& v : dryDelay)
        v.assign ((size_t)latencySamples + 1, 0);
    for (auto& v : effectDelay)
        v.assign ((size_t)(rate * .35) + 128, 0);
    reverb.setSampleRate (sr);
    reverb.reset ();
    detectedNote = targetNote = -1;
    confidence = lastShift = 0;
}
void
Engine::process (const float* const* in,
                 float* const* out,
                 int channels,
                 int count,
                 const Settings& s,
                 float manualTarget)
{
    constexpr int chunk = 64;
    std::array<std::array<float, chunk>, 2> input{}, wet{}, dry{};
    juce::Reverb::Parameters rp;
    rp.roomSize = .55f;
    rp.damping = .6f;
    rp.wetLevel = s.space * .01f;
    rp.dryLevel = 1 - rp.wetLevel * .45f;
    rp.width = 1;
    reverb.setParameters (rp);
    const float lowCoeff = (float)(1 - std::exp (-2 * juce::MathConstants<double>::pi * 4000 / rate));
    const float envCoeff = (float)(1 - std::exp (-1 / (rate * .008)));
    const float amount = std::clamp (s.amount * .01f, 0.0f, 1.0f);
    wetMix.setTargetValue (s.bypass ? 0 : s.mix * .01f);
    outputLevel.setTargetValue (s.bypass ? 1 : juce::Decibels::decibelsToGain (s.output));
    for (int start = 0; start < count; start += chunk)
    {
        const int n = std::min (chunk, count - start);
        for (int i = 0; i < n; ++i)
        {
            float mono = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                float v = in[std::min (ch, channels - 1)][start + i];
                if (!std::isfinite (v))
                    v = 0;
                v = std::clamp (v, -8.0f, 8.0f);
                mono += v * .5f;
                dryDelay[(size_t)ch][(size_t)dryPos] = v;
                dry[(size_t)ch][(size_t)i] = dryDelay[(size_t)ch][(size_t)((dryPos + 1) % (latencySamples + 1))];
                preLow[(size_t)ch] += lowCoeff * (v - preLow[(size_t)ch]);
                const float hf = v - preLow[(size_t)ch];
                auto& ess = essEnvelope[(size_t)ch];
                const float essTime = std::abs (hf) > ess ? .002f : .060f;
                ess += (float)(1 - std::exp (-1 / (rate * essTime))) * (std::abs (hf) - ess);
                input[(size_t)ch][(size_t)i] =
                  v - hf * std::clamp ((ess - .020f) * 15.0f, 0.0f, 1.0f) * (s.deess * .008f);
            }
            dryPos = (dryPos + 1) % (latencySamples + 1);
            envelope += envCoeff * (std::abs (mono) - envelope);
            const float db = juce::Decibels::gainToDecibels (envelope, -100.0f);
            const float reduction = std::max (0.0f, db + 18) * (.65f * s.compress * .01f);
            const float cg = juce::Decibels::decibelsToGain (-reduction);
            for (int ch = 0; ch < 2; ++ch)
                input[(size_t)ch][(size_t)i] *= cg;
            if (detector.push (mono))
            {
                confidence = detector.confidence;
                detectedNote = midiFromHz (detector.frequency);
                targetNote = detectedNote >= 0 ? (float)nearestNote (detectedNote, s.root, s.scale) : -1;
            }
        }
        float desired = 0;
        const auto target = manualTarget >= 0 ? manualTarget : targetNote;
        if (detectedNote >= 0 && confidence > .85f && target >= 0)
        {
            desired = std::clamp (target - detectedNote, -12.0f, 12.0f);
            const float deadzone = s.human * .0025f;
            desired = std::copysign (std::max (0.0f, std::abs (desired) - deadzone), desired) * amount;
        }
        const float a = s.speed <= 0 ? 1.0f : (float)(1 - std::exp (-(double)n / (rate * s.speed * .001)));
        correction += a * (desired - correction);
        lastShift = std::clamp (correction + s.transpose, -24.0f, 24.0f);
        shift.setTransposeSemitones (lastShift);
        shift.setFormantSemitones (s.formant, s.preserve);
        shift.setFormantBase (detector.frequency > 0 ? detector.frequency / (float)rate : 180.0f / (float)rate);
        const float* ptrs[2]{ input[0].data (), input[1].data () };
        float* wptrs[2]{ wet[0].data (), wet[1].data () };
        shift.process (ptrs, n, wptrs, n);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& v = wet[(size_t)ch][(size_t)i];
                const float d = 1 + s.drive * .04f;
                if (s.drive > .01f)
                    v = std::tanh (v * d) / std::tanh (d);
                auto& delay = effectDelay[(size_t)ch];
                const int size = (int)delay.size ();
                const float echo = delay[(size_t)((fxPos - (int)(rate * .240) + size) % size)];
                const float doubled = delay[(size_t)((fxPos - (int)(rate * (ch == 0 ? .013 : .021)) + size) % size)];
                delay[(size_t)fxPos] = v + echo * .30f;
                v += echo * s.echo * .01f + doubled * s.doubleVoice * .007f;
            }
            fxPos = (fxPos + 1) % (int)effectDelay[0].size ();
        }
        reverb.processStereo (wet[0].data (), wet[1].data (), n);
        for (int i = 0; i < n; ++i)
        {
            const float mix = wetMix.getNextValue (), outputGain = outputLevel.getNextValue ();
            for (int ch = 0; ch < channels; ++ch)
            {
                const float v =
                  ((1 - mix) * dry[(size_t)ch][(size_t)i] + mix * wet[(size_t)ch][(size_t)i]) * outputGain;
                out[ch][start + i] = std::isfinite (v) ? std::clamp (v, -8.0f, 8.0f) : 0;
            }
        }
    }
}
}
