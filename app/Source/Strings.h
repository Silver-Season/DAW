#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Language.h"

#include <map>

// Every user-visible text lives here, in English and Russian: tr ("English", "Русский"). English is the default; the
// language is chosen in Settings → Appearance and applies after a restart (Language.h). A text whose English and Russian
// differ in more than the words (word order, plurals, decimal comma) branches on isRussian().
namespace Strings
{
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
inline juce::String tr (const char* en, const char* ru) { return juce::String::fromUTF8 (isRussian() ? ru : en); }

// Track types
inline const juce::String trackTypeMidi = tr ("MIDI track", "MIDI-дорожка");
inline const juce::String trackTypeAudio = tr ("Audio track", "Аудиодорожка");
inline const juce::String trackTypeReturn = tr ("Return track", "Возвратная дорожка");
inline const juce::String trackTypeGroup = tr ("Group track", "Групповая дорожка");

// Default names of new objects
inline juce::String defaultTrackName (int number) { return utf8 ("MIDI ") + juce::String (number); }
inline juce::String defaultAudioTrackName (int number) { return tr ("Audio ", "Аудио ") + juce::String (number); }
inline juce::String defaultSceneName (int number) { return tr ("Scene ", "Сцена ") + juce::String (number); }
inline const juce::String defaultClipName = tr ("Clip", "Клип");
inline juce::String defaultMarkerName (int number) { return tr ("Marker ", "Маркер ") + juce::String (number); }
inline const juce::String masterName = tr ("Master", "Мастер");

// Undo transaction names ("Undo <name>" in the Edit menu)
inline const juce::String undoNewTrack = tr ("New Track", "Новая дорожка");
inline const juce::String undoDeleteTrack = tr ("Delete Track", "Удалить дорожку");
inline const juce::String undoDuplicateTrack = tr ("Duplicate Track", "Дублировать дорожку");
inline const juce::String undoRenameTrack = tr ("Rename Track", "Переименовать дорожку");
inline const juce::String undoNewScene = tr ("New Scene", "Новая сцена");
inline const juce::String undoDeleteScene = tr ("Delete Scene", "Удалить сцену");
inline const juce::String undoDuplicateScene = tr ("Duplicate Scene", "Дублировать сцену");
inline const juce::String undoNewClip = tr ("New Clip", "Новый клип");
inline const juce::String undoDeleteClip = tr ("Delete Clip", "Удалить клип");
inline const juce::String undoMoveClip = tr ("Move Clip", "Переместить клип");
inline const juce::String undoCopyClip = tr ("Copy Clip", "Копировать клип");
inline const juce::String undoEditNotes = tr ("Edit Notes", "Изменить ноты");
inline const juce::String undoSetInstrument = tr ("Replace Instrument", "Заменить инструмент");
inline const juce::String undoRemoveInstrument = tr ("Remove Instrument", "Удалить инструмент");
inline const juce::String undoTrackVolume = tr ("Track Volume", "Громкость дорожки");
inline const juce::String undoTrackPan = tr ("Track Pan", "Панорама дорожки");
inline const juce::String undoTrackMute = tr ("Mute Track", "Выключить дорожку");
inline const juce::String undoTrackSolo = tr ("Solo", "Соло");
inline const juce::String undoTempo = tr ("Tempo", "Темп");
inline const juce::String undoMasterVolume = tr ("Master Volume", "Громкость мастера");

// Undo transaction names of the Arrangement (stage 3)
inline const juce::String undoAutoEdit = tr ("Edit Automation", "Изменить автоматизацию");
inline const juce::String undoAutoDelete = tr ("Delete Automation Point", "Удалить точку автоматизации");
inline const juce::String undoAutoClear = tr ("Delete Automation", "Удалить автоматизацию");
inline const juce::String undoArrNewClip = tr ("New Timeline Clip", "Новый клип на шкале");
inline const juce::String undoArrMoveClips = tr ("Move Clips", "Переместить клипы");
inline const juce::String undoArrCopyClips = tr ("Copy Clips", "Копировать клипы");
inline const juce::String undoArrPasteClips = tr ("Paste Clips", "Вставить клипы");
inline const juce::String undoArrDuplicate = tr ("Duplicate", "Дублировать");
inline const juce::String undoArrResizeClip = tr ("Resize Clip", "Изменить длину клипа");
inline const juce::String undoArrSplit = tr ("Split", "Разрезать");
inline const juce::String undoArrJoin = tr ("Join Clips", "Склеить клипы");
inline const juce::String undoArrDelete = tr ("Delete on Timeline", "Удалить на шкале");
inline const juce::String undoArrInsertTime = tr ("Insert Time", "Вставить время");
inline const juce::String undoArrRemoveTime = tr ("Delete Time", "Удалить время");
inline const juce::String undoArrSendToSession = tr ("Send to Session", "Отправить в сессию");
inline const juce::String undoArrLoop = tr ("Arrangement Loop", "Петля аранжировки");
inline const juce::String undoArrMarkerAdd = tr ("New Marker", "Новый маркер");
inline const juce::String undoArrMarkerMove = tr ("Move Marker", "Переместить маркер");
inline const juce::String undoArrMarkerRename = tr ("Rename Marker", "Переименовать маркер");
inline const juce::String undoArrMarkerDelete = tr ("Delete Marker", "Удалить маркер");
inline const juce::String undoArrMarkerColour = tr ("Marker Color", "Цвет маркера");
inline const juce::String undoArrTrackHeight = tr ("Track Height", "Высота дорожки");
inline const juce::String undoAddMarker = tr ("New Marker", "Новый маркер");
inline const juce::String undoRenameMarker = tr ("Rename Marker", "Переименовать маркер");
inline const juce::String undoMoveMarker = tr ("Move Marker", "Переместить маркер");
inline const juce::String undoMarkerColour = tr ("Marker Color", "Цвет маркера");
inline const juce::String undoDeleteMarker = tr ("Delete Marker", "Удалить маркер");

// Menu items and labels used by the Session View
inline const juce::String menuAddMidiTrack = tr ("Add MIDI Track", "Добавить MIDI-дорожку");
inline const juce::String menuDeleteTrack = tr ("Delete Track", "Удалить дорожку");
inline const juce::String menuAddScene = tr ("Add Scene", "Добавить сцену");
inline const juce::String menuDeleteScene = tr ("Delete Scene", "Удалить сцену");
inline const juce::String menuOpenEditor = tr ("Open Instrument Editor", "Открыть редактор инструмента");
inline const juce::String menuRemoveInstrument = tr ("Remove Instrument", "Убрать инструмент");
inline const juce::String menuScanPlugins = tr ("Scan Plug-ins…", "Сканировать плагины…");
inline const juce::String menuPrisma = tr ("Prisma (Built-in Wavetable Synth)", "Prisma (встроенный wavetable-синт)");
inline const juce::String menuPlugins = tr ("AU / VST3 Instruments", "Инструменты AU / VST3");
inline const juce::String menuNoPlugins = tr ("No Plug-ins Found: Click “Scan”", "Плагины не найдены: нажмите «Сканировать»");
inline const juce::String menuCreateDemoClip = tr ("Create Demo Clip", "Создать демо-клип");
inline const juce::String menuReplaceDemoClip = tr ("Replace with Demo Clip", "Заменить демо-клипом");
inline const juce::String menuLaunch = tr ("Launch", "Запустить");
inline const juce::String menuDeleteClip = tr ("Delete Clip", "Удалить клип");
inline const juce::String labelScenes = tr ("SCENES", "СЦЕНЫ");
inline const juce::String labelStopAll = tr ("Stop all", "Стоп все");
inline const juce::String labelNoInstrument = tr ("(no instrument)", "(нет инструмента)");
inline const juce::String labelMaster = tr ("MASTER", "МАСТЕР");
inline const juce::String tipArm = tr ("Record: MIDI keyboards and the computer keyboard play this track", "Запись: MIDI-клавиатуры и клавиши компьютера играют на этой дорожке");

// Status bar hints and errors
inline const juce::String statusAudioDeviceFallback = tr ("Saved audio interface not found, using the system default", "Сохранённая звуковая карта не найдена, выбрана системная");
inline const juce::String errorPluginLoad = tr ("Couldn't load plug-in: ", "Не удалось загрузить плагин: ");
inline const juce::String errorNewerProject = tr ("The project was made in a newer version of DAW", "Проект создан в более новой версии DAW");
inline const juce::String errorBrokenProject = tr ("The project file is damaged or is not a DAW project", "Файл проекта повреждён или не является проектом DAW");
inline juce::String errorFileMissing (const juce::String& path) { return tr ("File not found: ", "Файл не найден: ") + path; }
inline juce::String errorMidiRead (const juce::String& n) { return tr ("Couldn't read MIDI file “", "Не удалось прочитать MIDI-файл «") + n + tr ("”", "»"); }
inline juce::String errorMidiTiming (const juce::String& n) { return tr ("MIDI file “", "MIDI-файл «") + n + tr ("” uses SMPTE timecode, which isn't supported", "» использует тайм-код SMPTE, он не поддерживается"); }
inline juce::String errorMidiNoNotes (const juce::String& n) { return tr ("MIDI file “", "В MIDI-файле «") + n + tr ("” has no notes", "» нет нот"); }
inline juce::String errorFolderExists (const juce::String& path) { return tr ("A project with this name already exists: ", "Проект с таким именем уже есть: ") + path; }
inline juce::String errorWriteFile (const juce::String& path) { return tr ("Couldn't write file: ", "Не удалось записать файл: ") + path; }
inline const juce::String dialogPluginTitle = tr ("Plug-in", "Плагин");
inline const juce::String windowPlugins = tr ("Plug-ins (AU / VST3)", "Плагины (AU / VST3)");
inline const juce::String windowAudioSettings = tr ("Audio and MIDI settings", "Настройки звука и MIDI");

// Undo transaction names of the window actions (task 1.4)
inline const juce::String undoRenameClip = tr ("Rename Clip", "Переименовать клип");
inline const juce::String undoRenameScene = tr ("Rename Scene", "Переименовать сцену");
inline const juce::String undoTrackColour = tr ("Track Color", "Цвет дорожки");
inline const juce::String undoClipColour = tr ("Clip Color", "Цвет клипа");
inline const juce::String undoSceneColour = tr ("Scene Color", "Цвет сцены");
inline const juce::String undoDuplicateClip = tr ("Duplicate Clip", "Дублировать клип");
inline const juce::String undoCutClip = tr ("Cut Clip", "Вырезать клип");
inline const juce::String undoPasteClip = tr ("Paste Clip", "Вставить клип");
inline const juce::String undoCaptureScene = tr ("Scene from Playing Clips", "Сцена с играющими клипами");
inline const juce::String undoTrackArm = tr ("Arm Track", "Запись дорожки");
inline const juce::String undoTrackWidth = tr ("Column Width", "Ширина столбца");
inline const juce::String undoMoveDevice = tr ("Move Device", "Переместить устройство");
inline const juce::String undoRemoveDevice = tr ("Delete Device", "Удалить устройство");
inline const juce::String undoToggleDevice = tr ("Toggle Device", "Включить или выключить устройство");
inline const juce::String undoFoldDevice = tr ("Fold Device", "Свернуть устройство");

// Drops from the browser (task 1.5): the name of the transaction says what was dropped
inline juce::String undoAddDevice (const juce::String& what) { return tr ("Add ", "Добавить ") + what; }
inline juce::String undoReplaceInstrument (const juce::String& what) { return tr ("Replace Instrument · ", "Заменить инструмент · ") + what; }
inline juce::String undoApplyPreset (const juce::String& what) { return tr ("Apply Preset · ", "Применить пресет · ") + what; }
inline juce::String undoAddEffect (const juce::String& what) { return tr ("Add Effect · ", "Добавить эффект · ") + what; }
inline juce::String undoAddChain (const juce::String& what) { return tr ("Add Chain · ", "Добавить цепочку · ") + what; }
inline juce::String undoReplaceChain (const juce::String& what) { return tr ("Replace Chain · ", "Заменить цепочку · ") + what; }
inline juce::String undoAddMidiClip (const juce::String& what) { return tr ("Add MIDI Clip · ", "Добавить MIDI-клип · ") + what; }
inline juce::String errorChainRead (const juce::String& n) { return tr ("Couldn't read chain “", "Не удалось прочитать цепочку «") + n + tr ("”", "»"); }
inline juce::String errorPluginUnknown (const juce::String& n) { return tr ("Plug-in “", "Плагин «") + n + tr ("” is not in the plug-in list", "» не найден в списке плагинов"); }
inline const juce::String errorNoSlot = tr ("No place to put the clip", "Некуда положить клип");
inline juce::String undoAddAudioClip (const juce::String& what) { return tr ("Add Sound · ", "Добавить звук · ") + what; }
inline juce::String undoAddAudioClips (int n) { return tr ("Add Sounds · ", "Добавить звуки · ") + juce::String (n); }
inline const juce::String errorAudioRead = tr ("Couldn't read the audio file", "Не удалось прочитать звуковой файл");
inline const juce::String errorAudioOnMidi = tr ("Audio goes on an audio track or an empty spot", "Звук кладётся на аудиодорожку или на пустое место");

// Dialogs
inline const juce::String dialogOk = utf8 ("OK");
inline const juce::String dialogCancel = tr ("Cancel", "Отмена");
inline const juce::String dialogSave = tr ("Save", "Сохранить");
inline const juce::String dialogDontSave = tr ("Don't save", "Не сохранять");
inline juce::String dialogSaveChanges (const juce::String& name) { return tr ("Save changes to “", "Сохранить изменения в «") + name + tr ("”?", "»?"); }
inline const juce::String dialogSaveChangesText = tr ("If you don't save, your changes will be lost.", "Если не сохранить, изменения будут потеряны.");

// Project files (task 1.7)
inline const juce::String dialogOpenTitle = tr ("Open project", "Открыть проект");
inline const juce::String dialogOpenFailed = tr ("Couldn't open the project", "Не удалось открыть проект");
inline const juce::String dialogSaveAsTitle = tr ("Save project as", "Сохранить проект как");
inline const juce::String dialogSaveAsPrompt = tr ("Project name. A folder “Name Project” will be created.", "Название проекта. Будет создана папка «Название Project».");
inline const juce::String dialogSaveFailed = tr ("Couldn't save the project", "Не удалось сохранить проект");
inline const juce::String recoveryTitle = tr ("Recover unsaved work?", "Восстановить несохранённую работу?");
inline juce::String recoveryText (const juce::String& name)
{
    return tr ("The last session closed unexpectedly. A copy of the project “", "Прошлый сеанс закрылся неожиданно. Найдена копия проекта «") + name + tr ("” with unsaved changes was found.", "» с несохранёнными изменениями.");
}
inline const juce::String recoveryRestore = tr ("Restore", "Восстановить");
inline const juce::String recoveryOpenSaved = tr ("Open last saved", "Открыть последнее сохранение");
inline const juce::String recoveryDiscard = tr ("Don't open", "Не открывать");
inline const juce::String infoSaved = tr ("Project saved", "Проект сохранён");
inline const juce::String infoTemplateSaved = tr ("This project is now the template for new projects", "Этот проект теперь шаблон для новых проектов");
inline const juce::String infoRecovered = tr ("Work restored from a copy. Save the project", "Работа восстановлена из копии. Сохраните проект");
inline const juce::String clearRecent = tr ("Clear list", "Очистить список");

// Version history window
namespace versions
{
inline const juce::String title = tr ("Version history", "История версий");
inline const juce::String labelButton = tr ("Label…", "Метка…");
inline const juce::String restoreButton = tr ("Restore", "Восстановить");
inline const juce::String closeButton = tr ("Close", "Закрыть");
inline const juce::String kindSave = tr ("save", "сохранение");
inline const juce::String kindAutosave = tr ("autosave", "автосохранение");
inline const juce::String kindRestore = tr ("before restore", "до возврата");
inline const juce::String empty = tr ("No versions yet. Save the project: every save and autosave shows up here.", "Версий пока нет. Сохраните проект: каждое сохранение и автосохранение попадает сюда.");
inline juce::String labelPrompt (int number) { return tr ("Label for version v", "Метка версии v") + juce::String (number) + tr (". Leave empty to remove the label.", ". Пустая строка снимает метку."); }
inline juce::String restoreTitle (int number) { return tr ("Go back to version v", "Вернуться к версии v") + juce::String (number) + utf8 ("?"); }
inline const juce::String restoreText = tr ("The current state is kept as a separate version, nothing is lost. But the undo history (Cmd+Z) is cleared: the restore itself can't be undone, you can only go back to the “before restore” version.", "Текущее состояние сохранится отдельной версией, ничего не пропадёт. Но история отмены (Cmd+Z) очистится: сам возврат отменить нельзя, только вернуться к версии «до возврата».");
inline const juce::String restoreOk = tr ("Go back", "Вернуться");
inline const juce::String restored = tr ("Project restored to the selected version", "Проект возвращён к выбранной версии");
// Taking a track from a version (docs/spec/03-arrangement.md, task 3.5)
inline const juce::String takeTrackButton = tr ("Take track…", "Забрать дорожку…");
inline juce::String pickTrack (int number) { return tr ("Tracks of version v", "Дорожки версии v") + juce::String (number); }
inline const juce::String noTracks = tr ("This version has no tracks", "В этой версии нет дорожек");
inline juce::String trackLabel (const juce::String& name, int devices, int sessionClips, int arrangementClips)
{
    return name + tr ("   ·   devices ", "   ·   устройств ") + juce::String (devices) + tr (", clips ", ", клипов ") + juce::String (sessionClips) + utf8 (" + ")
           + juce::String (arrangementClips) + tr (" on timeline", " на шкале");
}
inline juce::String trackTaken (const juce::String& name, int number) { return tr ("Track “", "Дорожка «") + name + tr ("” taken from version v", "» взята из версии v") + juce::String (number); }
} // namespace versions

// Settings window (task 1.9)
namespace settingsui
{
inline const juce::String title = tr ("Settings", "Настройки");
inline const juce::String tabSound = tr ("Audio", "Звук");
inline const juce::String tabMidi = utf8 ("MIDI");
inline const juce::String tabAppearance = tr ("Appearance", "Внешний вид");
inline const juce::String tabLibrary = tr ("Library", "Библиотека");
inline const juce::String tabRecording = tr ("Recording", "Запись");
inline const juce::String driver = tr ("Driver", "Драйвер");
inline const juce::String input = tr ("Input", "Вход");
inline const juce::String output = tr ("Output", "Выход");
inline const juce::String sampleRate = tr ("Sample rate", "Частота");
inline const juce::String bufferSize = tr ("Buffer", "Буфер");
inline const juce::String none = tr ("(none)", "(нет)");
inline const juce::String noDevice = tr ("No audio interface open", "Звуковая карта не открыта");
inline juce::String samples (int n) { return juce::String (n) + tr (" samples", " сэмплов"); }
inline juce::String hz (double r)
{
    const int whole = (int) (r + 0.5);
    if (isRussian())
        return juce::String ((double) whole / 1000.0, whole % 1000 == 0 ? 0 : 1).replace (".", ",") + utf8 (" кГц");
    return juce::String ((double) whole / 1000.0, whole % 1000 == 0 ? 0 : 1) + " kHz";
}
inline juce::String ms (double v)
{
    if (isRussian())
        return juce::String (v, 1).replace (".", ",") + utf8 (" мс");
    return juce::String (v, 1) + " ms";
}
inline juce::String latency (double in, double out)
{
    return tr ("In ", "Вход ") + ms (in) + tr (" · Out ", " · Выход ") + ms (out) + tr (" · Total ", " · Всего ") + ms (in + out);
}
inline juce::String cpu (int percent) { return tr ("CPU load: ", "Нагрузка процессора: ") + juce::String (percent) + " %"; }
inline const juce::String channels = tr ("Channels", "Каналы");
inline const juce::String inputChannels = tr ("Inputs", "Входы");
inline const juce::String outputChannels = tr ("Outputs", "Выходы");
inline const juce::String channelNameHint = tr ("Channel name", "Имя канала");
inline const juce::String testTone = tr ("Test tone", "Тестовый сигнал");
inline const juce::String audioMidiSetup = tr ("Open Audio MIDI Setup", "Открыть настройку Audio MIDI");
inline const juce::String driverNoteMac = tr ("On a Mac every audio interface works through Core Audio. No separate ASIO driver is needed.", "На Mac все звуковые карты работают через Core Audio. Отдельный драйвер ASIO не нужен.");
inline const juce::String driverNoteDev = tr ("ALSA is used for development on Linux.", "Для разработки на Linux используется ALSA.");
inline const juce::String midiInputs = tr ("MIDI inputs", "Входы MIDI");
inline const juce::String midiOutputs = tr ("MIDI outputs", "Выходы MIDI");
inline const juce::String playTracks = tr ("Play tracks", "Игра на дорожках");
inline const juce::String control = tr ("Control", "Управление");
inline const juce::String noMidiInputs = tr ("No MIDI inputs. Connect a keyboard and it will show up here.", "MIDI-входов нет. Подключите клавиатуру: она появится здесь сама.");
inline const juce::String noMidiOutputs = tr ("No MIDI outputs", "MIDI-выходов нет");
inline const juce::String themeHeading = tr ("Theme", "Тема");
inline const juce::String scale = tr ("Interface scale", "Масштаб интерфейса");
inline const juce::String animations = tr ("Animations", "Анимации");
inline const juce::String libraryPath = tr ("User Library folder", "Папка User Library");
inline const juce::String change = tr ("Change…", "Изменить…");
inline const juce::String byDefault = tr ("Default", "По умолчанию");
inline const juce::String chooseLibrary = tr ("Choose the User Library folder", "Выберите папку User Library");
inline const juce::String placesHeading = tr ("Places folders", "Папки «Места»");
inline const juce::String noPlaces = tr ("No folders yet", "Папок пока нет");
inline const juce::String add = tr ("Add…", "Добавить…");
inline const juce::String remove = tr ("Remove", "Убрать");
inline const juce::String rescan = tr ("Rescan plug-ins…", "Повторно сканировать плагины…");
inline const juce::String copySamples = tr ("Copy samples into project", "Копировать сэмплы в проект");
inline const juce::String copySamplesHint = tr ("Audio dropped in from outside is copied to the project's Samples folder, so the project doesn't break when moved", "Звук, брошенный в проект извне, копируется в папку Samples проекта, чтобы проект не ломался при переносе");
inline const juce::String language = tr ("Language", "Язык");
inline const juce::String languageRestart = tr ("Applies after the app restarts", "Применится после перезапуска программы");
} // namespace settingsui

// Window
inline const juce::String windowUntitled = tr ("Untitled", "Без названия");
inline const juce::String labelPlaceholderProject = tr ("Drag an instrument here from the browser", "Перетащите инструмент из браузера сюда");
inline const juce::String labelAppName = utf8 ("Prisma");

// Menu bar (task 1.10). The text of each command is also its name in the menu.
namespace menu
{
inline const juce::String file = tr ("File", "Файл");
inline const juce::String edit = tr ("Edit", "Правка");
inline const juce::String create = tr ("Create", "Создать");
inline const juce::String view = tr ("View", "Вид");
inline const juce::String newProject = tr ("New Project", "Новый проект");
inline const juce::String open = tr ("Open…", "Открыть…");
inline const juce::String openRecent = tr ("Open Recent", "Открыть недавний");
inline const juce::String save = tr ("Save", "Сохранить");
inline const juce::String saveAs = tr ("Save As…", "Сохранить как…");
inline const juce::String saveTemplate = tr ("Save as Default Template", "Сохранить как шаблон по умолчанию");
inline const juce::String versionHistory = tr ("Version History…", "История версий…");
inline const juce::String exportAudio = tr ("Export…", "Экспорт…");
inline const juce::String showInFinder = tr ("Show Project in Finder", "Показать проект в Finder");
inline const juce::String scanPlugins = tr ("Scan Plug-ins…", "Сканировать плагины…");
inline const juce::String quit = tr ("Quit", "Выйти");
inline const juce::String undo = tr ("Undo", "Отменить");
inline const juce::String redo = tr ("Redo", "Повторить");
inline const juce::String cut = tr ("Cut", "Вырезать");
inline const juce::String copy = tr ("Copy", "Копировать");
inline const juce::String paste = tr ("Paste", "Вставить");
inline const juce::String duplicate = tr ("Duplicate", "Дублировать");
inline const juce::String remove = tr ("Delete", "Удалить");
inline const juce::String rename = tr ("Rename", "Переименовать");
inline const juce::String newTrack = tr ("New MIDI Track", "Новая MIDI-дорожка");
inline const juce::String newAudioTrack = tr ("New Audio Track", "Новая аудиодорожка");
inline const juce::String findMissing = tr ("Find Missing Files…", "Найти потерянные файлы…");
inline const juce::String deleteUnusedRecordings = tr ("Delete Unused Recordings…", "Удалить неиспользуемые записи…");
inline const juce::String noUnusedRecordings = tr ("No unused recordings", "Неиспользуемых записей нет");
inline const juce::String deleteButton = tr ("Delete", "Удалить");
inline juce::String deleteUnusedQuestion (int n)
{
    return tr ("Recordings no clip uses: ", "Записей, которые не нужны ни одному клипу: ") + juce::String (n) + tr (". The files will be deleted from disk and the undo history cleared. This can't be undone.", ". Файлы будут удалены с диска, история отмены очистится. Это нельзя отменить.");
}
inline juce::String deletedRecordings (int n)
{
    if (isRussian())
        return utf8 ("Удалено записей: ") + juce::String (n);
    return juce::String (n) + (n == 1 ? " recording deleted" : " recordings deleted");
}
inline const juce::String newScene = tr ("Scene", "Сцена");
inline const juce::String captureScene = tr ("Scene from Playing Clips", "Сцена с играющими клипами");
inline const juce::String newClip = tr ("Empty MIDI Clip", "Пустой MIDI-клип");
inline const juce::String captureMidi = tr ("Capture MIDI", "Захватить MIDI");
inline const juce::String browser = tr ("Browser", "Браузер");
inline const juce::String detail = tr ("Detail Panel", "Детальная панель");
inline const juce::String mixer = tr ("Mixer", "Микшер");
inline const juce::String switchView = utf8 ("Session / Arrangement");
inline const juce::String zoomIn = tr ("Zoom In", "Увеличить масштаб");
inline const juce::String zoomOut = tr ("Zoom Out", "Уменьшить масштаб");
inline const juce::String settings = tr ("Settings…", "Настройки…");
inline const juce::String playStop = tr ("Play / Stop", "Играть / стоп");
inline const juce::String computerKeys = tr ("Play with Computer Keyboard", "Игра на клавиатуре компьютера");
inline const juce::String find = tr ("Search in Browser", "Поиск в браузере");
inline const juce::String insertTrackHere = tr ("Insert MIDI Track", "Вставить MIDI-дорожку");
inline const juce::String insertAudioTrackHere = tr ("Insert Audio Track", "Вставить аудиодорожку");
inline const juce::String colour = tr ("Color", "Цвет");
inline const juce::String createClipHere = tr ("Create MIDI Clip", "Создать MIDI-клип");
inline const juce::String stopAll = tr ("Stop All Clips", "Остановить все клипы");
inline const juce::String quantise = tr ("Quantize", "Квантовать");
inline const juce::String quantiseOptions = tr ("Quantize…", "Квантовать…");
inline const juce::String toggleClipEditor = tr ("Clip Editor / Device Chain", "Редактор клипа / цепочка устройств");
inline const juce::String keyboardStrip = tr ("On-Screen Keyboard", "Экранная клавиатура");
inline const juce::String projectProperties = tr ("Project Properties…", "Свойства проекта…");
inline const juce::String openClip = tr ("Open in Editor", "Открыть в редакторе");
} // namespace menu

// Audio in Session View
namespace sessionAudio
{
inline const juce::String fileNotFound = tr ("File not found", "Файл не найден");
inline const juce::String undoNewAudioTrack = tr ("New Audio Track", "Новая аудиодорожка");
inline const juce::String audioTag = tr ("Audio", "Аудио");
inline const juce::String findMissingTitle = tr ("Choose a folder to search for missing files", "Выберите папку, где искать потерянные файлы");
inline const juce::String undoRelink = tr ("Find Missing Files", "Найти потерянные файлы");
inline juce::String foundFiles (int n)
{
    if (isRussian())
        return utf8 ("Найдено файлов: ") + juce::String (n);
    return juce::String (n) + (n == 1 ? " file found" : " files found");
}
inline const juce::String nothingFound = tr ("Nothing found", "Ничего не найдено");
inline const juce::String diskBehind = tr ("Disk too slow: some audio was skipped", "Диск не успевает: часть звука пропущена");
} // namespace sessionAudio

// Transport bar
namespace transport
{
inline const juce::String arrangementTab = utf8 ("Arrangement");
inline const juce::String sessionTab = utf8 ("Session");
inline const juce::String capture = tr ("Capture", "Захватить");
inline const juce::String capture10 = tr ("10 min", "10 мин");
inline const juce::String versionNone = tr ("v1 · new", "v1 · новый");
// "v14 · now", "v14 · 5 min", "v14 · 3 h"; sinceMs < 0: never saved by hand
inline juce::String versionText (int number, juce::int64 sinceMs)
{
    juce::String when = tr ("auto", "авто");
    if (sinceMs >= 0)
    {
        const auto minutes = sinceMs / 60000;
        when = minutes < 1 ? tr ("now", "сейчас") : (minutes < 60 ? juce::String (minutes) + tr (" min", " мин") : juce::String (minutes / 60) + tr (" h", " ч"));
    }
    return "v" + juce::String (number) + utf8 (" · ") + when;
}
inline const juce::String cpu = utf8 ("CPU");
inline const juce::String keysOn = tr ("KEYS", "КЛАВ");
inline const juce::String midi = utf8 ("MIDI");
inline const juce::String scaleOff = tr ("Scale: off", "Лад: выкл");
} // namespace transport

// Browser panel (frame only; the sources come with task 1.5)
namespace browser
{
inline const juce::String searchPlaceholder = tr ("Search  ⌘F", "Поиск  ⌘F");
inline const juce::String instruments = tr ("Instruments", "Инструменты");
inline const juce::String presets = tr ("Presets", "Пресеты");
inline const juce::String effects = tr ("Effects", "Эффекты");
inline const juce::String plugins = tr ("Plug-ins", "Плагины");
inline const juce::String sounds = tr ("Sounds", "Звуки");
inline const juce::String midiFiles = tr ("MIDI files", "MIDI-файлы");
inline const juce::String places = tr ("Places", "Места");
inline const juce::String emptyCategory = tr ("Items of this category will appear here", "Здесь появятся элементы категории");
inline juce::String notFound (const juce::String& query) { return tr ("Nothing found for “", "Ничего не найдено по «") + query + tr ("”", "»"); }
inline const juce::String chains = tr ("Chains", "Цепочки");
inline const juce::String recent = tr ("Recent", "Недавние");
inline const juce::String builtIn = tr ("built-in", "встроенный");
inline const juce::String otherVendor = tr ("Other vendors", "Другие производители");
inline const juce::String builtinGroup = tr ("Built-in", "Встроенные");
inline const juce::String builtinEffectsSoon = tr ("Coming in stage 6", "Появятся на этапе 6");
inline const juce::String userChains = tr ("My chains", "Мои цепочки");
inline const juce::String devicesShort = tr ("dev.", "устр.");
inline juce::String userGroup (const juce::String& device) { return tr ("My presets · ", "Мои пресеты · ") + device; }
inline juce::String projectPlace (const juce::String& folderName) { return tr ("Project · ", "Проект · ") + folderName; }
inline const juce::String addFolder = tr ("Add Folder…", "Добавить папку…");
inline const juce::String dropFolder = tr ("Release to add the folder to Places", "Отпустите, чтобы добавить папку в Места");
inline const juce::String addFolderTitle = tr ("Choose a folder for Places", "Выберите папку для «Мест»");
inline const juce::String removeFolder = tr ("Remove from Places", "Убрать из «Мест»");
inline const juce::String noPlaces = tr ("Add a folder with sounds and MIDI files", "Добавьте папку со звуками и MIDI-файлами");
inline const juce::String previewToggle = tr ("Preview", "Предпрослушивание");
inline const juce::String labelColour = tr ("Label Color", "Цвет метки");
inline const juce::String labelNone = tr ("No Label", "Без метки");
inline const juce::String renamePreset = tr ("Rename…", "Переименовать…");
inline const juce::String deletePreset = tr ("Delete…", "Удалить…");
inline const juce::String showInFinder = tr ("Show in Finder", "Показать в Finder");
inline const juce::String emptyRecent = tr ("Items you have used will appear here", "Здесь появятся элементы, которые вы использовали");
inline const juce::String emptyLabel = tr ("No items with this label", "Нет элементов с этой меткой");
inline const juce::String deleteTitle = tr ("Delete from library", "Удалить из библиотеки");
inline juce::String deleteQuestion (const juce::String& name) { return tr ("Delete “", "Удалить «") + name + tr ("”? This can't be undone.", "»? Это нельзя отменить."); }
inline const juce::String deleteOk = tr ("Delete", "Удалить");
inline const juce::String dropNewTrack = tr ("Release to create a new track", "Отпустите, чтобы создать новую дорожку");
inline const juce::String saveChain = tr ("Save Chain…", "Сохранить цепочку…");
inline const juce::String savePreset = tr ("Save Preset…", "Сохранить пресет…");
inline const juce::String savePresetTitle = tr ("Save preset", "Сохранить пресет");
inline const juce::String savePresetPrompt = tr ("Preset name", "Название пресета");
inline const juce::String saveChainPrompt = tr ("Chain name", "Название цепочки");
inline juce::String presetSaved (const juce::String& n) { return tr ("Preset “", "Пресет «") + n + tr ("” saved to the library", "» сохранён в библиотеку"); }
inline juce::String chainSaved (const juce::String& n) { return tr ("Chain “", "Цепочка «") + n + tr ("” saved to the library", "» сохранена в библиотеку"); }
inline const juce::String factoryPresets = tr ("Prisma presets", "Пресеты Prisma");
inline const juce::String myPresets = tr ("My presets", "Мои пресеты");
inline const juce::String noUserPresets = tr ("(none yet)", "(пока нет)");
inline const juce::String replaceInstrument = tr ("Replace Instrument", "Заменить инструмент");
inline const juce::String useOnTrack = tr ("Use on Selected Track", "Поставить на выбранную дорожку");
} // namespace browser

// Detail panel and device chain
namespace detail
{
inline const juce::String transpose = tr ("Transpose", "Транспонирование");
inline const juce::String loading = tr ("Loading…", "Загрузка…");
inline const juce::String track = tr ("Track", "Дорожка");
inline const juce::String input = tr ("Input", "Вход");
inline const juce::String monitor = tr ("Monitoring", "Мониторинг");
inline const juce::String chain = tr ("Chain", "Цепочка");
inline const juce::String noTrack = tr ("Select a track to see its device chain", "Выберите дорожку, чтобы увидеть её цепочку устройств");
inline const juce::String dropHere = tr ("Drag an effect or plug-in here from the browser", "Перетащите сюда эффект или плагин из браузера");
inline juce::String devices (int n) { return juce::String (n) + tr (" dev.", " устр."); }
inline juce::String missingPlugin (const juce::String& name) { return tr ("Plug-in not found: ", "Плагин не найден: ") + name; }
inline const juce::String inputAllMidi = tr ("All MIDI inputs", "Все MIDI-входы");
inline const juce::String inputKeyboard = tr ("Computer keyboard", "Клавиатура компьютера");
inline const juce::String inputNone = tr ("None", "Нет");
inline const juce::String inputDevice = tr ("MIDI device", "MIDI-устройство");
inline const juce::String monitorAuto = tr ("Auto", "Авто");
inline const juce::String monitorIn = tr ("In", "Вкл");
inline const juce::String monitorOff = tr ("Off", "Выкл");
inline const juce::String showOtherParam = tr ("Show Another Parameter…", "Показать другой параметр…");
} // namespace detail

// Settings, tab "Recording": recording of sound (docs/spec/05-audio-recording.md, task 5.4)
namespace audioRec
{
inline const juce::String heading = tr ("Audio recording latency", "Задержка записи звука");
inline const juce::String driverLatency = tr ("Latency reported by the driver", "Задержка по данным драйвера");
inline const juce::String measure = tr ("Measure latency", "Измерить задержку");
inline const juce::String running = tr ("Listening for clicks…", "Слушаю щелчки…");
inline const juce::String notFound = tr ("The clicks did not come back. Connect output 1 to the input with a cable and turn the volume up.",
                                         "Щелчки не вернулись. Соедините кабелем выход 1 со входом и поднимите громкость.");
inline juce::String measured (double roundTripMs, double correctionMs)
{
    if (isRussian())
        return utf8 ("Петля ") + juce::String (roundTripMs, 1).replace (".", ",") + utf8 (" мс, поправка ") + juce::String (correctionMs, 1).replace (".", ",")
               + utf8 (" мс сохранена");
    return "Round trip " + juce::String (roundTripMs, 1) + " ms, correction " + juce::String (correctionMs, 1) + " ms saved";
}
inline const juce::String bitDepth = tr ("Recording bit depth", "Разрядность записи");
inline const juce::String float32 = utf8 ("32 (float)");
inline const juce::String testNote = tr ("Recorded audio is shifted earlier by the input and output latency. To measure it precisely, connect output 1 of the interface to the selected input with a cable and press “Measure”: 10 clicks will play. The slider adjusts it by hand.",
                                         "Записанный звук сдвигается раньше на задержку входа и выхода. Чтобы измерить её точно, соедините кабелем выход 1 карты с выбранным входом и нажмите «Измерить»: прозвучат 10 щелчков. Ползунок поправляет вручную.");
} // namespace audioRec

// The export window (docs/spec/04-export.md)
namespace exportUi
{
inline const juce::String title = tr ("Export", "Экспорт");
inline const juce::String range = tr ("Range", "Диапазон");
inline const juce::String format = tr ("Format", "Формат");
inline const juce::String bits = tr ("Bit depth", "Разрядность");
inline const juce::String normalise = tr ("Normalize", "Нормализация");
inline const juce::String wholeSong = tr ("Whole song", "Вся песня");
inline const juce::String loop = tr ("Loop", "Петля");
inline const juce::String none = tr ("None", "Нет");
inline const juce::String peak01 = tr ("Peak −0.1 dBFS", "По пику −0,1 dBFS");
inline const juce::String peak1 = tr ("Peak −1 dBFS", "По пику −1 dBFS");
inline const juce::String tail = tr ("Add tail (reverb, delay)", "Добавлять хвост (ревер, дилей)");
inline const juce::String exportButton = tr ("Export…", "Экспорт…");
inline const juce::String close = tr ("Close", "Закрыть");
inline const juce::String cancel = tr ("Cancel", "Отмена");
inline const juce::String showInFinder = tr ("Show in Finder", "Показать в Finder");
inline const juce::String rendering = tr ("Rendering…", "Рендер…");
inline const juce::String cancelled = tr ("Export cancelled", "Экспорт отменён");
inline const juce::String failed = tr ("Failed: ", "Не получилось: ");
inline const juce::String emptyArrangement = tr ("The arrangement is empty. Record a performance from Session View to the timeline (F9) and export again.",
                                                 "Аранжировка пуста. Запишите выступление из Session View на шкалу (F9) и повторите экспорт.");
inline juce::String done (const juce::String& file, double seconds, double peakDb)
{
    const int s = (int) std::round (seconds);
    if (isRussian())
        return utf8 ("Готово: ") + file + "\n" + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2) + utf8 (", пик ")
               + juce::String (peakDb, 1).replace (".", ",") + " dBFS";
    return "Done: " + file + "\n" + juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2) + ", peak " + juce::String (peakDb, 1) + " dBFS";
}
inline juce::String clipping (double peakDb)
{
    if (isRussian())
        return utf8 ("Пик +") + juce::String (peakDb, 1).replace (".", ",") + utf8 (" dBFS: в файле будет клиппинг. Включите нормализацию или лимитер на мастере.");
    return "Peak +" + juce::String (peakDb, 1) + " dBFS: the file will clip. Turn on normalize or put a limiter on the master.";
}
} // namespace exportUi

