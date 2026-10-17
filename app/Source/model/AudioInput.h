#pragma once

#include <juce_core/juce_core.h>

// The sound input of an audio track as TRACK.input stores it (docs/spec/05-audio-recording.md, task 5.3):
//   "audio:0-1"  channels 1 and 2 of the sound card (stereo; channels count from 0 in the file)
//   "audio:2"    channel 3 alone (mono)
//   "resample"   the master output
//   "none"
// A track that has never chosen (the MIDI default "all-midi", or nothing) listens to channels 1/2.
enum class AudioInputKind : int
{
    none = 0,
    device,   // channels of the sound card
    resample  // the master output (one block late)
};

struct AudioInputSpec
{
    AudioInputKind kind = AudioInputKind::device;
    int left = 0, right = 1; // device channels; right < 0 = mono

    bool isStereo() const { return right >= 0; }
    int numChannels() const { return kind == AudioInputKind::resample || isStereo() ? 2 : 1; }

    static AudioInputSpec parse (const juce::String& text)
    {
        AudioInputSpec s;
        if (text == "none")
            s.kind = AudioInputKind::none;
        else if (text == "resample")
            s.kind = AudioInputKind::resample;
        else if (text.startsWith ("audio:"))
        {
            const auto body = text.fromFirstOccurrenceOf ("audio:", false, false);
            s.left = juce::jmax (0, body.upToFirstOccurrenceOf ("-", false, false).getIntValue());
            s.right = body.containsChar ('-') ? juce::jmax (0, body.fromFirstOccurrenceOf ("-", false, false).getIntValue()) : -1;
        }
        return s;
    }

    juce::String toString() const
    {
        if (kind == AudioInputKind::none)
            return "none";
        if (kind == AudioInputKind::resample)
            return "resample";
        return "audio:" + juce::String (left) + (isStereo() ? "-" + juce::String (right) : juce::String());
    }
};

// TRACK.monitor: "auto" | "in" | "off".
enum class MonitorMode : int
{
    automatic = 0, // the input is heard while the track is armed and plays no clip (and always while it records)
    in,            // always heard
    off
};

inline MonitorMode monitorModeOf (const juce::String& text)
{
    return text == "in" ? MonitorMode::in : text == "off" ? MonitorMode::off : MonitorMode::automatic;
}
