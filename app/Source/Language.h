#pragma once

#include <juce_core/juce_core.h>

// The language of the user interface. It is read once, at the first call (the strings of Strings.h are made before
// main), and stays for the whole run: a new choice in Settings applies after a restart. English by default.
// The DAW_LANG environment variable ("en", "ru") overrides the saved choice (screenshots, tests).
namespace Strings
{
enum class Language
{
    english,
    russian
};

Language language();
inline bool isRussian() { return language() == Language::russian; }

const char* languageCode (Language); // "en", "ru"
Language languageFromCode (const juce::String&); // anything unknown is English
} // namespace Strings