// Return tracks and sends (docs/spec/06-effects-mixing.md, task 6.3)
namespace returns
{
inline const juce::String reverbName = tr ("A Reverb", "A Ревер");
inline const juce::String delayName = tr ("B Delay", "B Дилей");
inline const juce::String newReturn = tr ("New return track", "Новая возвратная дорожка");
inline const juce::String sidechain = tr ("Sidechain", "Сайдчейн");
inline const juce::String sidechainNone = tr ("none (own input)", "нет (свой вход)");
inline const juce::String undoSidechain = tr ("Sidechain", "Сайдчейн");
} // namespace returns
inline const juce::String undoSend = tr ("Send", "Посыл");

// Inspiration: generated parts and sounds
namespace inspiration
{
inline const juce::String menu = tr ("Inspiration", "Вдохновение");
inline const juce::String idea = tr ("Whole Idea", "Идея целиком");
inline const juce::String drums = tr ("Drums", "Барабаны");
inline const juce::String chords = tr ("Chords", "Аккорды");
inline const juce::String bass = tr ("Bass", "Бас");
inline const juce::String melody = tr ("Melody", "Мелодия");
inline const juce::String arp = tr ("Arpeggio", "Арпеджио");
inline const juce::String sound = tr ("New Sound", "Новый звук");
inline const juce::String undo = tr ("Inspiration: Part", "Вдохновение: партия");
inline const juce::String undoSound = tr ("Inspiration: Sound", "Вдохновение: звук");
inline const juce::String undoIdea = tr ("Inspiration: Idea", "Вдохновение: идея");
inline juce::String ideaScene (const juce::String& style) { return tr ("Idea · ", "Идея · ") + style; }
inline juce::String made (const juce::String& part, const juce::String& track)
{
    if (isRussian())
        return utf8 ("Готово: ") + part.toLowerCase() + utf8 (" на дорожке ") + track + utf8 (". Не нравится: Cmd+Z и ещё раз.");
    return "Done: " + part.toLowerCase() + " on track " + track + ". Don't like it? Cmd+Z and try again.";
}
inline juce::String newSound (const juce::String& name, const juce::String& track)
{
    if (isRussian())
        return utf8 ("Новый звук «") + name + utf8 ("» на дорожке ") + track;
    return juce::String::fromUTF8 ("New sound \xe2\x80\x9c") + name + juce::String::fromUTF8 ("\xe2\x80\x9d on track ") + track;
}
inline juce::String styleName (const juce::String& s)
{
    static const std::map<juce::String, const char*> names { { "pop", "Поп" }, { "house", "Хаус" }, { "techno", "Техно" }, { "hiphop", "Хип-хоп" },
                                                             { "trap", "Трэп" }, { "dnb", "Драм-н-бейс" }, { "ambient", "Эмбиент" } };
    static const std::map<juce::String, const char*> namesEn { { "pop", "Pop" }, { "house", "House" }, { "techno", "Techno" }, { "hiphop", "Hip-Hop" },
                                                               { "trap", "Trap" }, { "dnb", "Drum & Bass" }, { "ambient", "Ambient" } };
    const auto& map = isRussian() ? names : namesEn;
    const auto it = map.find (s);
    return it != map.end() ? utf8 (it->second) : s;
}
inline juce::String soundKind (const juce::String& k)
{
    static const std::map<juce::String, const char*> names { { "bass", "Бас" }, { "sub", "Саб-бас" }, { "lead", "Лид" }, { "pad", "Пэд" },
                                                             { "pluck", "Плак" }, { "keys", "Клавиши" }, { "bell", "Колокольчик" }, { "fx", "Эффект" },
                                                             { "arp", "Арп" } };
    static const std::map<juce::String, const char*> namesEn { { "bass", "Bass" }, { "sub", "Sub Bass" }, { "lead", "Lead" }, { "pad", "Pad" },
                                                               { "pluck", "Pluck" }, { "keys", "Keys" }, { "bell", "Bell" }, { "fx", "FX" },
                                                               { "arp", "Arp" } };
    const auto& map = isRussian() ? names : namesEn;
    const auto it = map.find (k);
    return it != map.end() ? utf8 (it->second) : k;
}
} // namespace inspiration

