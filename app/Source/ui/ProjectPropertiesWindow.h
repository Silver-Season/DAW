#pragma once

#include "Controls.h"

#include "../model/ProjectActions.h"

namespace ui
{
// "Project Properties" (Cmd+Alt+P): tempo, time signature, scale, the tuning of the built-in instruments, author
// and notes. Every change is one undo step of the project.
class ProjectPropertiesWindow final : public juce::DocumentWindow
{
public:
    ProjectPropertiesWindow (ProjectModel&, ProjectActions&, std::function<void()> onClosed);
    void closeButtonPressed() override;

private:
    class Content;
    std::function<void()> closed;
};
} // namespace ui
