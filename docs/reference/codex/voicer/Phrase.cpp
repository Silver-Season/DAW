#include "Phrase.h"
namespace voice
{
std::shared_ptr<Phrase>
analyse (std::shared_ptr<const juce::AudioBuffer<float>> audio, double sr)
{
    auto p = std::make_shared<Phrase> ();
    p->audio = audio;
    p->rate = sr;
    PitchDetector detector;
    detector.prepare (sr);
    Note current;
    bool active = false;
    auto finish = [&]
    {
        if (active && current.end - current.start > sr * .065)
            p->notes.push_back (current);
        active = false;
    };
    for (int i = 0; i < audio->getNumSamples (); ++i)
    {
        float mono = 0;
        for (int ch = 0; ch < audio->getNumChannels (); ++ch)
            mono += audio->getSample (ch, i) / (float)audio->getNumChannels ();
        if (!detector.push (mono))
            continue;
        const float note = midiFromHz (detector.frequency);
        const int at = std::max (0, i - (int)(sr * .032));
        if (note < 0 || detector.confidence < .85f)
        {
            finish ();
            continue;
        }
        if (active && (int)std::round (note) == (int)std::round (current.detected))
        {
            current.end = at;
            current.detected = .9f * current.detected + .1f * note;
        }
        else
        {
            finish ();
            current = { at, at, note, (float)std::round (note), false };
            active = true;
        }
    }
    finish ();
    return p;
}
juce::AudioBuffer<float>
render (const Phrase& p, const Settings& settings)
{
    Engine engine;
    engine.prepare (p.rate);
    const int channels = p.audio->getNumChannels (), original = p.audio->getNumSamples ();
    const double tail = settings.space > 0 ? 4.0 : settings.echo > 0 ? 2.0 : settings.doubleVoice > 0 ? .04 : 0;
    const int total = original + (int)(tail * p.rate);
    juce::AudioBuffer<float> result (channels, total), scratch (channels, 128), input (channels, 128);
    const int extra = engine.latency ();
    size_t note = 0;
    // Flush delayed output and compensate the reported algorithmic delay in the exported file.
    for (int start = 0; start < total + extra; start += 128)
    {
        const int count = std::min (128, total + extra - start);
        input.clear ();
        if (start < original)
            for (int ch = 0; ch < channels; ++ch)
                input.copyFrom (ch, 0, *p.audio, ch, start, std::min (count, original - start));
        const int at = start - engine.controlLatency ();
        while (note < p.notes.size () && p.notes[note].end < at)
            ++note;
        float target = -1;
        if (note < p.notes.size () && at >= p.notes[note].start && p.notes[note].edited)
            target = p.notes[note].target;
        engine.process (
          input.getArrayOfReadPointers (), scratch.getArrayOfWritePointers (), channels, count, settings, target);
        const int first = std::max (0, extra - start), last = std::min (count, total + extra - start);
        if (last > first)
            for (int ch = 0; ch < channels; ++ch)
                result.copyFrom (ch, start + first - extra, scratch, ch, first, last - first);
    }
    return result;
}
}
