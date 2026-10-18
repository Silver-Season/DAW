#include "FxTest.h"

int main()
{
    for (const auto& c : fxtest::registry())
    {
        std::printf ("[fx] %s\n", c.name);
        c.fn();
    }
    std::printf ("\n%d checks, %d failures\n", fxtest::checks, fxtest::failures);
    return fxtest::failures == 0 ? 0 : 1;
}
