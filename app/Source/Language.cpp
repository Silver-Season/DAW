#include "Language.h"

#include "Settings.h"

#include <cstdlib>

namespace Strings
{
const char* languageCode (Language l) { return l == Language::russian ? "ru" : "en"; }

Language languageFromCode (const juce::String& code)
{
    return code.trim().equalsIgnoreCase ("ru") ? Language::russian : Language::english;
}

Language language()
{
    static const Language chosen = [] {
        if (const char* env = std::getenv ("DAW_LANG"); env != nullptr && env[0] != 0)
            return languageFromCode (env);
        return languageFromCode (Settings::languageFileFor (Settings::defaultFile()).loadFileAsString());
    }();
    return chosen;
}
} // namespace Strings
