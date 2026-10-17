#include "Dialogs.h"

#include "../Strings.h"
#include "Theme.h"

namespace ui
{
void promptText (const juce::String& title, const juce::String& message, const juce::String& initial, std::function<void (const juce::String&)> done, bool allowEmpty)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initial);
    window->addButton (Strings::dialogOk, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (Strings::dialogCancel, 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safe (window);
    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([safe, allowEmpty, done = std::move (done)] (int result) {
                                 if (result == 1 && safe != nullptr)
                                 {
                                     const auto text = safe->getTextEditorContents ("text").trim();
                                     if ((allowEmpty || text.isNotEmpty()) && done)
                                         done (text);
                                 }
                             }),
                             true);
}

void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void (bool)> done,
              const juce::String& cancelText)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton (okText)
                                      .withButton (cancelText.isEmpty() ? Strings::dialogCancel : cancelText),
                                  [done = std::move (done)] (int result) {
                                      if (done)
                                          done (result == 1);
                                  });
}

void askThree (const juce::String& title, const juce::String& message, const juce::String& first, const juce::String& second,
               const juce::String& cancel, std::function<void (int)> done)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton (first)
                                      .withButton (second)
                                      .withButton (cancel),
                                  [done = std::move (done)] (int result) {
                                      if (done)
                                          done (result == 1 ? 0 : (result == 2 ? 1 : 2)); // JUCE: 1 first, 2 second, 0 last
                                  });
}

void showMessage (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}
} // namespace ui
