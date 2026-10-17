// Reverb ("reverb"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class ReverbEffect final : public BasicEffect
{
public:
    ReverbEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "reverb"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeReverb() { return std::make_unique<ReverbEffect>(); }
} // namespace daw::fx
