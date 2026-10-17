// Gate ("gate"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class GateEffect final : public BasicEffect
{
public:
    GateEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "gate"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeGate() { return std::make_unique<GateEffect>(); }
} // namespace daw::fx
