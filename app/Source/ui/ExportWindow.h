#pragma once

#include "Controls.h"

#include "../engine/OfflineRenderer.h"
#include "../model/ProjectModel.h"

namespace ui
{
// "Export…" (Cmd+Shift+R, docs/spec/04-export.md, tasks 4.2–4.5): master, stems or both; range; WAV, AIFF, FLAC or MP3;
// bit depth or MP3 quality; peak or loudness (LUFS) normalisation; the tail. Then a progress bar and the result
// (length, peak, loudness, true peak, the stems, "Show in Finder").
class ExportWindow final : public juce::DocumentWindow
{
public:
    ExportWindow (Settings&, ProjectModel&, AudioEngine&, juce::File defaultFolder, juce::String defaultName, std::function<void()> closed);
    ~ExportWindow() override;
    void closeButtonPressed() override;

private:
    class Content;
    std::function<void()> onClosed;
};
} // namespace ui