// The built-in drum machine
namespace drums
{
inline const juce::String group = tr ("Drums", "Барабаны");
inline const juce::String deviceName = tr ("Drum Machine", "Драм-машина");
inline const juce::String dropOnPad = tr ("Drop a sound on a pad, click plays the pad", "Бросьте звук на пэд, щелчок играет пэд");
inline const juce::String tune = tr ("Tune", "Тон");
inline const juce::String decay = tr ("Decay", "Спад");
inline const juce::String volume = tr ("Vol.", "Громк.");
inline const juce::String cutoff = tr ("Cutoff", "Срез");
inline const juce::String pan = tr ("Pan", "Панор.");
inline const juce::String drive = tr ("Drive", "Драйв");
inline const juce::String help = tr ("Drum machine\nClicking a pad plays it and selects it for the knobs on the right. Drag an audio file onto a pad from the browser or Finder. Notes C1–D#2.",
                                     "Драм-машина\nЩелчок по пэду играет его и выбирает для ручек справа. Перетащите на пэд звуковой файл из браузера или Finder. Ноты C1–D#2.");
} // namespace drums

// Built-in effects (docs/spec/06-effects-mixing.md): names of the effects, their parameters and choices. Ids come from core;
// whatever has no translation here shows the English name from core. In English, parameters, choices and units come from core as they are.
namespace fx
{
inline juce::String effectName (const juce::String& type)
{
    static const std::map<juce::String, const char*> names {
        { "eq8", "Эквалайзер" }, { "comp", "Компрессор" }, { "gate", "Гейт" }, { "limiter", "Лимитер" }, { "reverb", "Ревербератор" },
        { "delay", "Дилей" }, { "drive", "Сатуратор" }, { "crush", "Биткрашер" }, { "filter", "Фильтр" }, { "chorus", "Хорус" },
        { "flanger", "Фланжер" }, { "phaser", "Фейзер" }, { "autopan", "Автопан" },
    };
    static const std::map<juce::String, const char*> namesEn {
        { "eq8", "EQ" }, { "comp", "Compressor" }, { "gate", "Gate" }, { "limiter", "Limiter" }, { "reverb", "Reverb" },
        { "delay", "Delay" }, { "drive", "Saturator" }, { "crush", "Bitcrusher" }, { "filter", "Filter" }, { "chorus", "Chorus" },
        { "flanger", "Flanger" }, { "phaser", "Phaser" }, { "autopan", "Auto Pan" },
    };
    const auto& map = isRussian() ? names : namesEn;
    const auto it = map.find (type);
    return it != map.end() ? utf8 (it->second) : type;
}

inline juce::String paramName (const juce::String& type, const juce::String& id, const juce::String& fallback)
{
    if (! isRussian())
        return fallback;
    static const std::map<juce::String, const char*> names {
        { "mix", "Микс" }, { "output", "Выход" }, { "threshold", "Порог" }, { "ratio", "Ratio" }, { "attack", "Атака" }, { "release", "Релиз" },
        { "autoRelease", "Авторелиз" }, { "knee", "Колено" }, { "makeup", "Makeup" }, { "autoMakeup", "Авто-makeup" }, { "lookahead", "Lookahead" },
        { "detector", "Детектор" }, { "scHpf", "ФВЧ сайдчейна" }, { "scListen", "Слушать сайдчейн" }, { "hysteresis", "Гистерезис" },
        { "hold", "Удержание" }, { "floor", "Глубина" }, { "ceiling", "Потолок" }, { "gain", "Усиление" }, { "truePeak", "True peak" },
        { "size", "Размер" }, { "decay", "Спад" }, { "predelay", "Предзадержка" }, { "predelaySync", "Предзадержка (синхр.)" }, { "width", "Ширина" },
        { "damping", "Демпф. ВЧ" }, { "lowCut", "Срез НЧ" }, { "diffusion", "Диффузия" }, { "freeze", "Заморозка" }, { "modulation", "Модуляция" },
        { "sync", "Синхр." }, { "timeL", "Время L" }, { "timeR", "Время R" }, { "divL", "Доля L" }, { "divR", "Доля R" }, { "link", "Связать" },
        { "feedback", "Обр. связь" }, { "pingPong", "Пинг-понг" }, { "hp", "ФВЧ" }, { "lp", "ФНЧ" }, { "modRate", "Частота мод." },
        { "modDepth", "Глубина мод." }, { "ducking", "Дакинг" }, { "mode", "Режим" }, { "drive", "Драйв" }, { "tone", "Тон" }, { "bias", "Смещение" },
        { "input", "Вход" }, { "bits", "Биты" }, { "rate", "Частота" }, { "dither", "Дизер" }, { "type", "Тип" }, { "freq", "Частота" },
        { "resonance", "Резонанс" }, { "lfoShape", "Форма LFO" }, { "lfoSync", "Синхр. LFO" }, { "lfoRate", "Частота LFO" }, { "lfoDiv", "Доля LFO" },
        { "lfoDepth", "Глубина LFO" }, { "envDepth", "Огибающая" }, { "envAttack", "Атака огиб." }, { "envRelease", "Релиз огиб." },
        { "voices", "Голоса" }, { "depth", "Глубина" }, { "delay", "Задержка" }, { "phase", "Фаза" }, { "div", "Доля" }, { "stages", "Ступени" },
        { "center", "Центр" }, { "shape", "Форма" }, { "offset", "Сдвиг" },
    };
    if (type == "eq8" && id.length() >= 3 && id[0] == 'b' && juce::CharacterFunctions::isDigit (id[1]))
    {
        const auto band = id.substring (1, 2), what = id.substring (2);
        static const std::map<juce::String, const char*> parts { { "on", "вкл" }, { "type", "тип" }, { "freq", "частота" }, { "gain", "усиление" },
                                                                 { "q", "Q" }, { "ch", "канал" } };
        const auto it = parts.find (what);
        return utf8 ("Полоса ") + band + " " + (it != parts.end() ? utf8 (it->second) : what);
    }
    const auto it = names.find (id);
    return it != names.end() ? utf8 (it->second) : fallback;
}

inline juce::String choice (const juce::String& c)
{
    if (! isRussian())
        return c;
    static const std::map<juce::String, const char*> names {
        { "Off", "Выкл" }, { "On", "Вкл" }, { "Stereo", "Стерео" }, { "Left/Right", "Лев./Прав." }, { "Mid/Side", "Mid/Side" }, { "Both", "Оба" },
        { "Low cut 6", "Срез НЧ 6" }, { "Low cut 12", "Срез НЧ 12" }, { "Low cut 24", "Срез НЧ 24" }, { "Low cut 48", "Срез НЧ 48" },
        { "Low shelf", "Полка НЧ" }, { "Bell", "Колокол" }, { "Notch", "Режекция" }, { "High shelf", "Полка ВЧ" }, { "High cut 6", "Срез ВЧ 6" },
        { "High cut 12", "Срез ВЧ 12" }, { "High cut 24", "Срез ВЧ 24" }, { "High cut 48", "Срез ВЧ 48" }, { "Sine", "Синус" },
        { "Triangle", "Треугольник" }, { "Saw", "Пила" }, { "Square", "Меандр" }, { "Soft", "Мягкий" }, { "Tape", "Лента" }, { "Tube", "Лампа" },
        { "Hard", "Жёсткий" }, { "Fold", "Fold" }, { "Crush", "Crush" },
    };
    const auto it = names.find (c);
    return it != names.end() ? utf8 (it->second) : c;
}

inline juce::String unit (const std::string& u)
{
    if (isRussian())
    {
        if (u == "Hz")
            return utf8 ("Гц");
        if (u == "dB")
            return utf8 ("дБ");
        if (u == "ms")
            return utf8 ("мс");
        if (u == "s")
            return utf8 ("с");
    }
    return juce::String (u);
}
inline const juce::String kHz = tr (" kHz", " кГц");
inline const juce::String group = tr ("Built-in", "Встроенные");
inline juce::String categoryName (const juce::String& category)
{
    static const std::map<juce::String, const char*> names { { "eq", "Эквалайзер" }, { "dynamics", "Динамика" }, { "space", "Пространство" },
                                                             { "colour", "Окраска" }, { "filter", "Фильтр" }, { "modulation", "Модуляция" } };
    static const std::map<juce::String, const char*> namesEn { { "eq", "EQ" }, { "dynamics", "Dynamics" }, { "space", "Space" },
                                                               { "colour", "Color" }, { "filter", "Filter" }, { "modulation", "Modulation" } };
    const auto& map = isRussian() ? names : namesEn;
    const auto it = map.find (category);
    return it != map.end() ? utf8 (it->second) : category;
}
inline const juce::String gainReduction = tr ("Reduction", "Подавление");
} // namespace fx

