#pragma once

#include "UiCommon.h"

class ProjectModel;

namespace ui
{
// Placeholder of the Arrangement tab until stage 3: an empty view with a ruler of bars and a line of text
// (docs/spec/02-midi-recording.md, task 2.8). Better than an inactive tab: Tab already switches to it.
class ArrangementView final : public juce::Component
{
public:
    static constexpr int kRulerHeight = 28;

    explicit ArrangementView (ProjectModel&);

    void paint (juce::Graphics&) override;

private:
    ProjectModel& model;
};
} // namespace ui
