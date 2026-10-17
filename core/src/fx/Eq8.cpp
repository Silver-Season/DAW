// EQ Eight ("eq8"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class Eq8Effect final : public BasicEffect
{
public:
    Eq8Effect() : BasicEffect ({}) {}
    std::string_view type() const override { return "eq8"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeEq8() { return std::make_unique<Eq8Effect>(); }
} // namespace daw::fx