// Inputs and monitoring of a track (docs/spec/05-audio-recording.md, task 5.3)
namespace audioInput
{
inline juce::String mono (int channel) { return tr ("Input ", "Вход ") + juce::String (channel + 1); }
inline juce::String stereo (int left, int right) { return tr ("Inputs ", "Входы ") + juce::String (left + 1) + "/" + juce::String (right + 1); }
inline const juce::String separator = utf8 (" · ");
inline const juce::String resample = tr ("Resampling", "Ресемплинг");
inline const juce::String none = tr ("None", "Нет");
inline const juce::String monoInputs = tr ("Mono", "Моно");
inline const juce::String noInputs = tr ("The audio interface has no open inputs (Settings → Audio)", "У звуковой карты нет открытых входов (Настройки → Звук)");
inline const juce::String monitorAutoLong = tr ("Auto: audible when the track is armed and no clip is playing", "Авто: слышно, когда дорожка взведена и клип не играет");
inline const juce::String monitorInLong = tr ("In: always audible", "Вкл: слышно всегда");
inline const juce::String monitorOffLong = tr ("Off: not audible", "Выкл: не слышно");
inline const juce::String undoInput = tr ("Track Input", "Вход дорожки");
inline const juce::String undoMonitor = tr ("Track Monitoring", "Мониторинг дорожки");
inline const juce::String meterHelp = tr ("Input level. Red: overloaded above −0.5 dB, click to reset.", "Уровень входа. Красный: был перегруз выше −0,5 дБ, щелчок сбрасывает.");
} // namespace audioInput

// Scales (task 2.7). The names of the scales are in core (daw/music/Scales.h).
inline const juce::String rootNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline const juce::String scaleNone = tr ("Off", "Выкл");
inline const juce::String scaleTonic = tr ("Root", "Тоника");
inline const juce::String scaleMode = tr ("Scale", "Лад");

// Undo transaction names of the clip editor and of the stage 2 project settings
inline const juce::String undoNewNote = tr ("Draw Note", "Нарисовать ноту");
inline const juce::String undoDeleteNotes = tr ("Delete Notes", "Удалить ноты");
inline const juce::String undoMoveNotes = tr ("Move Notes", "Переместить ноты");
inline const juce::String undoResizeNotes = tr ("Resize Notes", "Изменить длину нот");
inline const juce::String undoCopyNotes = tr ("Copy Notes", "Копировать ноты");
inline const juce::String undoPasteNotes = tr ("Paste Notes", "Вставить ноты");
inline const juce::String undoCutNotes = tr ("Cut Notes", "Вырезать ноты");
inline const juce::String undoDuplicateNotes = tr ("Duplicate Notes", "Дублировать ноты");
inline const juce::String undoVelocity = tr ("Velocity", "Сила нажатия");
inline const juce::String undoQuantise = tr ("Quantize", "Квантизация");
inline const juce::String undoQuantiseSettings = tr ("Quantize Settings", "Настройки квантизации");
inline const juce::String undoTransposeNotes = tr ("Transpose Notes", "Транспонировать ноты");
inline const juce::String undoSplitNotes = tr ("Split Notes", "Разрезать ноты");
inline const juce::String undoJoinNotes = tr ("Join Notes", "Склеить ноты");
inline const juce::String undoLoopRange = tr ("Clip Loop", "Петля клипа");
inline const juce::String undoHalveTime = tr ("Clip Double Speed", "Клип вдвое быстрее");
inline const juce::String undoDoubleTime = tr ("Clip Half Speed", "Клип вдвое медленнее");
inline const juce::String undoLoopSwitch = tr ("Loop On / Off", "Петля вкл / выкл");
inline const juce::String undoStartPoint = tr ("Clip Start Point", "Точка старта клипа");
inline const juce::String undoClipScale = tr ("Clip Scale", "Лад клипа");
inline const juce::String undoProjectScale = tr ("Project Scale", "Лад проекта");
inline const juce::String undoTuning = tr ("Tuning", "Строй");
inline const juce::String undoTrackTranspose = tr ("Track Transpose", "Транспонирование дорожки");
inline const juce::String undoSignature = tr ("Time Signature", "Размер такта");
inline const juce::String undoAuthor = tr ("Project Author", "Автор проекта");
inline const juce::String undoProjectNotes = tr ("Project Notes", "Заметки проекта");

