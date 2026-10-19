#pragma once

#include <juce_core/juce_core.h>

#include <cstdio>

// MP3 export through LAME (docs/spec/04-export.md, task 4.3): stereo, CBR 128..320 kbps or VBR V0..V9, quality 2, an
// ID3v2 tag (title, artist, tempo) and the LAME/Xing frame with the exact length, so players count the time right and
// play gapless. Rates other than 32, 44.1 and 48 kHz are resampled by LAME to 48 kHz (44.1 kHz for 88.2 kHz).
// Not real-time: the export calls it from its background thread.
class Mp3Writer
{
public:
    struct Settings
    {
        int kbps = 320;  // CBR bit rate when vbrQuality < 0
        int vbrQuality = -1; // 0 (best) .. 9; -1 = CBR
        juce::String title, artist;
        double tempo = 0.0; // written as TBPM when > 0
    };

    Mp3Writer() = default;
    ~Mp3Writer();
    Mp3Writer (const Mp3Writer&) = delete;
    Mp3Writer& operator= (const Mp3Writer&) = delete;

    bool open (const juce::File&, double sampleRate, const Settings&, juce::String& error);
    bool write (const float* left, const float* right, int numFrames);
    // Flushes the encoder and writes the LAME tag. Returns false if anything failed on the way.
    bool close();

    static int outputRateFor (double sampleRate);

private:
    void* lame = nullptr; // lame_global_flags*
    std::FILE* file = nullptr;
    juce::HeapBlock<unsigned char> buffer;
    size_t bufferSize = 0;
    bool failed = false;
};
