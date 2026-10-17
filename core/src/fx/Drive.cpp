// Drive ("drive"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class DriveEffect final : public BasicEffect
{
public:
    DriveEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "drive"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeDrive() { return std::make_unique<DriveEffect>(); }
} // namespace daw::fx