// The clip editor (task 2.4)
namespace editor
{
inline const juce::String noClip = tr ("No clip selected", "Клип не выбран");
inline const juce::String noClipHint = tr ("Select a clip in Session View: a double-click opens it here. Shift+Tab brings back the device chain.",
                                           "Выберите клип в Session View: двойной щелчок открывает его здесь. Shift+Tab возвращает цепочку устройств.");
inline const juce::String loop = tr ("Loop", "Петля");
inline const juce::String loopStart = tr ("Loop start", "Начало петли");
inline const juce::String loopEnd = tr ("End", "Конец");
inline const juce::String startPoint = tr ("Start", "Старт");
inline const juce::String grid = tr ("Grid", "Сетка");
inline const juce::String gridAdaptive = tr ("Adaptive", "Адаптивная");
inline const juce::String snap = tr ("Snap", "Прилипание");
inline const juce::String ownScale = tr ("Own scale", "Свой лад");
inline const juce::String scaleOnly = tr ("Scale only", "Только лад");
inline const juce::String fold = utf8 ("Fold");
inline const juce::String quantise = tr ("Quantize…", "Квантовать…");
inline const juce::String transpose = tr ("Transp.", "Транспон.");
inline const juce::String halveTime = tr ("÷2 faster", "÷2 быстрее");
inline const juce::String doubleTime = tr ("×2 slower", "×2 медленнее");
inline const juce::String pencil = tr ("Pencil", "Карандаш");
inline const juce::String preview = tr ("Preview", "Слушать");
inline const juce::String velocity = utf8 ("Velocity");
inline const juce::String controllers = tr ("Controllers", "Контроллеры");
inline const juce::String modwheel = utf8 ("Modwheel");
inline const juce::String pitchBend = utf8 ("Pitch Bend");
inline const juce::String sustain = tr ("Sustain", "Сустейн");
inline const juce::String controllersEmpty = tr ("No movements: record them from a MIDI keyboard or draw with the mouse",
                                                 "Нет движений: запишите их с MIDI-клавиатуры или проведите мышью");
inline const juce::String menuSelectAll = tr ("Select All", "Выделить все");
inline const juce::String menuInvert = tr ("Invert Selection", "Инвертировать выделение");
inline const juce::String menuSelectPitch = tr ("Select Notes of This Pitch", "Выделить ноты этой высоты");
inline const juce::String menuDelete = tr ("Delete", "Удалить");
inline const juce::String menuDuplicate = tr ("Duplicate", "Дублировать");
inline const juce::String menuCopy = tr ("Copy", "Копировать");
inline const juce::String menuPaste = tr ("Paste", "Вставить");
inline const juce::String menuSplit = tr ("Split Here", "Разрезать здесь");
inline const juce::String menuJoin = tr ("Join", "Склеить");
inline const juce::String menuQuantise = tr ("Quantize", "Квантовать");
inline const juce::String menuQuantiseOptions = tr ("Quantize…", "Квантовать…");
inline const juce::String menuLoopSelection = tr ("Loop Selection", "Петля вокруг выделения");
inline juce::String selectedCount (int n, int total) { return juce::String (n) + tr (" of ", " из ") + juce::String (total); }
inline const juce::String gridDivisionNames[] = { utf8 ("1/4"), utf8 ("1/8"), utf8 ("1/16"), utf8 ("1/32"), utf8 ("1/64") };
inline juce::String gridName (int denominator, bool triplet) { return "1/" + juce::String (denominator) + (triplet ? "T" : ""); }
inline juce::String loopExtended (double beats)
{
    const auto n = juce::String (beats, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    if (isRussian())
        return utf8 ("Петля клипа удлинена до ") + n + utf8 (" долей");
    return "Clip loop extended to " + n + (n == "1" ? " beat" : " beats");
}
} // namespace editor

namespace quantiseUi
{
inline const juce::String title = tr ("Quantize", "Квантизация");
inline const juce::String grid = tr ("Grid", "Сетка");
inline const juce::String strength = tr ("Strength", "Сила");
inline const juce::String swing = tr ("Swing", "Свинг");
inline const juce::String ends = tr ("Quantize note ends", "Квантовать концы нот");
inline const juce::String apply = tr ("Apply", "Применить");
inline const juce::String close = tr ("Close", "Закрыть");
inline const juce::String gridNames[] = { utf8 ("1/4"), utf8 ("1/8"), utf8 ("1/8T"), utf8 ("1/16"), utf8 ("1/16T"), utf8 ("1/32"), utf8 ("1/32T") };
inline const double gridBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
inline constexpr int numGrids = 7;
} // namespace quantiseUi

namespace projectProps
{
inline const juce::String title = tr ("Project properties", "Свойства проекта");
inline const juce::String tempo = tr ("Tempo", "Темп");
inline const juce::String signature = tr ("Time signature", "Размер");
inline const juce::String scale = tr ("Scale", "Лад");
inline const juce::String tuning = tr ("A4 tuning", "Строй A4");
inline const juce::String tuningSuffix = tr (" Hz", " Гц");
inline const juce::String tuningHint = tr ("Built-in synths. Third-party plug-ins set their tuning inside the plug-in",
                                           "Встроенные синты. У сторонних плагинов строй настраивается в самом плагине");
inline const juce::String author = tr ("Author", "Автор");
inline const juce::String notes = tr ("Notes", "Заметки");
inline const juce::String reset = utf8 ("440");
} // namespace projectProps

namespace keyboardUi
{
inline const juce::String inScale = tr ("In scale", "В ладу");
inline const juce::String chords = tr ("Chords", "Аккорды");
// "C3" for the note the A key plays; the octave runs from there over the 17 keys (A to ;).
inline juce::String octaveName (int baseNote) { return juce::MidiMessage::getMidiNoteName (baseNote, true, true, 3); }
inline juce::String octaveMessage (int baseNote)
{
    return tr ("Octave: ", "Октава: ") + octaveName (baseNote) + utf8 ("–") + octaveName (baseNote + 16);
}
inline juce::String velocityMessage (int velocity) { return tr ("Velocity: ", "Сила: ") + juce::String (velocity); }
inline const juce::String keysOffHint = tr ("Mac keyboard is off, press M", "Клавиатура Mac выключена, нажмите M");
inline juce::String stripCaption (int baseNote, int velocity)
{
    return tr ("Octave ", "Октава ") + octaveName (baseNote) + "\n" + tr ("velocity ", "сила ") + juce::String (velocity);
}
inline const juce::String noScaleHint = tr ("Project scale is off: the keys play every note in a row", "Лад проекта выключен: клавиши играют все ноты подряд");
} // namespace keyboardUi

namespace arrangement
{
inline const juce::String addMarker = tr ("+ Marker", "+ Маркер");
inline const juce::String noMarkers = tr ("No markers", "Маркеров нет");
inline const juce::String noTracks = tr ("No tracks. Create a track with Cmd+T.", "Дорожек нет. Создайте дорожку клавишами Cmd+T.");
inline const juce::String heightsUniform = tr ("Equal", "Равная");
inline const juce::String heightsByContent = tr ("By notes", "По нотам");
inline const juce::String heightsUniformMessage = tr ("Track height: equal", "Высота дорожек: одинаковая");
inline const juce::String heightsByContentMessage = tr ("Track height: by the notes of each", "Высота дорожек: по нотам каждой");
inline const juce::String snapOn = tr ("Snap to grid on", "Прилипание к сетке включено");
inline const juce::String snapOff = tr ("Snap to grid off", "Прилипание к сетке выключено");
inline const juce::String snapOffShort = tr ("no snap", "без прилипания");
inline const juce::String gridBar = tr ("1 bar", "1 такт");
inline juce::String gridBars (int n)
{
    if (! isRussian())
        return juce::String (n) + " bars";
    const bool few = n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14);
    return juce::String (n) + (few ? utf8 (" такта") : utf8 (" тактов"));
}
inline const juce::String gridBeat = tr ("beat", "доля");
inline juce::String gridBeats (int n) { return juce::String (n) + tr (" beats", " доли"); }
inline juce::String gridLabel (const juce::String& step, bool adaptive)
{
    return tr ("Grid ", "Сетка ") + step + (adaptive ? juce::String() : tr (" (fixed)", " (фикс.)"));
}
inline const juce::String followOn = tr ("Follow playhead on", "Следование за курсором включено");
inline const juce::String followOff = tr ("Follow playhead off", "Следование за курсором выключено");
inline juce::String sentToSession (int scene) { return tr ("Clip sent to scene ", "Клип отправлен в сцену ") + juce::String (scene); }

// Context menus
inline const juce::String menuOpen = tr ("Open in Editor", "Открыть в редакторе");
inline const juce::String menuRename = tr ("Rename", "Переименовать");
inline const juce::String menuDuplicate = tr ("Duplicate", "Дублировать");
inline const juce::String menuSplit = tr ("Split at Playhead", "Разрезать по курсору");
inline const juce::String menuJoin = tr ("Join Clips", "Склеить клипы");
inline const juce::String menuLoopSelection = tr ("Loop Selection", "Петля по выделению");
inline const juce::String menuSendToSession = tr ("Send to Session", "Отправить в сессию");
inline const juce::String menuNewClip = tr ("Create One-Bar Clip", "Создать клип на такт");
inline const juce::String menuPaste = tr ("Paste", "Вставить");
inline const juce::String menuSelectAll = tr ("Select All Clips", "Выделить все клипы");
inline const juce::String menuJumpToMarker = tr ("Jump to Marker", "Перейти к маркеру");
inline const juce::String menuRenameMarker = tr ("Rename Marker", "Переименовать маркер");

// Menu bar items that work in the Arrangement
inline const juce::String splitClip = tr ("Split", "Разрезать");
inline const juce::String joinClips = tr ("Join", "Склеить");
inline const juce::String loopSelection = tr ("Loop Selection", "Петля по выделению");
inline const juce::String insertTime = tr ("Insert Time", "Вставить время");
inline const juce::String removeTime = tr ("Delete Time", "Удалить время");
inline const juce::String newMarker = tr ("Add Marker at Playhead", "Маркер в позиции курсора");
inline const juce::String followPlayhead = tr ("Follow Playhead", "Следовать за курсором");
inline const juce::String backToArrangement = tr ("Back to Arrangement", "Вернуть аранжировку");

// Automation mode (task 6.7)
inline const juce::String automationMode = tr ("Automation (A)", "Автоматизация (A)");
inline const juce::String autoVolume = tr ("Volume", "Громкость");
inline const juce::String autoPan = tr ("Pan", "Панорама");
inline const juce::String autoParameter = tr ("Parameter", "Параметр");
inline const juce::String autoNoDevices = tr ("No devices", "Устройств нет");
inline const juce::String autoCurve = tr ("Curve", "Изгиб");
} // namespace arrangement

// Mixer
inline const juce::String labelPan = tr ("Pan", "Пан");
inline const juce::String labelCue = utf8 ("Cue");
inline const juce::String labelSendA = utf8 ("A");
inline const juce::String labelSendB = utf8 ("B");

// Prisma macro knobs (the eight parameters of the compact device view)
namespace macro
{
inline const juce::String wtPos = tr ("WT Morph", "Морф WT");
inline const juce::String detune = tr ("Detune", "Детюн");
inline const juce::String cutoff = tr ("Cutoff", "Срез");
inline const juce::String resonance = tr ("Resonance", "Резонанс");
inline const juce::String drive = tr ("Drive", "Драйв");
inline const juce::String attack = tr ("Attack", "Атака");
inline const juce::String lfoRate = utf8 ("LFO");
inline const juce::String width = tr ("Width", "Ширина");
} // namespace macro

// Info bar: title and description of an element under the mouse ("Name — what it does").
namespace recording
{
// Russian plural: 1 такт, 2 такта, 5 тактов
inline juce::String plural (int n, const char* one, const char* few, const char* many)
{
    const int mod100 = std::abs (n) % 100, mod10 = std::abs (n) % 10;
    const char* word = (mod100 >= 11 && mod100 <= 14) ? many : (mod10 == 1 ? one : (mod10 >= 2 && mod10 <= 4 ? few : many));
    return juce::String (n) + " " + utf8 (word);
}
inline const juce::String undoRecordClip = tr ("Record Clip", "Запись клипа");
inline const juce::String undoOverdub = utf8 ("Overdub");
inline const juce::String undoCaptureMidi = tr ("Capture MIDI", "Захват MIDI");
inline const juce::String undoRecordSource = tr ("Record Source", "Источник записи");
inline const juce::String undoDrawController = tr ("Draw Controller", "Рисование контроллера");
inline const juce::String ovr = utf8 ("OVR");
// Transport menus (docs/spec/02, tasks 2.1 and 2.3)
inline const juce::String menuVolume = tr ("Volume", "Громкость");
inline const juce::String menuOnlyRecording = tr ("Only While Recording", "Только во время записи");
inline const juce::String menuCountIn = tr ("Count-In", "Отсчёт");
inline const juce::String menuClickSound = tr ("Click Sound", "Звук клика");
inline const juce::String soundClassic = tr ("Classic", "Классический");
inline const juce::String soundWood = tr ("Wood", "Деревянный");
inline const juce::String none = tr ("None", "Нет");
inline const juce::String off = tr ("Off", "Выкл");
inline const juce::String menuFixedLength = tr ("Fixed Recording Length", "Фиксированная длина записи");
inline const juce::String menuRecordQuantise = tr ("Record Quantize", "Квантизация записи");
inline juce::String bars (int n)
{
    if (isRussian())
        return plural (n, "такт", "такта", "тактов");
    return juce::String (n) + (n == 1 ? " bar" : " bars");
}
inline juce::String volumeDb (int db)
{
    if (isRussian())
        return db <= -60 ? utf8 ("−∞") : (db == 0 ? juce::String ("0 ") : utf8 ("−") + juce::String (-db) + " ") + utf8 ("дБ");
    return db <= -60 ? utf8 ("−∞") : (db == 0 ? juce::String ("0 ") : utf8 ("−") + juce::String (-db) + " ") + "dB";
}
// "−1. 3": the count-in shows the bar and beat that are left before the recording starts
inline const juce::String recordSourceLabel = tr ("Record", "Запись");
inline const juce::String sourceMidi = utf8 ("MIDI");
inline const juce::String sourceAudio = tr ("Audio", "Звук");
inline const juce::String audioSoon = tr ("Audio recording arrives in stage 5", "Запись звука появится на этапе 5");
// Settings, tab "Recording"
inline const juce::String correction = tr ("Latency correction", "Поправка задержки");
inline const juce::String correctionReset = tr ("Reset", "Сбросить");
inline const juce::String automaticCompensation = tr ("Automatic compensation", "Автоматическая компенсация");
inline const juce::String correctionNote = tr ("Recorded notes are shifted by the audio latency plus half a buffer, the way the player hears them. If notes land slightly early or late, correct it here by hand.", "Записанные ноты сдвигаются на задержку звука и полбуфера, как слышит игрок. Если ноты ложатся в запись чуть раньше или позже, поправьте вручную.");
inline juce::String signedMs (double v)
{
    if (isRussian())
        return (v > 0.05 ? juce::String ("+") : (v < -0.05 ? utf8 ("−") : juce::String())) + juce::String (std::abs (v), 1).replace (".", ",") + utf8 (" мс");
    return (v > 0.05 ? juce::String ("+") : (v < -0.05 ? utf8 ("−") : juce::String())) + juce::String (std::abs (v), 1) + " ms";
}
inline juce::String autoMs (double v)
{
    if (isRussian())
        return juce::String (v, 1).replace (".", ",") + utf8 (" мс");
    return juce::String (v, 1) + " ms";
}
inline const juce::String recordNoTrack = tr ("Nothing to record: arm a MIDI track or select one", "Нечего записывать: взведите MIDI-дорожку или выберите её");
inline const juce::String captureNothing = tr ("Nothing to capture: play something on a MIDI keyboard", "Нечего захватывать: сыграйте что-нибудь на MIDI-клавиатуре");
inline const juce::String captureNoTrack = tr ("Select a MIDI track to capture the phrase to", "Выберите MIDI-дорожку, на которую захватить фразу");
// "Captured 8 bars, 112 notes → Arp, scene Drop"
inline juce::String captured (int bars, int notes, const juce::String& track, const juce::String& scene)
{
    if (isRussian())
    {
        return utf8 ("Захвачено ") + plural (bars, "такт", "такта", "тактов") + ", " + plural (notes, "нота", "ноты", "нот") + utf8 (" → ") + track
               + utf8 (", сцена ") + scene;
    }
    return "Captured " + juce::String (bars) + (bars == 1 ? " bar, " : " bars, ") + juce::String (notes) + (notes == 1 ? " note" : " notes")
           + utf8 (" → ") + track + ", scene " + scene;
}
// Recording of sound (docs/spec/05-audio-recording.md, task 5.4)
inline const juce::String undoRecordAudio = tr ("Record Audio", "Запись звука");
inline juce::String audioDropped (double atSeconds, double lostMs)
{
    if (isRussian())
    {
        return utf8 ("Диск не успевал за записью: ") + juce::String (lostMs, 0) + utf8 (" мс тишины на ") + juce::String (atSeconds, 1).replace (".", ",")
               + utf8 (" с от начала записи");
    }
    return "The disk could not keep up with the recording: " + juce::String (lostMs, 0) + " ms of silence at " + juce::String (atSeconds, 1)
           + " s from the start of the recording";
}
} // namespace recording

