// Bitcrusher ("crush"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class CrushEffect final : public BasicEffect
{
public:
    CrushEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "crush"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeCrush() { return std::make_unique<CrushEffect>(); }
} // namespace daw::fx
