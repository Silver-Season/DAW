// Delay ("delay"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class DelayEffect final : public BasicEffect
{
public:
    DelayEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "delay"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeDelay() { return std::make_unique<DelayEffect>(); }
} // namespace daw::fx
