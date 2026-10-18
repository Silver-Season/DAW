#include "daw/drums/DrumKit.h"

#include "daw/dsp/Filter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace daw::drums
{

// ---------------------------------------------------------------------------------------------------------------
// Pad map and parameter descriptors
// ---------------------------------------------------------------------------------------------------------------

const std::array<PadSlot, kNumPads>& padMap() noexcept
{
    static const std::array<PadSlot, kNumPads> map { {
        { 36, "C1", "kick" },
        { 37, "C#1", "rim" },
        { 38, "D1", "snare" },
        { 39, "D#1", "clap" },
        { 40, "E1", "snare2" },
        { 41, "F1", "tomLow" },
        { 42, "F#1", "closedHat" },
        { 43, "G1", "tomMid" },
        { 44, "G#1", "shaker" },
        { 45, "A1", "tomHigh" },
        { 46, "A#1", "openHat" },
        { 47, "B1", "cowbell" },
        { 48, "C2", "kick2" },
        { 49, "C#2", "crash" },
        { 50, "D2", "perc" },
        { 51, "D#2", "ride" },
    } };
    return map;
}

namespace
{
const std::array<PadParamInfo, kNumPadParams> paramInfos { {
    { "tune", -24.0f, 24.0f, 0.0f, false },
    { "fine", -100.0f, 100.0f, 0.0f, false },
    { "decay", 0.01f, 10.0f, 10.0f, false },
    { "attack", 0.0f, 100.0f, 0.0f, false },
    { "volumeDb", -60.0f, 12.0f, 0.0f, false },
    { "pan", -1.0f, 1.0f, 0.0f, false },
    { "cutoff", 20.0f, 20000.0f, 20000.0f, false },
    { "resonance", 0.0f, 1.0f, 0.0f, false },
    { "drive", 0.0f, 24.0f, 0.0f, false },
    { "startOffset", 0.0f, 1.0f, 0.0f, false },
    { "reverse", 0.0f, 1.0f, 0.0f, true },
    { "velocitySens", 0.0f, 1.0f, 1.0f, false },
    { "chokeGroup", 0.0f, (float) kMaxChokeGroups, 0.0f, true },
    { "mute", 0.0f, 1.0f, 0.0f, true },
    { "solo", 0.0f, 1.0f, 0.0f, true },
    { "oneShot", 0.0f, 1.0f, 1.0f, true },
    { "polyphony", 1.0f, (float) kMaxPadPolyphony, 4.0f, true },
} };

// Decay at or above this is "play the whole sample": no amp release at all.
constexpr float kDecayInfinite = 9.999f;
// Longest one-shot release ramp; a gate note-off never takes longer than kGateReleaseMax.
constexpr double kReleaseMax = 2.0, kReleaseMin = 0.005, kGateReleaseMax = 0.3;
// Starting inside the waveform (start offset, reverse) would click, so those starts ramp in over at least 1 ms.
constexpr double kDeclickSeconds = 0.001;
constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

// Cheap tanh-like curve (Padé), exact at +-3 where it meets +-1. std::tanh per sample per voice costs a few
// percent of a core with 32 voices, this costs almost nothing.
inline float softClip (float x) noexcept
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// 4-point cubic Hermite (Catmull-Rom). Frames outside the sample read as silence, so the first and the last frame
// glide in from and out to zero instead of being held.
inline float hermite (const float* d, int64_t len, int64_t i, float f) noexcept
{
    float xm1, x0, x1, x2;
    if (i >= 1 && i + 2 < len)
    {
        xm1 = d[i - 1];
        x0 = d[i];
        x1 = d[i + 1];
        x2 = d[i + 2];
    }
    else
    {
        auto at = [d, len] (int64_t k) { return k >= 0 && k < len ? d[k] : 0.0f; };
        xm1 = at (i - 1);
        x0 = at (i);
        x1 = at (i + 1);
        x2 = at (i + 2);
    }
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

} // namespace

const PadParamInfo& padParamInfo (PadParam id) noexcept
{
    const int i = std::clamp ((int) id, 0, kNumPadParams - 1);
    return paramInfos[(size_t) i];
}

float getParam (const PadParams& p, PadParam id) noexcept
{
    switch (id)
    {
        case PadParam::tune: return p.tune;
        case PadParam::fine: return p.fine;
        case PadParam::decay: return p.decay;
        case PadParam::attack: return p.attack;
        case PadParam::volumeDb: return p.volumeDb;
        case PadParam::pan: return p.pan;
        case PadParam::cutoff: return p.cutoff;
        case PadParam::resonance: return p.resonance;
        case PadParam::drive: return p.drive;
        case PadParam::startOffset: return p.startOffset;
        case PadParam::reverse: return p.reverse ? 1.0f : 0.0f;
        case PadParam::velocitySens: return p.velocitySens;
        case PadParam::chokeGroup: return (float) p.chokeGroup;
        case PadParam::mute: return p.mute ? 1.0f : 0.0f;
        case PadParam::solo: return p.solo ? 1.0f : 0.0f;
        case PadParam::oneShot: return p.oneShot ? 1.0f : 0.0f;
        case PadParam::polyphony: return (float) p.polyphony;
        case PadParam::count: break;
    }
    return 0.0f;
}

void setParam (PadParams& p, PadParam id, float value) noexcept
{
    const auto& info = padParamInfo (id);
    if (! std::isfinite (value))
        value = info.defaultValue;
    value = std::clamp (value, info.minValue, info.maxValue);
    if (info.discrete)
        value = std::round (value);

    switch (id)
    {
        case PadParam::tune: p.tune = value; break;
        case PadParam::fine: p.fine = value; break;
        case PadParam::decay: p.decay = value; break;
        case PadParam::attack: p.attack = value; break;
        case PadParam::volumeDb: p.volumeDb = value; break;
        case PadParam::pan: p.pan = value; break;
        case PadParam::cutoff: p.cutoff = value; break;
        case PadParam::resonance: p.resonance = value; break;
        case PadParam::drive: p.drive = value; break;
        case PadParam::startOffset: p.startOffset = value; break;
        case PadParam::reverse: p.reverse = value > 0.5f; break;
        case PadParam::velocitySens: p.velocitySens = value; break;
        case PadParam::chokeGroup: p.chokeGroup = (int) value; break;
        case PadParam::mute: p.mute = value > 0.5f; break;
        case PadParam::solo: p.solo = value > 0.5f; break;
        case PadParam::oneShot: p.oneShot = value > 0.5f; break;
        case PadParam::polyphony: p.polyphony = (int) value; break;
        case PadParam::count: break;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Kit text format
// ---------------------------------------------------------------------------------------------------------------

namespace
{
constexpr const char* kFormatId = "daw.drumkit";
constexpr int kFormatVersion = 1;

std::string escapeValue (const std::string& s)
{
    std::string out;
    out.reserve (s.size());
    for (char c : s)
    {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out;
}

std::string unescapeValue (const std::string& s)
{
    std::string out;
    out.reserve (s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const char c = s[++i];
            out += c == 'n' ? '\n' : (c == 'r' ? '\r' : c);
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

bool parseNumber (const std::string& s, float& out)
{
    std::istringstream in (s);
    in.imbue (std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail())
        return false;
    out = (float) v;
    return true;
}
} // namespace

std::string KitState::toText() const
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o << std::setprecision (9);
    o << "format=" << kFormatId << "\n";
    o << "version=" << kFormatVersion << "\n";
    o << "name=" << escapeValue (name) << "\n";

    for (int i = 0; i < kNumPads; ++i)
    {
        const auto& pad = pads[(size_t) i];
        const std::string prefix = "pad" + std::to_string (noteForPad (i)) + ".";
        o << prefix << "name=" << escapeValue (pad.name) << "\n";
        o << prefix << "source=" << escapeValue (pad.source) << "\n";
        for (int k = 0; k < kNumPadParams; ++k)
        {
            const auto id = (PadParam) k;
            const auto& info = padParamInfo (id);
            const float v = getParam (pad.params, id);
            o << prefix << info.key << "=";
            if (info.discrete)
                o << (int) v;
            else
                o << v;
            o << "\n";
        }
    }
    return o.str();
}

bool KitState::fromText (const std::string& text, KitState& out)
{
    KitState kit;
    bool formatOk = false;

    size_t lineStart = 0;
    while (lineStart <= text.size())
    {
        size_t lineEnd = text.find ('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        std::string line = text.substr (lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find ('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos)
            continue;

        const std::string key = line.substr (0, eq);
        const std::string value = line.substr (eq + 1);

        if (key == "format")
        {
            formatOk = (value == kFormatId);
            continue;
        }
        if (key == "name")
        {
            kit.name = unescapeValue (value);
            continue;
        }
        if (key.rfind ("pad", 0) != 0)
            continue;

        const size_t dot = key.find ('.');
        if (dot == std::string::npos || dot <= 3)
            continue;
        const int note = std::atoi (key.substr (3, dot - 3).c_str());
        const int pad = padForNote (note);
        if (pad < 0)
            continue;
        const std::string field = key.substr (dot + 1);
        auto& ps = kit.pads[(size_t) pad];

        if (field == "name")
        {
            ps.name = unescapeValue (value);
            continue;
        }
        if (field == "source")
        {
            ps.source = unescapeValue (value);
            continue;
        }
        for (int k = 0; k < kNumPadParams; ++k)
        {
            const auto id = (PadParam) k;
            if (field == padParamInfo (id).key)
            {
                float v = 0.0f;
                if (parseNumber (value, v))
                    setParam (ps.params, id, v);
                break;
            }
        }
    }

    if (! formatOk)
        return false;
    out = std::move (kit);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------------------------------------------

struct DrumKitEngine::Voice
{
    bool active = false;
    bool fading = false;   // stolen or choked: fadeGain ramps to 0, then the voice ends
    bool released = false; // gate mode note-off has started the release
    bool gate = false;
    bool stereo = false;
    bool filterOn = false; // latched once the pad's cutoff left 20 kHz, so sweeping back never jumps

    const Sample* sample = nullptr;
    const float* left = nullptr;
    const float* right = nullptr;
    int64_t length = 0;

    double pos = 0.0, step = 1.0; // step < 0 plays backwards
    int pad = -1;
    int chokeGroup = 0;
    uint64_t age = 0;

    int64_t t = 0; // samples since the start
    int64_t attackLen = 1;
    int64_t relStart = kNever;
    float relLen = 1.0f, relFrom = 1.0f, gateRelLen = 1.0f;

    float fadeGain = 1.0f, fadeStep = 0.0f;
    float velGain = 1.0f;
    float gainL = 1.0f, gainR = 1.0f, incL = 0.0f, incR = 0.0f;

    dsp::SVF filterL, filterR;

    // Renders [start, end) of the block into the outputs, adding to them.
    void render (const PadMix& m, float* outL, float* outR, int start, int end) noexcept
    {
        const bool drive = m.driveGain > 1.0001f;
        for (int i = start; i < end; ++i)
        {
            if (pos >= (double) length || pos <= -1.0)
            {
                active = false;
                return;
            }

            float g = velGain;
            if (t < attackLen)
                g *= (float) (t + 1) / (float) attackLen;
            if (t >= relStart)
            {
                const float x = (float) (t - relStart) / relLen;
                if (x >= 1.0f)
                {
                    active = false;
                    return;
                }
                const float r = 1.0f - x;
                g *= relFrom * r * r;
            }
            if (fading)
            {
                fadeGain -= fadeStep;
                if (fadeGain <= 0.0f)
                {
                    active = false;
                    return;
                }
                g *= fadeGain;
            }

            const double fl = std::floor (pos);
            const auto idx = (int64_t) fl;
            const auto frac = (float) (pos - fl);
            float l = hermite (left, length, idx, frac) * g;

            gainL += incL;
            gainR += incR;

            if (stereo)
            {
                float r = hermite (right, length, idx, frac) * g;
                if (drive)
                {
                    l = softClip (l * m.driveGain) * m.driveNorm;
                    r = softClip (r * m.driveGain) * m.driveNorm;
                }
                if (filterOn)
                {
                    l = filterL.tick (l).low;
                    r = filterR.tick (r).low;
                }
                outL[i] += l * gainL;
                outR[i] += r * gainR;
            }
            else
            {
                if (drive)
                    l = softClip (l * m.driveGain) * m.driveNorm;
                if (filterOn)
                    l = filterL.tick (l).low;
                outL[i] += l * gainL;
                outR[i] += l * gainR;
            }

            pos += step;
            ++t;
        }
    }

    float releaseLevelNow() const noexcept
    {
        if (t < relStart)
            return relFrom;
        const float x = std::min (1.0f, (float) (t - relStart) / relLen);
        return relFrom * (1.0f - x) * (1.0f - x);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------------------------------------------

DrumKitEngine::DrumKitEngine()
{
    for (auto& p : pads)
        for (int k = 0; k < kNumPadParams; ++k)
            p.params[(size_t) k].store (padParamInfo ((PadParam) k).defaultValue, std::memory_order_relaxed);
}

DrumKitEngine::~DrumKitEngine() = default;

void DrumKitEngine::prepare (double newSampleRate, int maxBlockSize, int maxVoices)
{
    (void) maxBlockSize; // voices render straight into the output, no scratch buffers are needed
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxSounding = std::max (1, maxVoices);
    // Every sounding voice can be stolen at once and still fade out in a slot of its own.
    numSlots = maxSounding * 2;
    voices = std::make_unique<Voice[]> ((size_t) numSlots);
    slotInUse = std::make_unique<std::atomic<const Sample*>[]> ((size_t) numSlots);
    for (int i = 0; i < numSlots; ++i)
        slotInUse[(size_t) i].store (nullptr, std::memory_order_relaxed);
    numPending = 0;
    numBlockEvents = 0;
    ageCounter = 0;
    activeVoices.store (0);
    for (auto& p : pads)
        p.activity.store (0.0f, std::memory_order_relaxed);
    // The audio is stopped while preparing and no voice survives it, so nothing retired is in use.
    retired.clear();
}

void DrumKitEngine::setPadSample (int pad, SamplePtr sample)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    // seq_cst on the store here and on the cycle counter below is what the reclamation argument relies on: any
    // block whose cycle increment comes after our read of the counter loads the new pointer.
    pads[(size_t) pad].sample.store (sample.get(), std::memory_order_seq_cst);
    if (owned[(size_t) pad] != nullptr)
        retired.push_back ({ std::move (owned[(size_t) pad]), cycle.load (std::memory_order_seq_cst) });
    owned[(size_t) pad] = std::move (sample);
    collectGarbage();
}

SamplePtr DrumKitEngine::getPadSample (int pad) const
{
    return pad >= 0 && pad < kNumPads ? owned[(size_t) pad] : nullptr;
}

void DrumKitEngine::collectGarbage()
{
    if (retired.empty())
        return;
    const uint64_t now = cycle.load (std::memory_order_seq_cst);
    std::erase_if (retired, [this, now] (const Retired& r) {
        if (numSlots == 0)
            return true; // never prepared: the audio thread cannot have seen it
        if (now <= r.cycle)
            return false;
        for (int i = 0; i < numSlots; ++i)
            if (slotInUse[(size_t) i].load (std::memory_order_acquire) == r.sample.get())
                return false;
        return true;
    });
}

void DrumKitEngine::setPadParam (int pad, PadParam id, float value) noexcept
{
    if (pad < 0 || pad >= kNumPads || id == PadParam::count)
        return;
    PadParams tmp;
    setParam (tmp, id, value); // clamps and rounds
    pads[(size_t) pad].params[(size_t) id].store (getParam (tmp, id), std::memory_order_relaxed);
}

void DrumKitEngine::setPadParams (int pad, const PadParams& p) noexcept
{
    for (int k = 0; k < kNumPadParams; ++k)
        setPadParam (pad, (PadParam) k, getParam (p, (PadParam) k));
}

PadParams DrumKitEngine::getPadParams (int pad) const noexcept
{
    PadParams p;
    if (pad < 0 || pad >= kNumPads)
        return p;
    for (int k = 0; k < kNumPadParams; ++k)
        setParam (p, (PadParam) k, pads[(size_t) pad].params[(size_t) k].load (std::memory_order_relaxed));
    return p;
}

float DrumKitEngine::padActivity (int pad) const noexcept
{
    return pad >= 0 && pad < kNumPads ? pads[(size_t) pad].activity.load (std::memory_order_relaxed) : 0.0f;
}

void DrumKitEngine::queue (const Event& e) noexcept
{
    if (numPending < kMaxQueuedEvents)
        pending[(size_t) numPending++] = e;
}

void DrumKitEngine::noteOn (int note, float velocity, int sampleOffset) noexcept
{
    if (velocity <= 0.0f)
        noteOff (note, sampleOffset);
    else
        queue ({ sampleOffset, note, std::min (velocity, 1.0f), EventKind::on });
}

void DrumKitEngine::noteOff (int note, int sampleOffset) noexcept
{
    queue ({ sampleOffset, note, 0.0f, EventKind::off });
}

void DrumKitEngine::allNotesOff() noexcept
{
    queue ({ 0, 0, 0.0f, EventKind::allOff });
}

void DrumKitEngine::reset() noexcept
{
    for (int i = 0; i < numSlots; ++i)
        voices[(size_t) i].active = false;
    numPending = 0;
    publishVoices();
}

void DrumKitEngine::addEvent (const Event& e) noexcept
{
    if (numBlockEvents < kMaxEventsPerBlock)
        blockEvents[(size_t) numBlockEvents++] = e;
}

void DrumKitEngine::process (float* outL, float* outR, int numSamples, const MidiEvent* events, int numEvents) noexcept
{
    numBlockEvents = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& m = events[i];
        const int type = m.status & 0xF0;
        if (type == 0x90 && m.data2 > 0)
            addEvent ({ m.sampleOffset, m.data1, (float) m.data2 / 127.0f, EventKind::on });
        else if (type == 0x80 || type == 0x90)
            addEvent ({ m.sampleOffset, m.data1, 0.0f, EventKind::off });
        else if (type == 0xB0 && (m.data1 == 120 || m.data1 == 123))
            addEvent ({ m.sampleOffset, 0, 0.0f, EventKind::allOff });
    }
    renderBlock (outL, outR, numSamples);
}

void DrumKitEngine::process (float* outL, float* outR, int numSamples, const DrumEvent* events, int numEvents) noexcept
{
    numBlockEvents = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[i];
        if (e.velocity > 0.0f)
            addEvent ({ e.offset, e.note, std::min (e.velocity, 1.0f), EventKind::on });
        else
            addEvent ({ e.offset, e.note, 0.0f, EventKind::off });
    }
    renderBlock (outL, outR, numSamples);
}

void DrumKitEngine::computeMix() noexcept
{
    bool anySolo = false;
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& bp = blockParams[(size_t) p];
        for (int k = 0; k < kNumPadParams; ++k)
            setParam (bp, (PadParam) k, pads[(size_t) p].params[(size_t) k].load (std::memory_order_relaxed));
        anySolo = anySolo || bp.solo;
    }

    for (int p = 0; p < kNumPads; ++p)
    {
        const auto& bp = blockParams[(size_t) p];
        auto& m = mix[(size_t) p];
        const bool silent = bp.mute || (anySolo && ! bp.solo) || bp.volumeDb <= -59.99f;
        const float vol = silent ? 0.0f : dbToGain (bp.volumeDb);
        // Balance law: the centre is unity, so a pad at pan 0 plays its sample exactly as recorded.
        m.gainL = vol * std::min (1.0f, 1.0f - bp.pan);
        m.gainR = vol * std::min (1.0f, 1.0f + bp.pan);
        m.cutoff = bp.cutoff;
        m.resonance = bp.resonance;
        m.driveGain = dbToGain (bp.drive);
        m.driveNorm = m.driveGain > 1.0001f ? 1.0f / softClip (m.driveGain) : 1.0f;
    }
}

void DrumKitEngine::beginFade (Voice& v, float ms) noexcept
{
    const float step = 1.0f / std::max (1.0f, ms * 0.001f * (float) sampleRate);
    if (! v.fading)
    {
        v.fading = true;
        v.fadeGain = 1.0f;
        v.fadeStep = step;
    }
    else
    {
        v.fadeStep = std::max (v.fadeStep, step);
    }
}

DrumKitEngine::Voice* DrumKitEngine::takeSlot() noexcept
{
    Voice* quietest = nullptr;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active)
            return &v;
        if (v.fading && (quietest == nullptr || v.fadeGain < quietest->fadeGain))
            quietest = &v;
    }
    // Only reachable when a burst steals more voices than there are spare slots within one fade time; the voice
    // closest to silence is cut.
    return quietest;
}

void DrumKitEngine::startNote (int pad, float velocity) noexcept
{
    auto& ps = pads[(size_t) pad];
    ps.activity.store (std::max (ps.activity.load (std::memory_order_relaxed), velocity), std::memory_order_relaxed);

    const Sample* s = ps.sample.load (std::memory_order_seq_cst);
    if (s == nullptr || s->left.empty() || s->rate <= 0.0)
        return;

    const auto& p = blockParams[(size_t) pad];

    if (p.chokeGroup > 0)
        for (int i = 0; i < numSlots; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.active && v.pad != pad && v.chokeGroup == p.chokeGroup)
                beginFade (v, kChokeFadeMs);
        }

    // Per-pad polyphony, then the global limit; the oldest sounding voice is the one to go.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool perPad = (pass == 0);
        const int limit = perPad ? p.polyphony : maxSounding;
        for (;;)
        {
            int count = 0;
            Voice* oldest = nullptr;
            for (int i = 0; i < numSlots; ++i)
            {
                auto& v = voices[(size_t) i];
                if (! v.active || v.fading || (perPad && v.pad != pad))
                    continue;
                ++count;
                if (oldest == nullptr || v.age < oldest->age)
                    oldest = &v;
            }
            if (count < limit || oldest == nullptr)
                break;
            beginFade (*oldest, kStealFadeMs);
        }
    }

    Voice* vp = takeSlot();
    if (vp == nullptr)
        return;
    auto& v = *vp;

    v = Voice {};
    v.active = true;
    v.sample = s;
    v.left = s->left.data();
    v.length = s->numFrames();
    v.stereo = s->channels >= 2 && (int64_t) s->right.size() == v.length;
    v.right = v.stereo ? s->right.data() : nullptr;
    v.pad = pad;
    v.chokeGroup = p.chokeGroup;
    v.gate = ! p.oneShot;
    v.age = ++ageCounter;

    const double semis = (double) p.tune + (double) p.fine / 100.0;
    const double speed = s->rate / sampleRate * std::exp2 (semis / 12.0);
    const double skip = (double) p.startOffset * (double) (v.length - 1);
    v.pos = p.reverse ? (double) (v.length - 1) - skip : skip;
    v.step = p.reverse ? -speed : speed;

    double attackSec = (double) p.attack * 0.001;
    if (p.startOffset > 0.0f || p.reverse)
        attackSec = std::max (attackSec, kDeclickSeconds);
    v.attackLen = std::max<int64_t> (1, (int64_t) std::llround (attackSec * sampleRate));

    const double relSec = std::clamp (0.5 * (double) p.decay, kReleaseMin, kReleaseMax);
    if (p.decay < kDecayInfinite)
    {
        v.relLen = (float) std::max (1.0, relSec * sampleRate);
        v.relStart = std::max<int64_t> (0, (int64_t) std::llround ((double) p.decay * sampleRate - (double) v.relLen));
    }
    v.gateRelLen = (float) std::max (1.0, std::min (relSec, kGateReleaseMax) * sampleRate);

    v.velGain = 1.0f + p.velocitySens * (velocity - 1.0f);

    const auto& m = mix[(size_t) pad];
    v.gainL = m.gainL;
    v.gainR = m.gainR;
    v.filterOn = m.cutoff < 19999.0f;
    v.filterL.setCoefficients (m.cutoff, m.resonance, (float) sampleRate);
    v.filterR.setCoefficients (m.cutoff, m.resonance, (float) sampleRate);
}

void DrumKitEngine::releaseNote (int pad) noexcept
{
    for (int i = 0; i < numSlots; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active || v.fading || v.released || ! v.gate || v.pad != pad)
            continue;
        v.released = true;
        const float level = v.releaseLevelNow();
        v.relFrom = level;
        v.relStart = v.t;
        v.relLen = v.gateRelLen;
    }
}

