#include "Mp3Writer.h"

#include <lame.h>

namespace
{
lame_global_flags* flags (void* p) { return static_cast<lame_global_flags*> (p); }
} // namespace

Mp3Writer::~Mp3Writer()
{
    if (lame != nullptr || file != nullptr)
        close();
}

int Mp3Writer::outputRateFor (double sampleRate)
{
    const int rate = juce::roundToInt (sampleRate);
    if (rate == 32000 || rate == 44100 || rate == 48000)
        return rate;
    return rate % 11025 == 0 ? 44100 : 48000;
}

bool Mp3Writer::open (const juce::File& target, double sampleRate, const Settings& s, juce::String& error)
{
    target.getParentDirectory().createDirectory();
    file = std::fopen (target.getFullPathName().toRawUTF8(), "w+b");
    if (file == nullptr)
    {
        error = "cannot write " + target.getFullPathName();
        return false;
    }
    auto* gf = lame_init();
    lame = gf;
    if (gf == nullptr)
    {
        error = "mp3";
        return false;
    }
    lame_set_in_samplerate (gf, juce::roundToInt (sampleRate));
    lame_set_out_samplerate (gf, outputRateFor (sampleRate));
    lame_set_num_channels (gf, 2);
    lame_set_mode (gf, JOINT_STEREO);
    lame_set_quality (gf, 2);
    if (s.vbrQuality >= 0)
    {
        lame_set_VBR (gf, vbr_default);
        lame_set_VBR_quality (gf, (float) juce::jlimit (0, 9, s.vbrQuality));
    }
    else
    {
        lame_set_VBR (gf, vbr_off);
        lame_set_brate (gf, juce::jlimit (32, 320, s.kbps));
    }
    lame_set_bWriteVbrTag (gf, 1); // the LAME/Xing frame: exact length and gapless playback
    id3tag_init (gf);
    id3tag_v2_only (gf);
    if (s.title.isNotEmpty())
        id3tag_set_title (gf, s.title.toRawUTF8());
    if (s.artist.isNotEmpty())
        id3tag_set_artist (gf, s.artist.toRawUTF8());
    if (s.tempo > 0.0)
        id3tag_set_textinfo_latin1 (gf, "TBPM", juce::String (juce::roundToInt (s.tempo)).toRawUTF8());
    if (lame_init_params (gf) < 0)
    {
        error = "mp3 settings";
        return false;
    }
    return true;
}

bool Mp3Writer::write (const float* left, const float* right, int numFrames)
{
    if (lame == nullptr || file == nullptr || failed)
        return false;
    // LAME's worst case: 1.25 x samples + 7200 bytes
    const size_t needed = (size_t) (1.25 * numFrames) + 7200;
    if (bufferSize < needed)
    {
        buffer.allocate (needed, false);
        bufferSize = needed;
    }
    const int bytes = lame_encode_buffer_ieee_float (flags (lame), left, right, numFrames, buffer.get(), (int) bufferSize);
    if (bytes < 0 || (bytes > 0 && std::fwrite (buffer.get(), 1, (size_t) bytes, file) != (size_t) bytes))
        failed = true;
    return ! failed;
}

bool Mp3Writer::close()
{
    if (lame != nullptr && file != nullptr && ! failed)
    {
        if (bufferSize < 7200)
        {
            buffer.allocate (7200, false);
            bufferSize = 7200;
        }
        const int bytes = lame_encode_flush (flags (lame), buffer.get(), (int) bufferSize);
        if (bytes < 0 || (bytes > 0 && std::fwrite (buffer.get(), 1, (size_t) bytes, file) != (size_t) bytes))
            failed = true;
        else
            lame_mp3_tags_fid (flags (lame), file);
    }
    if (lame != nullptr)
        lame_close (flags (lame));
    lame = nullptr;
    if (file != nullptr && std::fclose (file) != 0)
        failed = true;
    file = nullptr;
    return ! failed;
}
