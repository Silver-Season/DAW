// Phaser ("phaser"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class PhaserEffect final : public BasicEffect
{
public:
    PhaserEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "phaser"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makePhaser() { return std::make_unique<PhaserEffect>(); }
} // namespace daw::fx
