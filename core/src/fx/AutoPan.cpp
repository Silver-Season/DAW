// Auto Pan ("autopan"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class AutoPanEffect final : public BasicEffect
{
public:
    AutoPanEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "autopan"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeAutoPan() { return std::make_unique<AutoPanEffect>(); }
} // namespace daw::fx
