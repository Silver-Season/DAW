// Limiter ("limiter"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class LimiterEffect final : public BasicEffect
{
public:
    LimiterEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "limiter"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeLimiter() { return std::make_unique<LimiterEffect>(); }
} // namespace daw::fx