// Recording onto the Arrangement timeline, takes and taking a track from a version (docs/spec/03-arrangement.md, 3.4 and 3.5)
namespace arrRec
{
inline const juce::String undoRecord = tr ("Record to Timeline", "Запись на шкалу");
inline const juce::String undoSwitchTake = tr ("Switch Take", "Выбрать дубль");
inline const juce::String undoTakeTrack = tr ("Take Track from Version", "Забрать дорожку из версии");
inline const juce::String clipName = tr ("Recording", "Запись");
inline const juce::String punchIn = utf8 ("IN");
inline const juce::String punchOut = utf8 ("OUT");
inline const juce::String menuTakes = tr ("Takes", "Дубли");
inline juce::String take (int number)
{
    if (isRussian())
        return utf8 ("Дубль ") + juce::String (number);
    return "Take " + juce::String (number);
}
inline const juce::String punchInOn = tr ("Punch-in on: recording starts at the loop start", "Punch-in включён: запись начнётся с начала петли");
inline const juce::String punchOutOn = tr ("Punch-out on: recording stops at the loop end", "Punch-out включён: запись закончится в конце петли");
inline const juce::String punchOff = tr ("Punch off", "Punch выключен");
inline const juce::String noTrack = tr ("Nothing to record: the project has no MIDI track", "Нечего записывать: в проекте нет MIDI-дорожки");
inline juce::String recorded (int clips, int takes)
{
    if (isRussian())
    {
        auto text = utf8 ("Записано на шкалу: ") + recording::plural (clips, "клип", "клипа", "клипов");
        if (takes > 0)
            text += utf8 (", ") + recording::plural (takes, "дубль", "дубля", "дублей");
        return text;
    }
    auto text = "Recorded to timeline: " + juce::String (clips) + (clips == 1 ? " clip" : " clips");
    if (takes > 0)
        text += ", " + juce::String (takes) + (takes == 1 ? " take" : " takes");
    return text;
}
inline juce::String takeBadge (int active, int total)
{
    if (isRussian())
        return utf8 ("Д ") + juce::String (active) + "/" + juce::String (total);
    return "T " + juce::String (active) + "/" + juce::String (total);
}
inline const juce::String menuToArrangement = tr ("Record to Timeline (F9)", "Записать на шкалу (F9)");
inline const juce::String menuToSlot = tr ("Record to Session Slot", "Записать в слот сессии");
inline const juce::String commandRecord = tr ("Record to Timeline", "Запись на шкалу");
inline juce::String takeSelected (int number)
{
    if (isRussian())
        return utf8 ("Показан ") + utf8 ("дубль ") + juce::String (number);
    return "Showing take " + juce::String (number);
}
} // namespace arrRec

