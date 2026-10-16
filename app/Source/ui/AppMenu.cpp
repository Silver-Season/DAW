#include "AppMenu.h"

#include "../Strings.h"

namespace ui
{
AppMenuModel::AppMenuModel (juce::ApplicationCommandManager& m) : manager (m) {}

juce::StringArray AppMenuModel::getMenuBarNames()
{
    return { Strings::menu::file, Strings::menu::edit, Strings::menu::create, Strings::menu::view };
}

juce::PopupMenu AppMenuModel::getMenuForIndex (int index, const juce::String&)
{
    namespace M = Strings::menu;
    juce::PopupMenu menu;
    auto add = [&] (juce::CommandID id) { menu.addCommandItem (&manager, id); };

    switch (index)
    {
        case 0:
        {
            add (commands::newProject);
            add (commands::openProject);
            juce::PopupMenu recent;
            const auto list = recentProjects ? recentProjects() : juce::StringArray();
            if (list.isEmpty())
                recent.addItem (Strings::utf8 ("(пусто)"), false, false, nullptr);
            menu.addSubMenu (M::openRecent, recent, false); // enabled with the project files (task 1.7)
            menu.addSeparator();
            add (commands::saveProject);
            add (commands::saveProjectAs);
            add (commands::saveTemplate);
            add (commands::versionHistory);
            menu.addSeparator();
            add (commands::exportAudio);
            add (commands::showInFinder);
            menu.addSeparator();
            add (commands::scanPlugins);
#if ! JUCE_MAC
            menu.addSeparator();
            add (commands::quitApp);
#endif
            break;
        }
        case 1:
            add (commands::undo);
            add (commands::redo);
            menu.addSeparator();
            add (commands::cut);
            add (commands::copy);
            add (commands::paste);
            menu.addSeparator();
            add (commands::duplicate);
            add (commands::deleteItem);
            add (commands::renameItem);
            break;
        case 2:
            add (commands::newTrack);
            add (commands::newScene);
            add (commands::captureScene);
            add (commands::newClip);
            break;
        case 3:
        {
            add (commands::toggleBrowser);
            add (commands::toggleDetail);
            add (commands::toggleMixer);
            add (commands::switchView);
            menu.addSeparator();
            add (commands::zoomIn);
            add (commands::zoomOut);
            if (themeIds && themeNames)
            {
                menu.addSeparator();
                juce::PopupMenu themes;
                const auto idList = themeIds(), nameList = themeNames();
                const auto current = currentThemeId ? currentThemeId() : juce::String();
                for (int i = 0; i < idList.size(); ++i)
                {
                    const auto id = idList[i];
                    themes.addItem (nameList[i], true, id == current, [this, id] {
                        if (onThemeChosen)
                            onThemeChosen (id);
                    });
                }
                menu.addSubMenu (Strings::utf8 ("Тема"), themes);
            }
#if ! JUCE_MAC
            menu.addSeparator();
            add (commands::openSettings);
#endif
            break;
        }
        default: break;
    }
    return menu;
}

juce::PopupMenu AppMenuModel::getMacExtraMenu()
{
    juce::PopupMenu menu;
    menu.addCommandItem (&manager, commands::openSettings);
    return menu;
}
} // namespace ui
