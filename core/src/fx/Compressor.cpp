// Compressor ("comp"): STUB until its DSP lands (docs/spec/06-effects-mixing.md, task 6.2). Passes the sound through.
#include "daw/fx/BasicEffect.h"

namespace daw::fx
{
namespace
{
class CompressorEffect final : public BasicEffect
{
public:
    CompressorEffect() : BasicEffect ({}) {}
    std::string_view type() const override { return "comp"; }

protected:
    void processWet (float* const*, int, int, const float* const*) override {}
};
} // namespace

std::unique_ptr<Effect> makeCompressor() { return std::make_unique<CompressorEffect>(); }
} // namespace daw::fx