namespace help
{
inline juce::String make (const juce::String& title, const juce::String& text) { return title + "\n" + text; }
inline const juce::String soon3 = tr ("Arrives in stage 3", "Появится на этапе 3");
inline const juce::String soon6 = tr ("Arrives in stage 6", "Появится на этапе 6");

inline const juce::String logo = make (tr ("Prisma", "Призма"), tr ("The main DAW window. Track colors follow the spectrum from left to right.", "Главное окно DAW. Цвета дорожек идут по спектру слева направо."));
inline const juce::String tempo = make (tr ("Tempo", "Темп"), tr ("Beats per minute. Drag or scroll to change, double-click to type a number. Shift: finer.", "Ударов в минуту. Тяните мышью, колесо меняет, двойной щелчок вводит число. Shift: точнее."));
inline const juce::String signature = make (tr ("Time signature", "Размер"), tr ("The project time signature. Change it in the “Project Properties” window (Cmd+Alt+P).", "Размер такта проекта. Меняется в окне «Свойства проекта» (Cmd+Alt+P)."));
inline const juce::String metronome = make (tr ("Metronome", "Метроном"), tr ("Click to turn on the project metronome. Right-click: volume, “Only While Recording”, count-in, click sound.", "Щелчок включает метроном проекта. Правая кнопка: громкость, «Только во время записи», отсчёт, звук клика."));
inline const juce::String play = make (tr ("Play", "Играть"), tr ("Starts and stops playback. Space does the same.", "Запускает и останавливает воспроизведение. Пробел делает то же самое."));
inline const juce::String stop = make (tr ("Stop", "Стоп"), tr ("Stops playback and all clips. Press again to return to the start.", "Останавливает воспроизведение и все клипы. Повторное нажатие возвращает в начало."));
inline const juce::String record = make (tr ("Record", "Запись"), tr ("In Arrangement, and in Session View while the transport plays: records to the timeline (armed tracks record notes, Session clip launches are recorded as clips). In Session View with the transport stopped: records into an empty slot of the selected scene. Click again to stop. Right-click: where to record, fixed length, quantize. F9 always records to the timeline.", "В Arrangement, а также в Session View при играющем транспорте: запись на шкалу (взведённые дорожки пишут ноты, запуски клипов сессии записываются как клипы). В Session View при остановленном транспорте: запись в пустой слот выбранной сцены. Повторный щелчок останавливает. Правая кнопка: куда писать, фиксированная длина, квантизация. F9 всегда пишет на шкалу."));
inline const juce::String punchIn = make (utf8 ("Punch-in"), tr ("Recording to the timeline starts only at the start of the loop brace. Drag the brace above the ruler.", "Запись на шкалу начинается только с начала скобки петли. Скобку тянут над линейкой."));
inline const juce::String punchOut = make (utf8 ("Punch-out"), tr ("Recording to the timeline stops at the end of the loop brace while playback goes on. Together with Punch-in, recording happens only inside the brace.", "Запись на шкалу заканчивается в конце скобки петли, воспроизведение идёт дальше. Вместе с Punch-in запись идёт только внутри скобки."));
inline const juce::String overdub = make (utf8 ("Overdub"), tr ("On: recording into a playing clip layers new notes without erasing the old ones. Each pass can be undone separately. On the timeline, notes go into the clips beneath them instead of replacing them.", "Включён: запись на играющий клип добавляет ноты слоями, старые не стираются. Каждый круг отменяется отдельно. На шкале ноты попадают в клипы под ними вместо замены."));
inline const juce::String position = make (tr ("Position", "Позиция"), tr ("Bars, beats and sixteenths from the start of playback.", "Такты, доли и шестнадцатые от начала воспроизведения."));
inline const juce::String loop = make (tr ("Arrangement loop", "Петля аранжировки"), tr ("Turns the Arrangement loop on and off. Drag the brace above the ruler to set its bounds; Cmd+L sets the loop to the selection.", "Включает и выключает петлю на шкале Arrangement. Границы петли тянутся по скобке над линейкой, Cmd+L ставит петлю по выделению."));
inline const juce::String followPlayhead = make (tr ("Follow playhead", "Следовать за курсором"), tr ("When lit, the timeline view pages along with the playhead (Cmd+Shift+F).", "Когда горит, вид шкалы перелистывается вслед за курсором воспроизведения (Cmd+Shift+F)."));
inline const juce::String backToArrangement = make (tr ("Back to Arrangement", "Вернуть аранжировку"), tr ("Lit when at least one track plays Session clips instead of the timeline. Press to stop Session clips at the next quantize boundary and return the tracks to the arrangement.", "Горит, когда хотя бы одна дорожка играет клипы Session вместо шкалы. Нажатие останавливает клипы сессии на ближайшей границе квантизации и возвращает дорожки к аранжировке."));
inline const juce::String ribbon = make (tr ("“10 min” ribbon", "Лента «10 мин»"), tr ("Always-on recording: the last 10 minutes of playing. Press “Capture” or Cmd+Shift+C", "Вечная запись: последние 10 минут игры. Нажмите “Захватить” или Cmd+Shift+C"));
inline const juce::String recordSlot = make (tr ("Record button", "Кружок записи"), tr ("Click to start recording into this slot at the next launch boundary. Needs an armed (or selected) MIDI track.", "Щелчок начинает запись в этот слот на ближайшей границе запуска. Нужна взведённая (или выбранная) MIDI-дорожка."));
inline const juce::String recordingSlot = make (tr ("Recording", "Идёт запись"), tr ("Click to stop recording at the next bar. The clip loops right away. Cmd+Z deletes the recorded clip.", "Щелчок заканчивает запись на ближайшем такте. Клип сразу играет по кругу. Cmd+Z удаляет записанный клип."));
inline const juce::String trackRecordSource = make (tr ("Record source", "Источник записи"), tr ("What this track records: MIDI from a keyboard or audio from an input.", "Что пишет эта дорожка: MIDI с клавиатуры или звук с входа."));
inline const juce::String trackRecordAudio = make (tr ("Audio recording", "Запись звука"), tr ("Audio recording arrives in stage 5", "Запись звука появится на этапе 5"));
inline const juce::String controllerLane = make (tr ("Controller lane", "Полоса контроллера"), tr ("Drag to draw values: events of this type under the stroke are replaced. One Cmd+Z undoes the whole stroke.", "Тяните мышью, чтобы нарисовать значения: события этого типа под мазком заменяются. Весь мазок отменяется одним Cmd+Z."));
inline const juce::String morph = make (tr ("Morph A ↔ B", "Морф A ↔ B"), tr ("Smooth transition between knob snapshots. ", "Плавный переход между снимками ручек. ") + soon6);
inline const juce::String tabSession = make (utf8 ("Session"), tr ("A grid of clips for live playing. Tab switches between Session and Arrangement.", "Сетка клипов для живой игры. Tab переключает Session и Arrangement."));
inline const juce::String tabArrangement = make (utf8 ("Arrangement"), tr ("A linear timeline: clips by bar, markers, loop. Drag a clip from Session here. Tab switches between Session and Arrangement.", "Линейная шкала времени: клипы по тактам, маркеры, петля. Клип из Session можно перетащить сюда. Tab переключает Session и Arrangement."));
inline const juce::String version = make (tr ("Version history", "История версий"), tr ("Version number and time since the last save. Click to open the list: labels, return to a version.", "Номер версии и время с последнего сохранения. Щелчок открывает список: метки, возврат к версии."));
inline const juce::String cpu = make (tr ("CPU load", "Нагрузка процессора"), tr ("Click to open audio settings.", "Щелчок открывает настройки звука."));
inline const juce::String midiIn = make (tr ("MIDI input", "MIDI-вход"), tr ("Blinks when events arrive from MIDI devices.", "Мигает, когда приходят события с MIDI-устройств."));
inline const juce::String keysIndicator = make (tr ("Computer keyboard", "Клавиатура компьютера"), tr ("When lit, letter keys play notes; next to it is the octave of the A key. Toggle with M. Z and X shift the octave, C and V change velocity.", "Когда горит, буквенные клавиши играют ноты; рядом октава клавиши A. Включается клавишей M. Z и X сдвигают октаву, C и V меняют силу."));

inline const juce::String browserPanel = make (tr ("Browser", "Браузер"), tr ("Instruments, presets, effects, chains and sounds. Drag items into the project.", "Инструменты, пресеты, эффекты, цепочки и звуки. Тяните элементы в проект."));
inline const juce::String browserPreview = make (tr ("Preview", "Предпрослушивание"), tr ("When on, the selected Prisma preset plays a phrase at the project tempo. Third-party plugins have no preview.", "Когда включено, выбранный пресет Prisma играет фразу в темпе проекта. Для сторонних плагинов предпрослушивания нет."));
inline const juce::String browserItem = tr ("Drag onto a track, into a chain or onto empty space. Double-click to add to the selected track, right-click for the menu.", "Тяните на дорожку, в цепочку или на пустое место. Двойной щелчок ставит на выбранную дорожку, правая кнопка открывает меню.");
inline const juce::String browserAddFolder = tr ("Adds a folder of sounds and MIDI files to “Places”.", "Добавляет папку со звуками и MIDI-файлами в «Места».");
inline const juce::String browserSearch = make (tr ("Search", "Поиск"), tr ("Searches all categories at once. Cmd+F moves the cursor here, Esc clears.", "Ищет по всем категориям сразу. Cmd+F переводит сюда курсор, Esc очищает."));
inline juce::String browserCategory (const juce::String& name) { return make (name, tr ("Browser section: click to show its contents.", "Раздел браузера: щелчок показывает его содержимое.")); }

inline const juce::String trackHeader = make (tr ("Track header", "Заголовок дорожки"), tr ("Click to select the track, double-click to rename, right-click for the menu. Drag the right edge to change the column width.", "Щелчок выделяет дорожку, двойной щелчок переименовывает, правая кнопка открывает меню. Правый край тянется, меняя ширину столбца."));
inline const juce::String clipSlot = make (tr ("Clip", "Клип"), tr ("Click to select the clip, the triangle on the left launches it. Double-click an empty slot to create a clip. Drag a clip to another slot (Alt: copy).", "Щелчок выбирает клип, треугольник слева запускает. Двойной щелчок по пустому слоту создаёт клип. Клип можно перетащить в другой слот (Alt: копия)."));
inline const juce::String recordAudioSlot = make (tr ("Audio record button", "Кружок записи звука"), tr ("Click to record the track input into this slot from the next launch boundary. Choose the input and monitoring in the track card below.", "Щелчок записывает вход дорожки в этот слот с ближайшей границы запуска. Вход и мониторинг выбираются в карточке дорожки внизу."));
inline const juce::String emptyAudioSlot = make (tr ("Empty audio track slot", "Пустой слот аудиодорожки"), tr ("Drop an audio file here from the browser or Finder. Hold Cmd when dropping several files to put each on its own track.", "Перетащите сюда звуковой файл из браузера или из Finder. Cmd при отпускании нескольких файлов кладёт каждый на свою дорожку."));
inline const juce::String emptySlot =make (tr ("Empty slot", "Пустой слот"), tr ("Click to stop the track, double-click to create an empty one-bar MIDI clip.", "Щелчок останавливает дорожку, двойной щелчок создаёт пустой MIDI-клип на такт."));
inline const juce::String sceneLaunch = make (tr ("Scene launch", "Запуск сцены"), tr ("Launches all clips of this scene. Double-click the name to rename.", "Запускает все клипы этой сцены. Двойной щелчок по названию переименовывает."));
inline const juce::String sceneName = make (tr ("Scene", "Сцена"), tr ("Click to select the scene, double-click to rename, right-click for the menu.", "Щелчок выделяет сцену, двойной щелчок переименовывает, правая кнопка открывает меню."));
inline const juce::String stopAllClips = make (tr ("Stop all clips", "Остановить все клипы"), tr ("Stops the clips of all tracks at the next quantize boundary.", "Останавливает клипы всех дорожек на ближайшей границе квантизации."));
inline const juce::String masterHeader = make (tr ("Master", "Мастер"), tr ("The project’s main output. The scene launch buttons are to the right of the clips.", "Общий выход проекта. Справа от клипов находятся кнопки запуска сцен."));
inline const juce::String fader = make (tr ("Volume", "Громкость"), tr ("Drag the fader or scroll to change, double-click to reset to 0 dB. Shift: finer.", "Тяните фейдер, колесо меняет, двойной щелчок сбрасывает в 0 дБ. Shift: точнее."));
inline const juce::String volumeReadout = make (tr ("Volume in dB", "Громкость в дБ"), tr ("Double-click to type a number: Enter confirms, Esc cancels.", "Двойной щелчок открывает ввод числа: Enter подтверждает, Esc отменяет."));
inline const juce::String panKnob = make (tr ("Pan", "Панорама"), tr ("Position in the stereo field. Double-click to center, Shift while turning: finer.", "Положение в стерео. Двойной щелчок возвращает в центр, Shift при вращении: точнее."));
inline const juce::String audioFollowTempo = make (tr ("Follow project tempo", "Под темп проекта"), tr ("A loop with a known tempo plays at the project tempo: faster or slower, with pitch changing along with speed.", "Луп с известным темпом играет в темпе проекта: быстрее или медленнее, высота меняется вместе со скоростью."));
inline const juce::String spectrum = make (tr ("Master spectrum", "Спектр мастера"), tr ("What plays at the output: from bass on the left to air on the right, tilted +4.5 dB per octave the way the ear hears. Ticks show recent peaks.", "Что звучит на выходе: от баса слева до воздуха справа, наклон +4,5 дБ на октаву, как слышит ухо. Чёрточки показывают недавние пики."));
inline const juce::String sendKnob = make (tr ("Send", "Посыл"), tr ("Sends part of the post-fader signal to return track A (reverb) or B (delay). The first touch creates the return track if there is none. Double-click to turn off.", "Отправляет часть звука после фейдера на возвратную дорожку A (ревер) или B (дилей). Первое касание создаёт возвратную дорожку, если её нет. Двойной щелчок выключает."));
inline const juce::String cueKnob = make (tr ("Cue", "Прослушивание"), tr ("Preview volume. ", "Громкость предпрослушивания. ") + soon6);
inline const juce::String meter = make (tr ("Level", "Уровень"), tr ("Peak level of the track signal.", "Пиковый уровень сигнала дорожки."));
inline const juce::String activator = make (tr ("Track on", "Дорожка включена"), tr ("Shows the track number. A track that is off is silent.", "Показывает номер дорожки. Выключенная дорожка молчит."));
inline const juce::String solo = make (utf8 ("Solo"), tr ("Hear only this track. Cmd-click adds to the ones already on.", "Слушать только эту дорожку. Cmd-щелчок добавляет к уже включённым."));
inline const juce::String arm = make (tr ("Track arm", "Запись дорожки"), tr ("MIDI keyboards and the computer keyboard play on this track. Cmd-click adds to the ones already on.", "MIDI-клавиатуры и клавиши компьютера играют на этой дорожке. Cmd-щелчок добавляет к уже включённым."));
inline const juce::String emptyProject = make (tr ("Empty project", "Пустой проект"), tr ("Create a track from the “Create” menu or with Cmd+T.", "Создайте дорожку из меню «Создать» или клавишей Cmd+T."));
inline const juce::String splitterBrowser = make (tr ("Browser edge", "Граница браузера"), tr ("Drag to change the width. The triangle hides and shows the browser (Cmd+Alt+B).", "Тяните, чтобы изменить ширину. Треугольник прячет и показывает браузер (Cmd+Alt+B)."));
inline const juce::String splitterDetail = make (tr ("Detail panel edge", "Граница детальной панели"), tr ("Drag to change the height. The triangle hides and shows the panel (Cmd+Alt+L).", "Тяните, чтобы изменить высоту. Треугольник прячет и показывает панель (Cmd+Alt+L)."));
inline const juce::String splitterMixer = make (tr ("Mixer", "Микшер"), tr ("The triangle hides and shows the mixer (Cmd+Alt+M).", "Треугольник прячет и показывает микшер (Cmd+Alt+M)."));
inline const juce::String hscroll = make (tr ("Scroll", "Прокрутка"), tr ("Scrolls the track columns.", "Прокручивает столбцы дорожек."));
inline const juce::String vscroll = make (tr ("Scroll", "Прокрутка"), tr ("Scrolls the scenes.", "Прокручивает сцены."));

inline const juce::String devicePower = make (tr ("Device on/off", "Выключатель устройства"), tr ("A device that is off passes audio straight through.", "Выключенное устройство пропускает звук насквозь."));
inline const juce::String devicePresets = make (tr ("Device presets", "Пресеты устройства"), tr ("Presets of this device and saving your settings to the library.", "Пресеты этого устройства и сохранение ваших настроек в библиотеку."));
inline const juce::String devicePresetsInstrument = make (tr ("Instrument presets", "Пресеты инструмента"), tr ("Instrument presets, replacing the instrument and saving your settings to the library.", "Пресеты инструмента, замена инструмента и сохранение ваших настроек в библиотеку."));
inline const juce::String deviceFold = make (tr ("Fold", "Свернуть"), tr ("Folds the device into a narrow strip.", "Сворачивает устройство в узкую полосу."));
inline const juce::String deviceOpen = make (tr ("Open in window", "Открыть в окне"), tr ("The full device editor in a separate window.", "Полный редактор устройства в отдельном окне."));
inline const juce::String deviceClose = make (tr ("Delete device", "Удалить устройство"), tr ("Removes the device from the chain. Cmd+Z brings it back.", "Убирает устройство из цепочки. Cmd+Z возвращает."));
inline const juce::String deviceHeader = make (tr ("Device header", "Заголовок устройства"), tr ("Drag to move the device. Delete removes the selected one.", "Тяните, чтобы переставить устройство. Delete удаляет выделенное."));
inline const juce::String deviceKnob = make (tr ("Device parameter", "Параметр устройства"), tr ("Drag or scroll to change, double-click to restore the default. Shift: finer.", "Тяните, колесо меняет, двойной щелчок возвращает значение по умолчанию. Shift: точнее."));
inline const juce::String wavetable = make (utf8 ("Wavetable"), tr ("The waveform of the selected table and the position within it.", "Форма волны выбранной таблицы и положение внутри неё."));
inline const juce::String missingDevice = make (tr ("Plugin not found", "Плагин не найден"), tr ("Its state is saved and comes back when the plugin is available. Audio passes straight through.", "Состояние сохранено и вернётся, когда плагин появится. Звук идёт насквозь."));
inline const juce::String dropZone = make (tr ("End of chain", "Конец цепочки"), tr ("Later you will be able to drop an effect or plugin here from the browser.", "Сюда можно будет перетащить эффект или плагин из браузера."));
inline const juce::String detailInfo = make (tr ("Selected track", "Выбранная дорожка"), tr ("The track’s input, monitoring and number of devices.", "Вход, мониторинг и число устройств дорожки."));
// Clip editor, scale, tuning and the stage 2 windows
inline const juce::String scaleButton = make (tr ("Project scale", "Лад проекта"), tr ("The project root and scale. New clips take it; the computer keyboard and the clip editor highlight its notes.", "Тоника и лад проекта. Новые клипы берут его, клавиатура компьютера и редактор клипа подсвечивают его ноты."));
inline const juce::String editorProps = make (tr ("Clip properties", "Свойства клипа"), tr ("Loop, grid, scale and transpose of the selected clip.", "Петля, сетка, лад и транспонирование выбранного клипа."));
inline const juce::String editorName = make (tr ("Clip name", "Название клипа"), tr ("Double-click to rename. The square on the right changes the color.", "Двойной щелчок переименовывает. Квадрат справа меняет цвет."));
inline const juce::String editorLoop = make (tr ("Loop", "Петля"), tr ("On: the clip loops. Off: it plays once from start to end. Cmd+L sets the loop around the selected notes.", "Включена: клип играет по кругу. Выключена: один раз от старта до конца. Cmd+L ставит петлю вокруг выделенных нот."));
inline const juce::String editorLoopStart = make (tr ("Loop start", "Начало петли"), tr ("In beats from the clip start. Drag the brace on the ruler.", "В долях от начала клипа. Скобка на линейке тянется мышью."));
inline const juce::String editorLoopEnd = make (tr ("Clip end", "Конец клипа"), tr ("In beats. A note dragged past this edge extends the clip by whole bars.", "В долях. Нота, перетянутая за этот край, удлиняет клип на целые такты."));
inline const juce::String editorStart = make (tr ("Start point", "Точка старта"), tr ("Where the clip starts playing when launched. Drag the triangle on the ruler.", "Откуда клип начинает играть при запуске. Треугольник на линейке тянется мышью."));
inline const juce::String editorGrid = make (tr ("Grid", "Сетка"), tr ("Grid step and new note length. Cmd+1 finer, Cmd+2 coarser, Cmd+3 triplets.", "Шаг сетки и длина новой ноты. Cmd+1 мельче, Cmd+2 крупнее, Cmd+3 триоли."));
inline const juce::String editorSnap = make (tr ("Snap", "Прилипание"), tr ("Notes snap to the grid while dragging. Hold Cmd while dragging to turn it off. Cmd+4.", "Ноты прилипают к сетке при перетаскивании. Cmd во время перетаскивания отключает. Cmd+4."));
inline const juce::String editorScale = make (tr ("Clip scale", "Лад клипа"), tr ("Choose the root and scale. Rows outside the scale are dimmed, the root is highlighted.", "Выбор тоники и лада. Строки вне лада затемнены, тоника подсвечена."));
inline const juce::String editorOwnScale = make (tr ("Own scale", "Свой лад"), tr ("Off: the clip shows the project scale and follows its changes.", "Выключено: клип показывает лад проекта и меняется вместе с ним."));
inline const juce::String editorScaleOnly = make (tr ("Scale only", "Только лад"), tr ("New notes and pitch dragging land only on scale notes.", "Новые ноты и перетаскивание по высоте попадают только на ноты лада."));
inline const juce::String editorFold = make (utf8 ("Fold"), tr ("Shows only rows that have notes (or scale rows if there are no notes).", "Показывает только строки, где есть ноты (или строки лада, если нот нет)."));
inline const juce::String editorQuantise = make (tr ("Quantize", "Квантизация"), tr ("Cmd+U quantizes the selected notes with the current settings, Cmd+Shift+U opens the settings window.", "Cmd+U квантует выделенные ноты по текущим настройкам, Cmd+Shift+U открывает окно настроек."));
inline const juce::String editorTranspose = make (tr ("Transpose", "Транспонирование"), tr ("Shifts the selected notes, or all of them. With a scale, ±1 moves by scale degrees; ±12 is always an octave. Cmd+Shift+arrows.", "Сдвигает выделенные ноты или все. С ладом ±1 идёт по ступеням лада, ±12 всегда на октаву. Cmd+Shift+стрелки."));
inline const juce::String editorHalveTime = make (tr ("÷2: twice as fast", "÷2: вдвое быстрее"), tr ("Halves the clip: notes, loop and length get shorter, and the clip plays twice as fast.", "Сжимает клип вдвое: ноты, петля и длина становятся короче, клип играет в два раза быстрее."));
inline const juce::String editorDoubleTime = make (tr ("×2: twice as slow", "×2: вдвое медленнее"), tr ("Doubles the clip: notes, loop and length get longer, and the clip plays twice as slow.", "Растягивает клип вдвое: ноты, петля и длина становятся длиннее, клип играет в два раза медленнее."));
inline const juce::String editorPencil = make (tr ("Pencil", "Карандаш"), tr ("Click to draw a note, click a note to delete it. Key B.", "Щелчок рисует ноту, щелчок по ноте удаляет. Клавиша B."));
inline const juce::String editorPreview = make (tr ("Preview", "Прослушивание"), tr ("A new, selected or dragged note plays briefly.", "Новая, выделенная и перетаскиваемая нота звучит коротко."));
inline const juce::String editorRuler = make (tr ("Ruler", "Линейка"), tr ("Loop brace: drag its edges and middle. Triangle: start point. Click to place the insert marker.", "Скобка петли: тяните края и середину. Треугольник: точка старта. Щелчок ставит курсор вставки."));
inline const juce::String editorKeys = make (tr ("Keys", "Клавиши"), tr ("Click to play the note on the track and select all notes of that pitch.", "Щелчок играет ноту на дорожке и выделяет все ноты этой высоты."));
inline const juce::String editorGridArea = make (tr ("Note grid", "Сетка нот"), tr ("Double-click to draw or delete a note, drag a frame to select, drag a note’s right edge to change its length, Alt copies, Cmd turns off snap. Cmd+scroll: zoom, Alt+scroll: row height.", "Двойной щелчок рисует или удаляет ноту, рамка выделяет, правый край ноты меняет длину, Alt копирует, Cmd отключает прилипание. Cmd+колесо: масштаб, Alt+колесо: высота строк."));
inline const juce::String editorVelocity = make (tr ("Velocity", "Сила нажатия"), tr ("Drag a bar. Alt+drag across several bars draws a ramp.", "Тяните столбик. Alt+тянуть по нескольким столбикам рисует рампу."));
inline const juce::String editorControllers = make (tr ("Controllers", "Контроллеры"), tr ("Mod wheel, pitch bend and sustain. The lane can be folded.", "Колесо модуляции, питч-бенд и сустейн. Полоса сворачивается."));
inline const juce::String arrangementView = make (utf8 ("Arrangement"), tr ("The project timeline. Space plays from the start point, Shift+Space continues from where it stopped. Tab returns to Session.", "Шкала времени проекта. Пробел играет с точки старта, Shift+Пробел продолжает с места остановки. Tab возвращает в Session."));
inline const juce::String arrAutomationLanes = make (tr ("Envelope", "Огибающая"), tr ("Click the line to add a point, drag to move it (Alt or Cmd: no grid snap). Alt+drag a segment: curve. Double-click a point to delete it; Delete removes the points in the selected time. A: leave automation mode.", "Щелчок по линии добавляет точку, перетаскивание двигает её (Alt или Cmd: без привязки к сетке). Alt+протяжка по отрезку: изгиб. Двойной щелчок по точке удаляет её, Delete при выделенном времени удаляет точки в нём. A: выйти из режима автоматизации."));
inline const juce::String arrAutomationChooser = make (tr ("What to automate", "Что автоматизировать"), tr ("Volume, pan or a device parameter of this track. Parameters that already have an envelope are marked with a dot.", "Громкость, панорама или параметр устройства этой дорожки. Дорожки с готовой огибающей отмечены точкой."));
inline const juce::String arrLanes = make (tr ("Tracks", "Дорожки"), tr ("Drag on empty space to select time, double-click to draw a one-bar clip. Cmd+scroll: zoom, Alt+scroll: track height, Shift+scroll: scroll in time.", "Протяжка по пустому месту выделяет время, двойной щелчок рисует клип на такт. Cmd+колесо: масштаб, Alt+колесо: высота дорожки, Shift+колесо: прокрутка по времени."));
inline const juce::String arrClip = make (tr ("Timeline clip", "Клип на шкале"), tr ("Drag to move (Alt: copy, Cmd: no snap). Double-click to open the note editor, right-click for the menu. Cmd+E splits, Cmd+D duplicates.", "Тяните, чтобы переместить (Alt: копия, Cmd: без прилипания). Двойной щелчок открывает редактор нот, правая кнопка открывает меню. Cmd+E разрезает, Cmd+D дублирует."));
inline const juce::String arrClipLeftEdge = make (tr ("Clip left edge", "Левый край клипа"), tr ("Drag to trim the start: the content stays in place.", "Тяните, чтобы обрезать начало: содержимое остаётся на месте."));
inline const juce::String arrClipRightEdge = make (tr ("Clip right edge", "Правый край клипа"), tr ("Drag to change the length: if the clip is longer than its loop, the content repeats.", "Тяните, чтобы изменить длину: если клип длиннее петли, содержимое повторяется."));
inline const juce::String arrRuler = make (tr ("Bar ruler", "Линейка тактов"), tr ("Click to set the start point and jump there (during playback at the quantize boundary, with Shift at once). Drag to select time on all tracks.", "Щелчок ставит точку старта и перематывает (во время игры на границе квантизации, с Shift сразу). Протяжка выделяет время на всех дорожках."));
inline const juce::String arrBrace = make (tr ("Loop brace", "Скобка петли"), tr ("Click to turn the loop on and off. Drag the edges and middle; Cmd+L sets the loop to the selection.", "Щелчок включает и выключает петлю. Края и середину можно тянуть, Cmd+L ставит петлю по выделению."));
inline const juce::String arrMarkerLane = make (tr ("Markers", "Маркеры"), tr ("Cmd+M adds a marker at the cursor. Click a marker to jump to it, double-click to rename, Delete removes it.", "Cmd+M ставит маркер в позиции курсора. Щелчок по маркеру переходит к нему, двойной щелчок переименовывает, Delete удаляет."));
inline const juce::String arrMarker = make (tr ("Marker", "Маркер"), tr ("Click to jump to the marker, drag to move, double-click to rename, right-click for color and delete.", "Щелчок переходит к маркеру, перетаскивание двигает, двойной щелчок переименовывает, правая кнопка открывает цвет и удаление."));
inline const juce::String arrHeader = make (tr ("Track header", "Заголовок дорожки"), tr ("The same tracks as in Session. Click to select, double-click the name to rename. Drag the bottom edge to change the track height.", "Те же дорожки, что в Session. Щелчок выделяет, двойной щелчок по имени переименовывает. Нижний край тянется, меняя высоту дорожки."));
inline const juce::String arrHeaderEdge = make (tr ("Track height", "Высота дорожки"), tr ("Drag up and down: 24 to 240 pixels. Key H switches between “same height” and “fit notes”.", "Тяните вверх и вниз: от 24 до 240 пикселей. Клавиша H переключает «одинаковая высота» и «по нотам»."));
inline const juce::String arrHeaderVolume = make (tr ("Track volume", "Громкость дорожки"), tr ("Drag up and down, double-click to type a number in dB. Shift: finer.", "Тяните вверх и вниз, двойной щелчок вводит число в дБ. Shift: точнее."));
inline const juce::String arrAddMarker = make (tr ("New marker", "Новый маркер"), tr ("Adds a marker at the cursor (Cmd+M).", "Ставит маркер в позиции курсора (Cmd+M)."));
inline const juce::String arrPrevMarker = make (tr ("Previous marker", "Предыдущий маркер"), tr ("Jumps to the marker left of the cursor.", "Переходит к маркеру левее курсора."));
inline const juce::String arrNextMarker = make (tr ("Next marker", "Следующий маркер"), tr ("Jumps to the marker right of the cursor.", "Переходит к маркеру правее курсора."));
inline const juce::String arrMarkerList = make (tr ("Marker list", "Список маркеров"), tr ("Quick jump to any marker.", "Быстрый переход к любому маркеру."));
inline const juce::String arrHeightMode = make (tr ("Track heights", "Высота дорожек"), tr ("The same for all, or fit to each track’s notes. Key H.", "Одинаковая для всех или по нотам каждой дорожки. Клавиша H."));
inline const juce::String arrGridLabel = make (tr ("Grid", "Сетка"), tr ("Grid step: adaptive to zoom or fixed. Cmd+1 finer, Cmd+2 coarser, Cmd+3 triplets, Cmd+4 snap.", "Шаг сетки: сам по масштабу или фиксированный. Cmd+1 мельче, Cmd+2 крупнее, Cmd+3 триоли, Cmd+4 прилипание."));
inline const juce::String arrCorner = make (tr ("Markers", "Маркеры"), tr ("Buttons to add a marker and jump between markers.", "Кнопки ставят маркер и переходят между маркерами."));
inline const juce::String arrMinimap = make (tr ("Song overview", "Обзор песни"), tr ("The whole song in miniature. Drag the frame, drag its edges to zoom, click beside it to move the view.", "Вся песня мелко. Рамку можно тянуть, её края меняют масштаб, щелчок рядом переносит вид."));
inline const juce::String keyboardStripHelp = make (tr ("On-screen keyboard", "Экранная клавиатура"), tr ("Plays on the selected track. Cmd+Alt+K hides the strip.", "Играет на выбранной дорожке. Cmd+Alt+K прячет полосу."));
inline const juce::String keyboardInScale = make (tr ("In scale", "В ладу"), tr ("Keys A S D F G H J K L ; play consecutive degrees of the project scale. External MIDI keyboards are not affected.", "Клавиши A S D F G H J K L ; играют подряд ступени лада проекта. Внешней MIDI-клавиатуры это не касается."));
inline const juce::String keyboardChords = make (tr ("Chords", "Аккорды"), tr ("One key plays a triad of the project scale (needs the scale on and “In scale” mode).", "Одна клавиша играет трезвучие лада проекта (нужен включённый лад и режим «В ладу»)."));
inline const juce::String trackTranspose = make (tr ("Track transpose", "Транспонирование дорожки"), tr ("Shifts everything going into the track’s instrument, live playing included, by −48…+48 semitones. Clip notes are not changed.", "Сдвигает всё, что идёт в инструмент дорожки, включая живую игру, на −48…+48 полутонов. Ноты клипов не меняются."));
inline const juce::String projectTempoField = make (tr ("Tempo", "Темп"), tr ("Project tempo in beats per minute.", "Темп проекта в ударах в минуту."));
inline const juce::String projectSignatureField = make (tr ("Time signature", "Размер"), tr ("The project time signature.", "Размер такта проекта."));
inline const juce::String projectTuning = make (tr ("A4 tuning", "Строй A4"), tr ("Frequency of the note A4, 415…466 Hz. Built-in synths only; third-party plugins set their tuning inside the plugin. Double-click to reset to 440.", "Частота ноты ля первой октавы, 415…466 Гц. Встроенные синты. У сторонних плагинов строй настраивается в самом плагине. Двойной щелчок возвращает 440."));
inline const juce::String projectAuthor = make (tr ("Author", "Автор"), tr ("The project author’s name.", "Имя автора проекта."));
inline const juce::String projectNotes = make (tr ("Notes", "Заметки"), tr ("Free text, saved with the project.", "Свободный текст, сохраняется вместе с проектом."));
inline const juce::String infoBar = make (tr ("Info bar", "Строка подсказок"), tr ("Shows the name and action of the element under the mouse.", "Показывает название и действие элемента под мышью."));
} // namespace help

