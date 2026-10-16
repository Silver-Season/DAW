#include "PrismaProcessor.h"

// Entry point used by the AU / VST3 / Standalone wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismaProcessor();
}
