// Filter ("filter"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class FilterEffect final : public BasicEffect
{
public:
    FilterEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "filter"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeFilter() { return std::make_unique<FilterEffect>(); }
} // namespace daw::fx