// The audio clip editor and audio clips on the timeline (docs/spec/05-audio-recording.md, tasks 5.1, 5.2, 5.6).
namespace audioUi
{
inline const juce::String fileMissing = tr ("File not found", "Файл не найден");
inline const juce::String file = tr ("File", "Файл");
inline const juce::String gain = tr ("Gain", "Громкость");
inline const juce::String transpose = tr ("Transpose", "Транспонирование");
inline const juce::String detune = tr ("Detune", "Подстройка");
inline const juce::String reverse = tr ("Reverse", "Обратно");
inline const juce::String loop = tr ("Loop", "Петля");
inline const juce::String fadeIn = tr ("Fade in (timeline)", "Фейд-ин (шкала)");
inline const juce::String fadeOut = tr ("Fade out (timeline)", "Фейд-аут (шкала)");
inline const juce::String warpSoon = tr ("Stretching without pitch change: stage 7", "Растяжение без смены высоты: этап 7");
inline juce::String followTempo (double bpm)
{
    if (isRussian())
        return utf8 ("Под темп проекта (файл ") + juce::String (bpm, 1).replace (".", ",") + ")";
    return "Follow project tempo (file " + juce::String (bpm, 1) + ")";
}
inline const juce::String tempoUnknown = tr ("File tempo unknown", "Темп файла неизвестен");
inline const juce::String undoFollowTempo = tr ("Follow Tempo", "Подстройка под темп");
inline const juce::String loopStart = tr ("Loop start", "Начало петли");
inline const juce::String loopEnd = tr ("End", "Конец");
inline const juce::String startPoint = tr ("Start", "Старт");
inline const juce::String grid = tr ("Grid", "Сетка");
inline const juce::String snap = tr ("Snap", "Прилипание");
inline const juce::String noFile = tr ("File not found: the audio stays silent until the file is back in place.", "Файл не найден: звук не играет, пока файл не вернётся на место.");
inline const juce::String undoGain = tr ("Audio Gain", "Громкость звука");
inline const juce::String undoTranspose = tr ("Transpose Audio", "Транспонирование звука");
inline const juce::String undoDetune = tr ("Detune Audio", "Подстройка звука");
inline const juce::String undoReverse = tr ("Reverse Audio", "Обратное воспроизведение");
inline const juce::String undoFadeIn = tr ("Clip Fade In", "Фейд-ин клипа");
inline const juce::String undoFadeOut = tr ("Clip Fade Out", "Фейд-аут клипа");
inline const juce::String undoLoopBrace = tr ("Clip Loop", "Петля клипа");
inline const juce::String undoStartMarker = tr ("Clip Start Point", "Точка старта клипа");
inline juce::String fileInfo (const juce::String& name, double rate, int channels, double seconds)
{
    if (isRussian())
    {
        return name + utf8 (" · ") + juce::String (rate / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".").replace (".", ",") + utf8 (" кГц · ")
               + (channels == 1 ? utf8 ("моно") : channels == 2 ? utf8 ("стерео") : juce::String (channels) + utf8 (" кан.")) + utf8 (" · ")
               + juce::String (seconds, 2).replace (".", ",") + utf8 (" с");
    }
    return name + utf8 (" · ") + juce::String (rate / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + utf8 (" kHz · ")
           + (channels == 1 ? juce::String ("mono") : channels == 2 ? juce::String ("stereo") : juce::String (channels) + " ch.") + utf8 (" · ")
           + juce::String (seconds, 2) + " s";
}
inline juce::String rateWarning (double rate)
{
    if (isRussian())
    {
        return utf8 ("Частота файла ") + juce::String (rate / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".").replace (".", ",")
               + utf8 (" кГц: пересчитывается при воспроизведении");
    }
    return "File sample rate " + juce::String (rate / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
           + " kHz: resampled during playback";
}
inline const juce::String dropNewTracks = tr ("New audio tracks", "Новые аудиодорожки");
} // namespace audioUi

namespace help
{
inline const juce::String audioProps = make (tr ("Audio clip properties", "Свойства звукового клипа"), tr ("Gain, pitch, reverse, loop and fades of the clip.", "Громкость, высота, обратное воспроизведение, петля и фейды клипа."));
inline const juce::String audioGain = make (tr ("Clip gain", "Громкость клипа"), tr ("From -70 to +24 dB. Drag to change, double-click to type a number.", "От -70 до +24 дБ. Тяните мышью, двойной щелчок вводит число."));
inline const juce::String audioTranspose = make (tr ("Transpose", "Транспонирование"), tr ("Semitones, from -48 to +48. Changes both pitch and length of the audio (when not following tempo).", "Полутоны, от -48 до +48. Меняет и высоту, и длительность звука (без подстройки под темп)."));
inline const juce::String audioDetune = make (tr ("Detune", "Подстройка"), tr ("Cents, from -50 to +50.", "Центы, от -50 до +50."));
inline const juce::String audioReverse = make (tr ("Reverse", "Обратное воспроизведение"), tr ("The clip audio plays backwards.", "Звук клипа играет с конца."));
inline const juce::String audioLoop = make (tr ("Loop", "Петля"), tr ("Repeats the part of the clip from the loop start to the end while the clip plays.", "Повторять часть клипа от начала петли до конца, пока клип звучит."));
inline const juce::String audioFadeIn = make (tr ("Fade in", "Фейд-ин"), tr ("Volume rise at the start of the clip on the timeline, in beats. A short fade at the edges is always added.", "Нарастание громкости в начале клипа на шкале, в долях. Короткий фейд на краях добавляется всегда."));
inline const juce::String audioFadeOut = make (tr ("Fade out", "Фейд-аут"), tr ("Volume fall at the end of the clip on the timeline, in beats.", "Затухание в конце клипа на шкале, в долях."));
inline const juce::String audioWave = make (tr ("Clip audio", "Звук клипа"), tr ("Drag the start marker and the loop brace. Shift or Alt turn off snap.", "Тяните маркер старта и скобку петли. Shift или Alt отключают прилипание."));
} // namespace help

// Tooltips of the small tabs that hide and show panels: what a click will do.
namespace tips
{
inline const juce::String hideBrowser = tr ("Hide browser (Cmd+Alt+B)", "Скрыть браузер (Cmd+Alt+B)");
inline const juce::String showBrowser = tr ("Show browser (Cmd+Alt+B)", "Показать браузер (Cmd+Alt+B)");
inline const juce::String hideDetail = tr ("Hide bottom panel (Cmd+Alt+L)", "Скрыть нижнюю панель (Cmd+Alt+L)");
inline const juce::String showDetail = tr ("Show bottom panel (Cmd+Alt+L)", "Показать нижнюю панель (Cmd+Alt+L)");
inline const juce::String hideMixer = tr ("Hide mixer (Cmd+Alt+M)", "Скрыть микшер (Cmd+Alt+M)");
inline const juce::String showMixer = tr ("Show mixer (Cmd+Alt+M)", "Показать микшер (Cmd+Alt+M)");
} // namespace tips
} // namespace Strings