void DrumKitEngine::handle (const Event& e) noexcept
{
    switch (e.kind)
    {
        case EventKind::on:
        {
            const int pad = padForNote (e.note);
            if (pad >= 0)
                startNote (pad, e.velocity);
            break;
        }
        case EventKind::off:
        {
            const int pad = padForNote (e.note);
            if (pad >= 0)
                releaseNote (pad);
            break;
        }
        case EventKind::allOff:
            for (int i = 0; i < numSlots; ++i)
                if (voices[(size_t) i].active)
                    beginFade (voices[(size_t) i], kStealFadeMs);
            break;
    }
}

void DrumKitEngine::renderBlock (float* outL, float* outR, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    std::fill (outL, outL + numSamples, 0.0f);
    std::fill (outR, outR + numSamples, 0.0f);
    if (numSlots == 0)
    {
        numPending = 0;
        return;
    }

    // Queued noteOn()/noteOff() calls go first, so at equal offsets they keep the order they were made in.
    {
        const int external = numBlockEvents;
        const int total = std::min (kMaxEventsPerBlock, external + numPending);
        const int fromQueue = total - external;
        for (int i = external - 1; i >= 0; --i)
            if (i + fromQueue < kMaxEventsPerBlock)
                blockEvents[(size_t) (i + fromQueue)] = blockEvents[(size_t) i];
        for (int i = 0; i < fromQueue; ++i)
            blockEvents[(size_t) i] = pending[(size_t) i];
        numBlockEvents = total;
        numPending = 0;
    }
    for (int i = 0; i < numBlockEvents; ++i)
        blockEvents[(size_t) i].offset = std::clamp (blockEvents[(size_t) i].offset, 0, numSamples - 1);
    // Stable insertion sort: a block holds a handful of events, mostly already in order.
    for (int i = 1; i < numBlockEvents; ++i)
    {
        const Event e = blockEvents[(size_t) i];
        int j = i - 1;
        while (j >= 0 && blockEvents[(size_t) j].offset > e.offset)
        {
            blockEvents[(size_t) (j + 1)] = blockEvents[(size_t) j];
            --j;
        }
        blockEvents[(size_t) (j + 1)] = e;
    }

    computeMix();

    // Gains glide to this block's pad mix over the block; the filter follows the pad's cutoff block by block.
    const float invN = 1.0f / (float) numSamples;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active)
            continue;
        const auto& m = mix[(size_t) v.pad];
        v.incL = (m.gainL - v.gainL) * invN;
        v.incR = (m.gainR - v.gainR) * invN;
        if (m.cutoff < 19999.0f)
            v.filterOn = true;
        if (v.filterOn)
        {
            v.filterL.setCoefficients (m.cutoff, m.resonance, (float) sampleRate);
            v.filterR.setCoefficients (m.cutoff, m.resonance, (float) sampleRate);
        }
    }

    int pos = 0, ev = 0;
    while (pos < numSamples)
    {
        while (ev < numBlockEvents && blockEvents[(size_t) ev].offset <= pos)
            handle (blockEvents[(size_t) ev++]);
        const int end = ev < numBlockEvents ? blockEvents[(size_t) ev].offset : numSamples;
        for (int i = 0; i < numSlots; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.active)
                v.render (mix[(size_t) v.pad], outL, outR, pos, end);
        }
        pos = end;
    }
    numBlockEvents = 0;

    for (int i = 0; i < numSlots; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active)
            continue;
        // Snap away rounding drift of the ramp; voices started inside this block are already on target.
        v.gainL = mix[(size_t) v.pad].gainL;
        v.gainR = mix[(size_t) v.pad].gainR;
        v.incL = v.incR = 0.0f;
    }

    const float decay = std::exp (-(float) numSamples / (kActivityDecaySeconds * (float) sampleRate));
    for (auto& p : pads)
    {
        const float a = p.activity.load (std::memory_order_relaxed) * decay;
        p.activity.store (a < 1.0e-3f ? 0.0f : a, std::memory_order_relaxed);
    }

    publishVoices();
}

void DrumKitEngine::publishVoices() noexcept
{
    int count = 0;
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (v.active)
            ++count;
        slotInUse[(size_t) i].store (v.active ? v.sample : nullptr, std::memory_order_release);
    }
    activeVoices.store (count, std::memory_order_relaxed);
    cycle.fetch_add (1, std::memory_order_seq_cst);
}

} // namespace daw::drums
