// Spec test 1 (docs/spec/06-effects-mixing.md) for every built-in effect, with its default parameters.
#include "FxTest.h"

using namespace fxtest;

FX_TEST ("every effect: Mix 0 is the input delayed by the latency (null test below -120 dB)")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        check (fx != nullptr, "effect " + t.id + " can be created");
        if (fx == nullptr)
            continue;
        const int mix = fx->paramIndex ("mix");
        if (mix < 0)
            continue; // an effect without Mix (none yet) has nothing to null
        fx->setParam (mix, 0.0f);
        fx->reset();
        const auto in = noise (-6.0, 48000, 7);
        const auto out = run (*fx, in);
        const int latency = fx->latencySamples();
        double err = 0.0;
        for (size_t i = (size_t) latency + 4800; i < in.size(); ++i)
            err = std::max ({ err, (double) std::abs (out.l[i] - in.l[i - (size_t) latency]), (double) std::abs (out.r[i] - in.r[i - (size_t) latency]) });
        check (linToDb (err) < -120.0, t.id + ": null test " + std::to_string (linToDb (err)) + " dB");
    }
}

FX_TEST ("every effect: silence stays silent after the tail, noise gives finite output within +-4, reset clears the tail")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        if (fx == nullptr)
            continue;
        const auto loud = run (*fx, noise (-6.0, 96000, 3));
        check (finite (loud), t.id + ": no NaN or Inf on noise");
        check (peak (loud.l) <= 4.0 && peak (loud.r) <= 4.0, t.id + ": output within +-4 (peak " + std::to_string (peak (loud.l)) + ")");

        fx->reset();
        const auto quiet = run (*fx, Stereo (4800));
        check (peak (quiet.l) < 1.0e-6 && peak (quiet.r) < 1.0e-6, t.id + ": reset leaves no tail");

        run (*fx, noise (-6.0, 4800, 5));
        const size_t tail = (size_t) ((fx->tailSeconds() + 0.5) * kRate) + (size_t) fx->latencySamples();
        const auto after = run (*fx, Stereo (tail + 4800));
        check (peak (after.l, tail) < 1.0e-4 && peak (after.r, tail) < 1.0e-4,
               t.id + ": silent after the tail (" + std::to_string (linToDb (peak (after.l, tail))) + " dB)");
    }
}

FX_TEST ("every effect: an impulse through Mix 0 comes out exactly latencySamples() later")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        if (fx == nullptr || fx->paramIndex ("mix") < 0)
            continue;
        fx->setParam (fx->paramIndex ("mix"), 0.0f);
        fx->reset();
        const auto out = run (*fx, impulse (9600, 100));
        size_t at = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out.l[i]) > 0.5f)
            {
                at = i;
                break;
            }
        check ((int) at - 100 == fx->latencySamples(), t.id + ": measured latency " + std::to_string ((int) at - 100) + ", reported "
                                                           + std::to_string (fx->latencySamples()));
    }
}

FX_TEST ("parameters: normalised round trip, ids unique, defaults inside the range")
{
    for (const auto& t : daw::fx::effectTypes())
    {
        auto fx = make (t.id.c_str());
        if (fx == nullptr)
            continue;
        const auto& ps = fx->params();
        for (size_t i = 0; i < ps.size(); ++i)
        {
            const auto& p = ps[i];
            check (p.def >= p.min && p.def <= p.max, t.id + "." + p.id + ": default in range");
            for (size_t j = i + 1; j < ps.size(); ++j)
                check (ps[j].id != p.id, t.id + ": id " + p.id + " unique");
            const float back = daw::fx::fromNormalised (p, daw::fx::toNormalised (p, p.def));
            check (std::abs (back - p.def) <= 1.0e-3f * std::max (1.0f, std::abs (p.def)), t.id + "." + p.id + ": normalised round trip");
            check (std::abs (fx->getParam ((int) i) - p.def) < 1.0e-6f, t.id + "." + p.id + ": starts at its default");
        }
    }
}
