#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Every user-visible text lives here, so that an English translation can be added later.
// Texts are Russian; identifiers and comments in code are English.
namespace Strings
{
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

// Track types
inline const juce::String trackTypeMidi = utf8 ("MIDI-дорожка");
inline const juce::String trackTypeAudio = utf8 ("Аудиодорожка");
inline const juce::String trackTypeReturn = utf8 ("Возвратная дорожка");
inline const juce::String trackTypeGroup = utf8 ("Групповая дорожка");

// Default names of new objects
inline juce::String defaultTrackName (int number) { return utf8 ("MIDI ") + juce::String (number); }
inline juce::String defaultSceneName (int number) { return utf8 ("Сцена ") + juce::String (number); }
inline const juce::String defaultClipName = utf8 ("Клип");
inline const juce::String masterName = utf8 ("Мастер");

// Undo transaction names ("Отменить <имя>" in the Edit menu)
inline const juce::String undoNewTrack = utf8 ("Новая дорожка");
inline const juce::String undoDeleteTrack = utf8 ("Удалить дорожку");
inline const juce::String undoDuplicateTrack = utf8 ("Дублировать дорожку");
inline const juce::String undoRenameTrack = utf8 ("Переименовать дорожку");
inline const juce::String undoNewScene = utf8 ("Новая сцена");
inline const juce::String undoDeleteScene = utf8 ("Удалить сцену");
inline const juce::String undoDuplicateScene = utf8 ("Дублировать сцену");
inline const juce::String undoNewClip = utf8 ("Новый клип");
inline const juce::String undoDeleteClip = utf8 ("Удалить клип");
inline const juce::String undoMoveClip = utf8 ("Переместить клип");
inline const juce::String undoCopyClip = utf8 ("Копировать клип");
inline const juce::String undoEditNotes = utf8 ("Изменить ноты");
inline const juce::String undoSetInstrument = utf8 ("Заменить инструмент");
inline const juce::String undoRemoveInstrument = utf8 ("Удалить инструмент");
inline const juce::String undoTrackVolume = utf8 ("Громкость дорожки");
inline const juce::String undoTrackPan = utf8 ("Панорама дорожки");
inline const juce::String undoTrackMute = utf8 ("Выключить дорожку");
inline const juce::String undoTrackSolo = utf8 ("Соло");
inline const juce::String undoTempo = utf8 ("Темп");
inline const juce::String undoMasterVolume = utf8 ("Громкость мастера");

// Menu items and labels used by the Session View
inline const juce::String menuAddMidiTrack = utf8 ("Добавить MIDI-дорожку");
inline const juce::String menuDeleteTrack = utf8 ("Удалить дорожку");
inline const juce::String menuAddScene = utf8 ("Добавить сцену");
inline const juce::String menuDeleteScene = utf8 ("Удалить сцену");
inline const juce::String menuOpenEditor = utf8 ("Открыть редактор инструмента");
inline const juce::String menuRemoveInstrument = utf8 ("Убрать инструмент");
inline const juce::String menuScanPlugins = utf8 ("Сканировать плагины…");
inline const juce::String menuPrisma = utf8 ("Prisma (встроенный wavetable-синт)");
inline const juce::String menuPlugins = utf8 ("Инструменты AU / VST3");
inline const juce::String menuNoPlugins = utf8 ("Плагины не найдены: нажмите «Сканировать»");
inline const juce::String menuCreateDemoClip = utf8 ("Создать демо-клип");
inline const juce::String menuReplaceDemoClip = utf8 ("Заменить демо-клипом");
inline const juce::String menuLaunch = utf8 ("Запустить");
inline const juce::String menuDeleteClip = utf8 ("Удалить клип");
inline const juce::String labelScenes = utf8 ("СЦЕНЫ");
inline const juce::String labelStopAll = utf8 ("Стоп все");
inline const juce::String labelNoInstrument = utf8 ("(нет инструмента)");
inline const juce::String labelMaster = utf8 ("МАСТЕР");
inline const juce::String tipArm = utf8 ("Запись: MIDI-клавиатуры и клавиши компьютера играют на этой дорожке");

// Status bar hints and errors
inline const juce::String statusAudioDeviceFallback = utf8 ("Сохранённая звуковая карта не найдена, выбрана системная");
inline const juce::String errorPluginLoad = utf8 ("Не удалось загрузить плагин: ");
inline const juce::String errorNewerProject = utf8 ("Проект создан в более новой версии DAW");
inline const juce::String errorBrokenProject = utf8 ("Файл проекта повреждён или не является проектом DAW");
inline juce::String errorFileMissing (const juce::String& path) { return utf8 ("Файл не найден: ") + path; }
inline juce::String errorMidiRead (const juce::String& n) { return utf8 ("Не удалось прочитать MIDI-файл «") + n + utf8 ("»"); }
inline juce::String errorMidiTiming (const juce::String& n) { return utf8 ("MIDI-файл «") + n + utf8 ("» использует тайм-код SMPTE, он не поддерживается"); }
inline juce::String errorMidiNoNotes (const juce::String& n) { return utf8 ("В MIDI-файле «") + n + utf8 ("» нет нот"); }
inline juce::String errorFolderExists (const juce::String& path) { return utf8 ("Проект с таким именем уже есть: ") + path; }
inline juce::String errorWriteFile (const juce::String& path) { return utf8 ("Не удалось записать файл: ") + path; }
inline const juce::String dialogPluginTitle = utf8 ("Плагин");
inline const juce::String windowPlugins = utf8 ("Плагины (AU / VST3)");
inline const juce::String windowAudioSettings = utf8 ("Настройки звука и MIDI");

// Undo transaction names of the window actions (task 1.4)
inline const juce::String undoRenameClip = utf8 ("Переименовать клип");
inline const juce::String undoRenameScene = utf8 ("Переименовать сцену");
inline const juce::String undoTrackColour = utf8 ("Цвет дорожки");
inline const juce::String undoClipColour = utf8 ("Цвет клипа");
inline const juce::String undoSceneColour = utf8 ("Цвет сцены");
inline const juce::String undoDuplicateClip = utf8 ("Дублировать клип");
inline const juce::String undoCutClip = utf8 ("Вырезать клип");
inline const juce::String undoPasteClip = utf8 ("Вставить клип");
inline const juce::String undoCaptureScene = utf8 ("Сцена с играющими клипами");
inline const juce::String undoTrackArm = utf8 ("Запись дорожки");
inline const juce::String undoTrackWidth = utf8 ("Ширина столбца");
inline const juce::String undoMoveDevice = utf8 ("Переместить устройство");
inline const juce::String undoRemoveDevice = utf8 ("Удалить устройство");
inline const juce::String undoToggleDevice = utf8 ("Включить или выключить устройство");
inline const juce::String undoFoldDevice = utf8 ("Свернуть устройство");

// Drops from the browser (task 1.5): the name of the transaction says what was dropped
inline juce::String undoAddDevice (const juce::String& what) { return utf8 ("Добавить ") + what; }
inline juce::String undoReplaceInstrument (const juce::String& what) { return utf8 ("Заменить инструмент · ") + what; }
inline juce::String undoApplyPreset (const juce::String& what) { return utf8 ("Применить пресет · ") + what; }
inline juce::String undoAddEffect (const juce::String& what) { return utf8 ("Добавить эффект · ") + what; }
inline juce::String undoAddChain (const juce::String& what) { return utf8 ("Добавить цепочку · ") + what; }
inline juce::String undoReplaceChain (const juce::String& what) { return utf8 ("Заменить цепочку · ") + what; }
inline juce::String undoAddMidiClip (const juce::String& what) { return utf8 ("Добавить MIDI-клип · ") + what; }
inline juce::String errorChainRead (const juce::String& n) { return utf8 ("Не удалось прочитать цепочку «") + n + utf8 ("»"); }
inline juce::String errorPluginUnknown (const juce::String& n) { return utf8 ("Плагин «") + n + utf8 ("» не найден в списке плагинов"); }
inline const juce::String errorNoSlot = utf8 ("Некуда положить клип");

// Dialogs
inline const juce::String dialogOk = utf8 ("OK");
inline const juce::String dialogCancel = utf8 ("Отмена");
inline const juce::String dialogSave = utf8 ("Сохранить");
inline const juce::String dialogDontSave = utf8 ("Не сохранять");
inline juce::String dialogSaveChanges (const juce::String& name) { return utf8 ("Сохранить изменения в «") + name + utf8 ("»?"); }
inline const juce::String dialogSaveChangesText = utf8 ("Если не сохранить, изменения будут потеряны.");

// Project files (task 1.7)
inline const juce::String dialogOpenTitle = utf8 ("Открыть проект");
inline const juce::String dialogOpenFailed = utf8 ("Не удалось открыть проект");
inline const juce::String dialogSaveAsTitle = utf8 ("Сохранить проект как");
inline const juce::String dialogSaveAsPrompt = utf8 ("Название проекта. Будет создана папка «Название Project».");
inline const juce::String dialogSaveFailed = utf8 ("Не удалось сохранить проект");
inline const juce::String recoveryTitle = utf8 ("Восстановить несохранённую работу?");
inline juce::String recoveryText (const juce::String& name)
{
    return utf8 ("Прошлый сеанс закрылся неожиданно. Найдена копия проекта «") + name + utf8 ("» с несохранёнными изменениями.");
}
inline const juce::String recoveryRestore = utf8 ("Восстановить");
inline const juce::String recoveryOpenSaved = utf8 ("Открыть последнее сохранение");
inline const juce::String recoveryDiscard = utf8 ("Не открывать");
inline const juce::String infoSaved = utf8 ("Проект сохранён");
inline const juce::String infoTemplateSaved = utf8 ("Этот проект теперь шаблон для новых проектов");
inline const juce::String infoRecovered = utf8 ("Работа восстановлена из копии. Сохраните проект");
inline const juce::String clearRecent = utf8 ("Очистить список");

// Version history window
namespace versions
{
inline const juce::String title = utf8 ("История версий");
inline const juce::String labelButton = utf8 ("Метка…");
inline const juce::String restoreButton = utf8 ("Восстановить");
inline const juce::String closeButton = utf8 ("Закрыть");
inline const juce::String kindSave = utf8 ("сохранение");
inline const juce::String kindAutosave = utf8 ("автосохранение");
inline const juce::String kindRestore = utf8 ("до возврата");
inline const juce::String empty = utf8 ("Версий пока нет. Сохраните проект: каждое сохранение и автосохранение попадает сюда.");
inline juce::String labelPrompt (int number) { return utf8 ("Метка версии v") + juce::String (number) + utf8 (". Пустая строка снимает метку."); }
inline juce::String restoreTitle (int number) { return utf8 ("Вернуться к версии v") + juce::String (number) + utf8 ("?"); }
inline const juce::String restoreText = utf8 ("Текущее состояние сохранится отдельной версией, ничего не пропадёт. Но история отмены (Cmd+Z) очистится: сам возврат отменить нельзя, только вернуться к версии «до возврата».");
inline const juce::String restoreOk = utf8 ("Вернуться");
inline const juce::String restored = utf8 ("Проект возвращён к выбранной версии");
} // namespace versions

// Settings window (task 1.9)
namespace settingsui
{
inline const juce::String title = utf8 ("Настройки");
inline const juce::String tabSound = utf8 ("Звук");
inline const juce::String tabMidi = utf8 ("MIDI");
inline const juce::String tabAppearance = utf8 ("Внешний вид");
inline const juce::String tabLibrary = utf8 ("Библиотека");
inline const juce::String tabRecording = utf8 ("Запись");
inline const juce::String driver = utf8 ("Драйвер");
inline const juce::String input = utf8 ("Вход");
inline const juce::String output = utf8 ("Выход");
inline const juce::String sampleRate = utf8 ("Частота");
inline const juce::String bufferSize = utf8 ("Буфер");
inline const juce::String none = utf8 ("(нет)");
inline const juce::String noDevice = utf8 ("Звуковая карта не открыта");
inline juce::String samples (int n) { return juce::String (n) + utf8 (" сэмплов"); }
inline juce::String hz (double r)
{
    const int whole = (int) (r + 0.5);
    return juce::String ((double) whole / 1000.0, whole % 1000 == 0 ? 0 : 1).replace (".", ",") + utf8 (" кГц");
}
inline juce::String ms (double v) { return juce::String (v, 1).replace (".", ",") + utf8 (" мс"); }
inline juce::String latency (double in, double out)
{
    return utf8 ("Вход ") + ms (in) + utf8 (" · Выход ") + ms (out) + utf8 (" · Всего ") + ms (in + out);
}
inline juce::String cpu (int percent) { return utf8 ("Нагрузка процессора: ") + juce::String (percent) + " %"; }
inline const juce::String channels = utf8 ("Каналы");
inline const juce::String inputChannels = utf8 ("Входы");
inline const juce::String outputChannels = utf8 ("Выходы");
inline const juce::String channelNameHint = utf8 ("Имя канала");
inline const juce::String testTone = utf8 ("Тестовый сигнал");
inline const juce::String audioMidiSetup = utf8 ("Открыть настройку Audio MIDI");
inline const juce::String driverNoteMac = utf8 ("На Mac все звуковые карты работают через Core Audio. Отдельный драйвер ASIO не нужен.");
inline const juce::String driverNoteDev = utf8 ("Для разработки на Linux используется ALSA.");
inline const juce::String midiInputs = utf8 ("Входы MIDI");
inline const juce::String midiOutputs = utf8 ("Выходы MIDI");
inline const juce::String playTracks = utf8 ("Игра на дорожках");
inline const juce::String control = utf8 ("Управление");
inline const juce::String noMidiInputs = utf8 ("MIDI-входов нет. Подключите клавиатуру: она появится здесь сама.");
inline const juce::String noMidiOutputs = utf8 ("MIDI-выходов нет");
inline const juce::String themeHeading = utf8 ("Тема");
inline const juce::String scale = utf8 ("Масштаб интерфейса");
inline const juce::String animations = utf8 ("Анимации");
inline const juce::String libraryPath = utf8 ("Папка User Library");
inline const juce::String change = utf8 ("Изменить…");
inline const juce::String byDefault = utf8 ("По умолчанию");
inline const juce::String chooseLibrary = utf8 ("Выберите папку User Library");
inline const juce::String placesHeading = utf8 ("Папки «Места»");
inline const juce::String noPlaces = utf8 ("Папок пока нет");
inline const juce::String add = utf8 ("Добавить…");
inline const juce::String remove = utf8 ("Убрать");
inline const juce::String rescan = utf8 ("Повторно сканировать плагины…");
} // namespace settingsui

// Window
inline const juce::String windowUntitled = utf8 ("Без названия");
inline const juce::String labelPlaceholderProject = utf8 ("Перетащите инструмент из браузера сюда");
inline const juce::String labelAppName = utf8 ("Prisma");

// Menu bar (task 1.10). The text of each command is also its name in the menu.
namespace menu
{
inline const juce::String file = utf8 ("Файл");
inline const juce::String edit = utf8 ("Правка");
inline const juce::String create = utf8 ("Создать");
inline const juce::String view = utf8 ("Вид");
inline const juce::String newProject = utf8 ("Новый проект");
inline const juce::String open = utf8 ("Открыть…");
inline const juce::String openRecent = utf8 ("Открыть недавний");
inline const juce::String save = utf8 ("Сохранить");
inline const juce::String saveAs = utf8 ("Сохранить как…");
inline const juce::String saveTemplate = utf8 ("Сохранить как шаблон по умолчанию");
inline const juce::String versionHistory = utf8 ("История версий…");
inline const juce::String exportAudio = utf8 ("Экспорт…");
inline const juce::String showInFinder = utf8 ("Показать проект в Finder");
inline const juce::String scanPlugins = utf8 ("Сканировать плагины…");
inline const juce::String quit = utf8 ("Выйти");
inline const juce::String undo = utf8 ("Отменить");
inline const juce::String redo = utf8 ("Повторить");
inline const juce::String cut = utf8 ("Вырезать");
inline const juce::String copy = utf8 ("Копировать");
inline const juce::String paste = utf8 ("Вставить");
inline const juce::String duplicate = utf8 ("Дублировать");
inline const juce::String remove = utf8 ("Удалить");
inline const juce::String rename = utf8 ("Переименовать");
inline const juce::String newTrack = utf8 ("MIDI-дорожка");
inline const juce::String newScene = utf8 ("Сцена");
inline const juce::String captureScene = utf8 ("Сцена с играющими клипами");
inline const juce::String newClip = utf8 ("Пустой MIDI-клип");
inline const juce::String captureMidi = utf8 ("Захватить MIDI");
inline const juce::String browser = utf8 ("Браузер");
inline const juce::String detail = utf8 ("Детальная панель");
inline const juce::String mixer = utf8 ("Микшер");
inline const juce::String switchView = utf8 ("Session / Arrangement");
inline const juce::String zoomIn = utf8 ("Увеличить масштаб");
inline const juce::String zoomOut = utf8 ("Уменьшить масштаб");
inline const juce::String settings = utf8 ("Настройки…");
inline const juce::String playStop = utf8 ("Играть / стоп");
inline const juce::String computerKeys = utf8 ("Игра на клавиатуре компьютера");
inline const juce::String find = utf8 ("Поиск в браузере");
inline const juce::String insertTrackHere = utf8 ("Вставить MIDI-дорожку");
inline const juce::String colour = utf8 ("Цвет");
inline const juce::String createClipHere = utf8 ("Создать MIDI-клип");
inline const juce::String stopAll = utf8 ("Остановить все клипы");
inline const juce::String quantise = utf8 ("Квантовать");
inline const juce::String quantiseOptions = utf8 ("Квантовать…");
inline const juce::String toggleClipEditor = utf8 ("Редактор клипа / цепочка устройств");
inline const juce::String keyboardStrip = utf8 ("Экранная клавиатура");
inline const juce::String projectProperties = utf8 ("Свойства проекта…");
inline const juce::String openClip = utf8 ("Открыть в редакторе");
} // namespace menu

// Transport bar
namespace transport
{
inline const juce::String arrangementTab = utf8 ("Arrangement");
inline const juce::String sessionTab = utf8 ("Session");
inline const juce::String capture = utf8 ("Захватить");
inline const juce::String capture10 = utf8 ("10 мин");
inline const juce::String versionNone = utf8 ("v1 · новый");
// "v14 · сейчас", "v14 · 5 мин", "v14 · 3 ч"; sinceMs < 0: never saved by hand
inline juce::String versionText (int number, juce::int64 sinceMs)
{
    juce::String when = utf8 ("авто");
    if (sinceMs >= 0)
    {
        const auto minutes = sinceMs / 60000;
        when = minutes < 1 ? utf8 ("сейчас") : (minutes < 60 ? juce::String (minutes) + utf8 (" мин") : juce::String (minutes / 60) + utf8 (" ч"));
    }
    return "v" + juce::String (number) + utf8 (" · ") + when;
}
inline const juce::String cpu = utf8 ("CPU");
inline const juce::String keysOn = utf8 ("КЛАВ");
inline const juce::String midi = utf8 ("MIDI");
inline const juce::String scaleOff = utf8 ("Лад: выкл");
} // namespace transport

// Browser panel (frame only; the sources come with task 1.5)
namespace browser
{
inline const juce::String searchPlaceholder = utf8 ("Поиск  ⌘F");
inline const juce::String instruments = utf8 ("Инструменты");
inline const juce::String presets = utf8 ("Пресеты");
inline const juce::String effects = utf8 ("Эффекты");
inline const juce::String plugins = utf8 ("Плагины");
inline const juce::String sounds = utf8 ("Звуки");
inline const juce::String midiFiles = utf8 ("MIDI-файлы");
inline const juce::String places = utf8 ("Места");
inline const juce::String emptyCategory = utf8 ("Здесь появятся элементы категории");
inline juce::String notFound (const juce::String& query) { return utf8 ("Ничего не найдено по «") + query + utf8 ("»"); }
inline const juce::String chains = utf8 ("Цепочки");
inline const juce::String recent = utf8 ("Недавние");
inline const juce::String builtIn = utf8 ("встроенный");
inline const juce::String otherVendor = utf8 ("Другие производители");
inline const juce::String builtinGroup = utf8 ("Встроенные");
inline const juce::String builtinEffectsSoon = utf8 ("Появятся на этапе 6");
inline const juce::String userChains = utf8 ("Мои цепочки");
inline const juce::String devicesShort = utf8 ("устр.");
inline juce::String userGroup (const juce::String& device) { return utf8 ("Мои пресеты · ") + device; }
inline juce::String projectPlace (const juce::String& folderName) { return utf8 ("Проект · ") + folderName; }
inline const juce::String addFolder = utf8 ("Добавить папку…");
inline const juce::String dropFolder = utf8 ("Отпустите, чтобы добавить папку в Места");
inline const juce::String addFolderTitle = utf8 ("Выберите папку для «Мест»");
inline const juce::String removeFolder = utf8 ("Убрать из «Мест»");
inline const juce::String noPlaces = utf8 ("Добавьте папку со звуками и MIDI-файлами");
inline const juce::String previewToggle = utf8 ("Предпрослушивание");
inline const juce::String labelColour = utf8 ("Цвет метки");
inline const juce::String labelNone = utf8 ("Без метки");
inline const juce::String renamePreset = utf8 ("Переименовать…");
inline const juce::String deletePreset = utf8 ("Удалить…");
inline const juce::String showInFinder = utf8 ("Показать в Finder");
inline const juce::String emptyRecent = utf8 ("Здесь появятся элементы, которые вы использовали");
inline const juce::String emptyLabel = utf8 ("Нет элементов с этой меткой");
inline const juce::String deleteTitle = utf8 ("Удалить из библиотеки");
inline juce::String deleteQuestion (const juce::String& name) { return utf8 ("Удалить «") + name + utf8 ("»? Это нельзя отменить."); }
inline const juce::String deleteOk = utf8 ("Удалить");
inline const juce::String dropNewTrack = utf8 ("Отпустите, чтобы создать новую дорожку");
inline const juce::String saveChain = utf8 ("Сохранить цепочку…");
inline const juce::String savePreset = utf8 ("Сохранить пресет…");
inline const juce::String savePresetTitle = utf8 ("Сохранить пресет");
inline const juce::String savePresetPrompt = utf8 ("Название пресета");
inline const juce::String saveChainPrompt = utf8 ("Название цепочки");
inline juce::String presetSaved (const juce::String& n) { return utf8 ("Пресет «") + n + utf8 ("» сохранён в библиотеку"); }
inline juce::String chainSaved (const juce::String& n) { return utf8 ("Цепочка «") + n + utf8 ("» сохранена в библиотеку"); }
inline const juce::String factoryPresets = utf8 ("Пресеты Prisma");
inline const juce::String myPresets = utf8 ("Мои пресеты");
inline const juce::String noUserPresets = utf8 ("(пока нет)");
inline const juce::String replaceInstrument = utf8 ("Заменить инструмент");
inline const juce::String useOnTrack = utf8 ("Поставить на выбранную дорожку");
} // namespace browser

// Detail panel and device chain
namespace detail
{
inline const juce::String transpose = utf8 ("Транспонирование");
inline const juce::String loading = utf8 ("Загрузка…");
inline const juce::String track = utf8 ("Дорожка");
inline const juce::String input = utf8 ("Вход");
inline const juce::String monitor = utf8 ("Мониторинг");
inline const juce::String chain = utf8 ("Цепочка");
inline const juce::String noTrack = utf8 ("Выберите дорожку, чтобы увидеть её цепочку устройств");
inline const juce::String dropHere = utf8 ("Перетащите сюда эффект или плагин из браузера");
inline juce::String devices (int n) { return juce::String (n) + utf8 (" устр."); }
inline juce::String missingPlugin (const juce::String& name) { return utf8 ("Плагин не найден: ") + name; }
inline const juce::String inputAllMidi = utf8 ("Все MIDI-входы");
inline const juce::String inputKeyboard = utf8 ("Клавиатура компьютера");
inline const juce::String inputNone = utf8 ("Нет");
inline const juce::String inputDevice = utf8 ("MIDI-устройство");
inline const juce::String monitorAuto = utf8 ("Авто");
inline const juce::String monitorIn = utf8 ("Вкл");
inline const juce::String monitorOff = utf8 ("Выкл");
inline const juce::String showOtherParam = utf8 ("Показать другой параметр…");
} // namespace detail

// Scales (task 2.7). The names of the scales are in core (daw/music/Scales.h).
inline const juce::String rootNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline const juce::String scaleNone = utf8 ("Выкл");
inline const juce::String scaleTonic = utf8 ("Тоника");
inline const juce::String scaleMode = utf8 ("Лад");

// Undo transaction names of the clip editor and of the stage 2 project settings
inline const juce::String undoNewNote = utf8 ("Нарисовать ноту");
inline const juce::String undoDeleteNotes = utf8 ("Удалить ноты");
inline const juce::String undoMoveNotes = utf8 ("Переместить ноты");
inline const juce::String undoResizeNotes = utf8 ("Изменить длину нот");
inline const juce::String undoCopyNotes = utf8 ("Копировать ноты");
inline const juce::String undoPasteNotes = utf8 ("Вставить ноты");
inline const juce::String undoCutNotes = utf8 ("Вырезать ноты");
inline const juce::String undoDuplicateNotes = utf8 ("Дублировать ноты");
inline const juce::String undoVelocity = utf8 ("Сила нажатия");
inline const juce::String undoQuantise = utf8 ("Квантизация");
inline const juce::String undoQuantiseSettings = utf8 ("Настройки квантизации");
inline const juce::String undoTransposeNotes = utf8 ("Транспонировать ноты");
inline const juce::String undoSplitNotes = utf8 ("Разрезать ноты");
inline const juce::String undoJoinNotes = utf8 ("Склеить ноты");
inline const juce::String undoLoopRange = utf8 ("Петля клипа");
inline const juce::String undoLoopSwitch = utf8 ("Петля вкл / выкл");
inline const juce::String undoStartPoint = utf8 ("Точка старта клипа");
inline const juce::String undoClipScale = utf8 ("Лад клипа");
inline const juce::String undoProjectScale = utf8 ("Лад проекта");
inline const juce::String undoTuning = utf8 ("Строй");
inline const juce::String undoTrackTranspose = utf8 ("Транспонирование дорожки");
inline const juce::String undoSignature = utf8 ("Размер такта");
inline const juce::String undoAuthor = utf8 ("Автор проекта");
inline const juce::String undoProjectNotes = utf8 ("Заметки проекта");

// The clip editor (task 2.4)
namespace editor
{
inline const juce::String noClip = utf8 ("Клип не выбран");
inline const juce::String noClipHint = utf8 ("Выберите клип в Session View: двойной щелчок открывает его здесь. Shift+Tab возвращает цепочку устройств.");
inline const juce::String loop = utf8 ("Петля");
inline const juce::String loopStart = utf8 ("Начало петли");
inline const juce::String loopEnd = utf8 ("Конец");
inline const juce::String startPoint = utf8 ("Старт");
inline const juce::String grid = utf8 ("Сетка");
inline const juce::String gridAdaptive = utf8 ("Адаптивная");
inline const juce::String snap = utf8 ("Прилипание");
inline const juce::String ownScale = utf8 ("Свой лад");
inline const juce::String scaleOnly = utf8 ("Только лад");
inline const juce::String fold = utf8 ("Fold");
inline const juce::String quantise = utf8 ("Квантовать…");
inline const juce::String transpose = utf8 ("Транспон.");
inline const juce::String pencil = utf8 ("Карандаш");
inline const juce::String preview = utf8 ("Слушать");
inline const juce::String velocity = utf8 ("Velocity");
inline const juce::String controllers = utf8 ("Контроллеры");
inline const juce::String modwheel = utf8 ("Modwheel");
inline const juce::String pitchBend = utf8 ("Pitch Bend");
inline const juce::String sustain = utf8 ("Сустейн");
inline const juce::String controllersEmpty = utf8 ("Нет движений: запишите их с MIDI-клавиатуры или проведите мышью");
inline const juce::String menuSelectAll = utf8 ("Выделить все");
inline const juce::String menuInvert = utf8 ("Инвертировать выделение");
inline const juce::String menuSelectPitch = utf8 ("Выделить ноты этой высоты");
inline const juce::String menuDelete = utf8 ("Удалить");
inline const juce::String menuDuplicate = utf8 ("Дублировать");
inline const juce::String menuCopy = utf8 ("Копировать");
inline const juce::String menuPaste = utf8 ("Вставить");
inline const juce::String menuSplit = utf8 ("Разрезать здесь");
inline const juce::String menuJoin = utf8 ("Склеить");
inline const juce::String menuQuantise = utf8 ("Квантовать");
inline const juce::String menuQuantiseOptions = utf8 ("Квантовать…");
inline const juce::String menuLoopSelection = utf8 ("Петля вокруг выделения");
inline juce::String selectedCount (int n, int total) { return juce::String (n) + utf8 (" из ") + juce::String (total); }
inline const juce::String gridDivisionNames[] = { utf8 ("1/4"), utf8 ("1/8"), utf8 ("1/16"), utf8 ("1/32"), utf8 ("1/64") };
inline juce::String gridName (int denominator, bool triplet) { return "1/" + juce::String (denominator) + (triplet ? "T" : ""); }
inline juce::String loopExtended (double beats) { return utf8 ("Петля клипа удлинена до ") + juce::String (beats, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + utf8 (" долей"); }
} // namespace editor

namespace quantiseUi
{
inline const juce::String title = utf8 ("Квантизация");
inline const juce::String grid = utf8 ("Сетка");
inline const juce::String strength = utf8 ("Сила");
inline const juce::String swing = utf8 ("Свинг");
inline const juce::String ends = utf8 ("Квантовать концы нот");
inline const juce::String apply = utf8 ("Применить");
inline const juce::String close = utf8 ("Закрыть");
inline const juce::String gridNames[] = { utf8 ("1/4"), utf8 ("1/8"), utf8 ("1/8T"), utf8 ("1/16"), utf8 ("1/16T"), utf8 ("1/32"), utf8 ("1/32T") };
inline const double gridBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };
inline constexpr int numGrids = 7;
} // namespace quantiseUi

namespace projectProps
{
inline const juce::String title = utf8 ("Свойства проекта");
inline const juce::String tempo = utf8 ("Темп");
inline const juce::String signature = utf8 ("Размер");
inline const juce::String scale = utf8 ("Лад");
inline const juce::String tuning = utf8 ("Строй A4");
inline const juce::String tuningSuffix = utf8 (" Гц");
inline const juce::String tuningHint = utf8 ("Встроенные синты. У сторонних плагинов строй настраивается в самом плагине");
inline const juce::String author = utf8 ("Автор");
inline const juce::String notes = utf8 ("Заметки");
inline const juce::String reset = utf8 ("440");
} // namespace projectProps

namespace keyboardUi
{
inline const juce::String inScale = utf8 ("В ладу");
inline const juce::String chords = utf8 ("Аккорды");
// "C3" for the note the A key plays; the octave runs from there over the 17 keys (A to ;).
inline juce::String octaveName (int baseNote) { return juce::MidiMessage::getMidiNoteName (baseNote, true, true, 3); }
inline juce::String octaveMessage (int baseNote)
{
    return utf8 ("Октава: ") + octaveName (baseNote) + utf8 ("–") + octaveName (baseNote + 16);
}
inline juce::String velocityMessage (int velocity) { return utf8 ("Сила: ") + juce::String (velocity); }
inline const juce::String keysOffHint = utf8 ("Клавиатура Mac выключена, нажмите M");
inline juce::String stripCaption (int baseNote, int velocity)
{
    return utf8 ("Октава ") + octaveName (baseNote) + "\n" + utf8 ("сила ") + juce::String (velocity);
}
inline const juce::String noScaleHint = utf8 ("Лад проекта выключен: клавиши играют все ноты подряд");
} // namespace keyboardUi

namespace arrangement
{
inline const juce::String placeholder = utf8 ("Arrangement появится в следующем этапе");
inline const juce::String hint = utf8 ("Здесь будет линейная шкала времени. Tab возвращает в Session.");
} // namespace arrangement

// Mixer
inline const juce::String labelPan = utf8 ("Пан");
inline const juce::String labelCue = utf8 ("Cue");
inline const juce::String labelSendA = utf8 ("A");
inline const juce::String labelSendB = utf8 ("B");

// Prisma macro knobs (the eight parameters of the compact device view)
namespace macro
{
inline const juce::String wtPos = utf8 ("Морф WT");
inline const juce::String detune = utf8 ("Детюн");
inline const juce::String cutoff = utf8 ("Срез");
inline const juce::String resonance = utf8 ("Резонанс");
inline const juce::String drive = utf8 ("Драйв");
inline const juce::String attack = utf8 ("Атака");
inline const juce::String lfoRate = utf8 ("LFO");
inline const juce::String width = utf8 ("Ширина");
} // namespace macro

// Info bar: title and description of an element under the mouse ("Название — что делает").
namespace recording
{
// Russian plural: 1 такт, 2 такта, 5 тактов
inline juce::String plural (int n, const char* one, const char* few, const char* many)
{
    const int mod100 = std::abs (n) % 100, mod10 = std::abs (n) % 10;
    const char* word = (mod100 >= 11 && mod100 <= 14) ? many : (mod10 == 1 ? one : (mod10 >= 2 && mod10 <= 4 ? few : many));
    return juce::String (n) + " " + utf8 (word);
}
inline const juce::String undoRecordClip = utf8 ("Запись клипа");
inline const juce::String undoOverdub = utf8 ("Overdub");
inline const juce::String undoCaptureMidi = utf8 ("Захват MIDI");
inline const juce::String undoRecordSource = utf8 ("Источник записи");
inline const juce::String undoDrawController = utf8 ("Рисование контроллера");
inline const juce::String ovr = utf8 ("OVR");
// Transport menus (docs/spec/02, tasks 2.1 and 2.3)
inline const juce::String menuVolume = utf8 ("Громкость");
inline const juce::String menuOnlyRecording = utf8 ("Только во время записи");
inline const juce::String menuCountIn = utf8 ("Отсчёт");
inline const juce::String menuClickSound = utf8 ("Звук клика");
inline const juce::String soundClassic = utf8 ("Классический");
inline const juce::String soundWood = utf8 ("Деревянный");
inline const juce::String none = utf8 ("Нет");
inline const juce::String off = utf8 ("Выкл");
inline const juce::String menuFixedLength = utf8 ("Фиксированная длина записи");
inline const juce::String menuRecordQuantise = utf8 ("Квантизация записи");
inline juce::String bars (int n) { return plural (n, "такт", "такта", "тактов"); }
inline juce::String volumeDb (int db) { return db <= -60 ? utf8 ("−∞") : (db == 0 ? juce::String ("0 ") : utf8 ("−") + juce::String (-db) + " ") + utf8 ("дБ"); }
// "−1. 3": the count-in shows the bar and beat that are left before the recording starts
inline const juce::String recordSourceLabel = utf8 ("Запись");
inline const juce::String sourceMidi = utf8 ("MIDI");
inline const juce::String sourceAudio = utf8 ("Звук");
inline const juce::String audioSoon = utf8 ("Запись звука появится на этапе 5");
// Settings, tab "Запись"
inline const juce::String correction = utf8 ("Поправка задержки");
inline const juce::String correctionReset = utf8 ("Сбросить");
inline const juce::String automaticCompensation = utf8 ("Автоматическая компенсация");
inline const juce::String correctionNote = utf8 ("Записанные ноты сдвигаются на задержку звука и полбуфера, как слышит игрок. Если ноты ложатся в запись чуть раньше или позже, поправьте вручную.");
inline juce::String signedMs (double v) { return (v > 0.05 ? juce::String ("+") : (v < -0.05 ? utf8 ("−") : juce::String())) + juce::String (std::abs (v), 1).replace (".", ",") + utf8 (" мс"); }
inline juce::String autoMs (double v) { return juce::String (v, 1).replace (".", ",") + utf8 (" мс"); }
inline const juce::String recordNoTrack = utf8 ("Нечего записывать: взведите MIDI-дорожку или выберите её");
inline const juce::String captureNothing = utf8 ("Нечего захватывать: сыграйте что-нибудь на MIDI-клавиатуре");
inline const juce::String captureNoTrack = utf8 ("Выберите MIDI-дорожку, на которую захватить фразу");
// "Захвачено 8 тактов, 112 нот → Arp, сцена Drop"
inline juce::String captured (int bars, int notes, const juce::String& track, const juce::String& scene)
{
    return utf8 ("Захвачено ") + plural (bars, "такт", "такта", "тактов") + ", " + plural (notes, "нота", "ноты", "нот") + utf8 (" → ") + track
           + utf8 (", сцена ") + scene;
}
} // namespace recording

namespace help
{
inline juce::String make (const juce::String& title, const juce::String& text) { return title + "\n" + text; }
inline const juce::String soon3 = utf8 ("Появится на этапе 3");
inline const juce::String soon6 = utf8 ("Появится на этапе 6");

inline const juce::String logo = make (utf8 ("Призма"), utf8 ("Главное окно DAW. Цвета дорожек идут по спектру слева направо."));
inline const juce::String tempo = make (utf8 ("Темп"), utf8 ("Ударов в минуту. Тяните мышью, колесо меняет, двойной щелчок вводит число. Shift: точнее."));
inline const juce::String signature = make (utf8 ("Размер"), utf8 ("Размер такта проекта. Меняется в окне «Свойства проекта» (Cmd+Alt+P)."));
inline const juce::String metronome = make (utf8 ("Метроном"), utf8 ("Щелчок включает метроном проекта. Правая кнопка: громкость, «Только во время записи», отсчёт, звук клика."));
inline const juce::String play = make (utf8 ("Играть"), utf8 ("Запускает и останавливает воспроизведение. Пробел делает то же самое."));
inline const juce::String stop = make (utf8 ("Стоп"), utf8 ("Останавливает воспроизведение и все клипы. Повторное нажатие возвращает в начало."));
inline const juce::String record = make (utf8 ("Запись"), utf8 ("Запись в выбранную сцену на взведённых дорожках; повторный щелчок останавливает. Правая кнопка: фиксированная длина и квантизация записи."));
inline const juce::String overdub = make (utf8 ("Overdub"), utf8 ("Включён: запись на играющий клип добавляет ноты слоями, старые не стираются. Каждый круг отменяется отдельно."));
inline const juce::String position = make (utf8 ("Позиция"), utf8 ("Такты, доли и шестнадцатые от начала воспроизведения."));
inline const juce::String loop = make (utf8 ("Петля"), soon3);
inline const juce::String ribbon = make (utf8 ("Лента «10 мин»"), utf8 ("Вечная запись: последние 10 минут игры. Нажмите “Захватить” или Cmd+Shift+C"));
inline const juce::String recordSlot = make (utf8 ("Кружок записи"), utf8 ("Щелчок начинает запись в этот слот на ближайшей границе запуска. Нужна взведённая (или выбранная) MIDI-дорожка."));
inline const juce::String recordingSlot = make (utf8 ("Идёт запись"), utf8 ("Щелчок заканчивает запись на ближайшем такте. Клип сразу играет по кругу. Cmd+Z удаляет записанный клип."));
inline const juce::String trackRecordSource = make (utf8 ("Источник записи"), utf8 ("Что пишет эта дорожка: MIDI с клавиатуры или звук с входа."));
inline const juce::String trackRecordAudio = make (utf8 ("Запись звука"), utf8 ("Запись звука появится на этапе 5"));
inline const juce::String controllerLane = make (utf8 ("Полоса контроллера"), utf8 ("Тяните мышью, чтобы нарисовать значения: события этого типа под мазком заменяются. Весь мазок отменяется одним Cmd+Z."));
inline const juce::String morph = make (utf8 ("Морф A ↔ B"), utf8 ("Плавный переход между снимками ручек. ") + soon6);
inline const juce::String tabSession = make (utf8 ("Session"), utf8 ("Сетка клипов для живой игры. Tab переключает Session и Arrangement."));
inline const juce::String tabArrangement = make (utf8 ("Arrangement"), utf8 ("Пока пустой вид: Arrangement появится на этапе 3. Tab переключает Session и Arrangement."));
inline const juce::String version = make (utf8 ("История версий"), utf8 ("Номер версии и время с последнего сохранения. Щелчок открывает список: метки, возврат к версии."));
inline const juce::String cpu = make (utf8 ("Нагрузка процессора"), utf8 ("Щелчок открывает настройки звука."));
inline const juce::String midiIn = make (utf8 ("MIDI-вход"), utf8 ("Мигает, когда приходят события с MIDI-устройств."));
inline const juce::String keysIndicator = make (utf8 ("Клавиатура компьютера"), utf8 ("Когда горит, буквенные клавиши играют ноты; рядом октава клавиши A. Включается клавишей M. Z и X сдвигают октаву, C и V меняют силу."));

inline const juce::String browserPanel = make (utf8 ("Браузер"), utf8 ("Инструменты, пресеты, эффекты, цепочки и звуки. Тяните элементы в проект."));
inline const juce::String browserPreview = make (utf8 ("Предпрослушивание"), utf8 ("Когда включено, выбранный пресет Prisma играет фразу в темпе проекта. Для сторонних плагинов предпрослушивания нет."));
inline const juce::String browserItem = utf8 ("Тяните на дорожку, в цепочку или на пустое место. Двойной щелчок ставит на выбранную дорожку, правая кнопка открывает меню.");
inline const juce::String browserAddFolder = utf8 ("Добавляет папку со звуками и MIDI-файлами в «Места».");
inline const juce::String browserSearch = make (utf8 ("Поиск"), utf8 ("Ищет по всем категориям сразу. Cmd+F переводит сюда курсор, Esc очищает."));
inline juce::String browserCategory (const juce::String& name) { return make (name, utf8 ("Раздел браузера: щелчок показывает его содержимое.")); }

inline const juce::String trackHeader = make (utf8 ("Заголовок дорожки"), utf8 ("Щелчок выделяет дорожку, двойной щелчок переименовывает, правая кнопка открывает меню. Правый край тянется, меняя ширину столбца."));
inline const juce::String clipSlot = make (utf8 ("Клип"), utf8 ("Щелчок выбирает клип, треугольник слева запускает. Двойной щелчок по пустому слоту создаёт клип. Клип можно перетащить в другой слот (Alt: копия)."));
inline const juce::String emptySlot = make (utf8 ("Пустой слот"), utf8 ("Щелчок останавливает дорожку, двойной щелчок создаёт пустой MIDI-клип на такт."));
inline const juce::String sceneLaunch = make (utf8 ("Запуск сцены"), utf8 ("Запускает все клипы этой сцены. Двойной щелчок по названию переименовывает."));
inline const juce::String sceneName = make (utf8 ("Сцена"), utf8 ("Щелчок выделяет сцену, двойной щелчок переименовывает, правая кнопка открывает меню."));
inline const juce::String stopAllClips = make (utf8 ("Остановить все клипы"), utf8 ("Останавливает клипы всех дорожек на ближайшей границе квантизации."));
inline const juce::String masterHeader = make (utf8 ("Мастер"), utf8 ("Общий выход проекта. Справа от клипов находятся кнопки запуска сцен."));
inline const juce::String fader = make (utf8 ("Громкость"), utf8 ("Тяните фейдер, колесо меняет, двойной щелчок сбрасывает в 0 дБ. Shift: точнее."));
inline const juce::String volumeReadout = make (utf8 ("Громкость в дБ"), utf8 ("Двойной щелчок открывает ввод числа: Enter подтверждает, Esc отменяет."));
inline const juce::String panKnob = make (utf8 ("Панорама"), utf8 ("Положение в стерео. Двойной щелчок возвращает в центр, Shift при вращении: точнее."));
inline const juce::String sendKnob = make (utf8 ("Посыл"), utf8 ("Отправляет часть сигнала на возвратную дорожку. ") + soon6);
inline const juce::String cueKnob = make (utf8 ("Прослушивание"), utf8 ("Громкость предпрослушивания. ") + soon6);
inline const juce::String meter = make (utf8 ("Уровень"), utf8 ("Пиковый уровень сигнала дорожки."));
inline const juce::String activator = make (utf8 ("Дорожка включена"), utf8 ("Показывает номер дорожки. Выключенная дорожка молчит."));
inline const juce::String solo = make (utf8 ("Solo"), utf8 ("Слушать только эту дорожку. Cmd-щелчок добавляет к уже включённым."));
inline const juce::String arm = make (utf8 ("Запись дорожки"), utf8 ("MIDI-клавиатуры и клавиши компьютера играют на этой дорожке. Cmd-щелчок добавляет к уже включённым."));
inline const juce::String emptyProject = make (utf8 ("Пустой проект"), utf8 ("Создайте дорожку из меню «Создать» или клавишей Cmd+T."));
inline const juce::String splitterBrowser = make (utf8 ("Граница браузера"), utf8 ("Тяните, чтобы изменить ширину. Треугольник прячет и показывает браузер (Cmd+Alt+B)."));
inline const juce::String splitterDetail = make (utf8 ("Граница детальной панели"), utf8 ("Тяните, чтобы изменить высоту. Треугольник прячет и показывает панель (Cmd+Alt+L)."));
inline const juce::String splitterMixer = make (utf8 ("Микшер"), utf8 ("Треугольник прячет и показывает микшер (Cmd+Alt+M)."));
inline const juce::String hscroll = make (utf8 ("Прокрутка"), utf8 ("Прокручивает столбцы дорожек."));
inline const juce::String vscroll = make (utf8 ("Прокрутка"), utf8 ("Прокручивает сцены."));

inline const juce::String devicePower = make (utf8 ("Выключатель устройства"), utf8 ("Выключенное устройство пропускает звук насквозь."));
inline const juce::String devicePresets = make (utf8 ("Пресеты устройства"), utf8 ("Пресеты этого устройства и сохранение ваших настроек в библиотеку."));
inline const juce::String devicePresetsInstrument = make (utf8 ("Пресеты инструмента"), utf8 ("Пресеты инструмента, замена инструмента и сохранение ваших настроек в библиотеку."));
inline const juce::String deviceFold = make (utf8 ("Свернуть"), utf8 ("Сворачивает устройство в узкую полосу."));
inline const juce::String deviceOpen = make (utf8 ("Открыть в окне"), utf8 ("Полный редактор устройства в отдельном окне."));
inline const juce::String deviceClose = make (utf8 ("Удалить устройство"), utf8 ("Убирает устройство из цепочки. Cmd+Z возвращает."));
inline const juce::String deviceHeader = make (utf8 ("Заголовок устройства"), utf8 ("Тяните, чтобы переставить устройство. Delete удаляет выделенное."));
inline const juce::String deviceKnob = make (utf8 ("Параметр устройства"), utf8 ("Тяните, колесо меняет, двойной щелчок возвращает значение по умолчанию. Shift: точнее."));
inline const juce::String wavetable = make (utf8 ("Wavetable"), utf8 ("Форма волны выбранной таблицы и положение внутри неё."));
inline const juce::String missingDevice = make (utf8 ("Плагин не найден"), utf8 ("Состояние сохранено и вернётся, когда плагин появится. Звук идёт насквозь."));
inline const juce::String dropZone = make (utf8 ("Конец цепочки"), utf8 ("Сюда можно будет перетащить эффект или плагин из браузера."));
inline const juce::String detailInfo = make (utf8 ("Выбранная дорожка"), utf8 ("Вход, мониторинг и число устройств дорожки."));
// Clip editor, scale, tuning and the stage 2 windows
inline const juce::String scaleButton = make (utf8 ("Лад проекта"), utf8 ("Тоника и лад проекта. Новые клипы берут его, клавиатура компьютера и редактор клипа подсвечивают его ноты."));
inline const juce::String editorProps = make (utf8 ("Свойства клипа"), utf8 ("Петля, сетка, лад и транспонирование выбранного клипа."));
inline const juce::String editorName = make (utf8 ("Название клипа"), utf8 ("Двойной щелчок переименовывает. Квадрат справа меняет цвет."));
inline const juce::String editorLoop = make (utf8 ("Петля"), utf8 ("Включена: клип играет по кругу. Выключена: один раз от старта до конца. Cmd+L ставит петлю вокруг выделенных нот."));
inline const juce::String editorLoopStart = make (utf8 ("Начало петли"), utf8 ("В долях от начала клипа. Скобка на линейке тянется мышью."));
inline const juce::String editorLoopEnd = make (utf8 ("Конец клипа"), utf8 ("В долях. Нота, перетянутая за этот край, удлиняет клип на целые такты."));
inline const juce::String editorStart = make (utf8 ("Точка старта"), utf8 ("Откуда клип начинает играть при запуске. Треугольник на линейке тянется мышью."));
inline const juce::String editorGrid = make (utf8 ("Сетка"), utf8 ("Шаг сетки и длина новой ноты. Cmd+1 мельче, Cmd+2 крупнее, Cmd+3 триоли."));
inline const juce::String editorSnap = make (utf8 ("Прилипание"), utf8 ("Ноты прилипают к сетке при перетаскивании. Cmd во время перетаскивания отключает. Cmd+4."));
inline const juce::String editorScale = make (utf8 ("Лад клипа"), utf8 ("Выбор тоники и лада. Строки вне лада затемнены, тоника подсвечена."));
inline const juce::String editorOwnScale = make (utf8 ("Свой лад"), utf8 ("Выключено: клип показывает лад проекта и меняется вместе с ним."));
inline const juce::String editorScaleOnly = make (utf8 ("Только лад"), utf8 ("Новые ноты и перетаскивание по высоте попадают только на ноты лада."));
inline const juce::String editorFold = make (utf8 ("Fold"), utf8 ("Показывает только строки, где есть ноты (или строки лада, если нот нет)."));
inline const juce::String editorQuantise = make (utf8 ("Квантизация"), utf8 ("Cmd+U квантует выделенные ноты по текущим настройкам, Cmd+Shift+U открывает окно настроек."));
inline const juce::String editorTranspose = make (utf8 ("Транспонирование"), utf8 ("Сдвигает выделенные ноты или все. С ладом ±1 идёт по ступеням лада, ±12 всегда на октаву. Cmd+Shift+стрелки."));
inline const juce::String editorPencil = make (utf8 ("Карандаш"), utf8 ("Щелчок рисует ноту, щелчок по ноте удаляет. Клавиша B."));
inline const juce::String editorPreview = make (utf8 ("Прослушивание"), utf8 ("Новая, выделенная и перетаскиваемая нота звучит коротко."));
inline const juce::String editorRuler = make (utf8 ("Линейка"), utf8 ("Скобка петли: тяните края и середину. Треугольник: точка старта. Щелчок ставит курсор вставки."));
inline const juce::String editorKeys = make (utf8 ("Клавиши"), utf8 ("Щелчок играет ноту на дорожке и выделяет все ноты этой высоты."));
inline const juce::String editorGridArea = make (utf8 ("Сетка нот"), utf8 ("Двойной щелчок рисует или удаляет ноту, рамка выделяет, правый край ноты меняет длину, Alt копирует, Cmd отключает прилипание. Cmd+колесо: масштаб, Alt+колесо: высота строк."));
inline const juce::String editorVelocity = make (utf8 ("Сила нажатия"), utf8 ("Тяните столбик. Alt+тянуть по нескольким столбикам рисует рампу."));
inline const juce::String editorControllers = make (utf8 ("Контроллеры"), utf8 ("Колесо модуляции, питч-бенд и сустейн. Полоса сворачивается."));
inline const juce::String arrangementView = make (utf8 ("Arrangement"), utf8 ("Появится на этапе 3. Tab возвращает в Session."));
inline const juce::String keyboardStripHelp = make (utf8 ("Экранная клавиатура"), utf8 ("Играет на выбранной дорожке. Cmd+Alt+K прячет полосу."));
inline const juce::String keyboardInScale = make (utf8 ("В ладу"), utf8 ("Клавиши A S D F G H J K L ; играют подряд ступени лада проекта. Внешней MIDI-клавиатуры это не касается."));
inline const juce::String keyboardChords = make (utf8 ("Аккорды"), utf8 ("Одна клавиша играет трезвучие лада проекта (нужен включённый лад и режим «В ладу»)."));
inline const juce::String trackTranspose = make (utf8 ("Транспонирование дорожки"), utf8 ("Сдвигает всё, что идёт в инструмент дорожки, включая живую игру, на −48…+48 полутонов. Ноты клипов не меняются."));
inline const juce::String projectTempoField = make (utf8 ("Темп"), utf8 ("Темп проекта в ударах в минуту."));
inline const juce::String projectSignatureField = make (utf8 ("Размер"), utf8 ("Размер такта проекта."));
inline const juce::String projectTuning = make (utf8 ("Строй A4"), utf8 ("Частота ноты ля первой октавы, 415…466 Гц. Встроенные синты. У сторонних плагинов строй настраивается в самом плагине. Двойной щелчок возвращает 440."));
inline const juce::String projectAuthor = make (utf8 ("Автор"), utf8 ("Имя автора проекта."));
inline const juce::String projectNotes = make (utf8 ("Заметки"), utf8 ("Свободный текст, сохраняется вместе с проектом."));
inline const juce::String infoBar = make (utf8 ("Строка подсказок"), utf8 ("Показывает название и действие элемента под мышью."));
} // namespace help

// Tooltips of the small tabs that hide and show panels: what a click will do.
namespace tips
{
inline const juce::String hideBrowser = utf8 ("Скрыть браузер (Cmd+Alt+B)");
inline const juce::String showBrowser = utf8 ("Показать браузер (Cmd+Alt+B)");
inline const juce::String hideDetail = utf8 ("Скрыть нижнюю панель (Cmd+Alt+L)");
inline const juce::String showDetail = utf8 ("Показать нижнюю панель (Cmd+Alt+L)");
inline const juce::String hideMixer = utf8 ("Скрыть микшер (Cmd+Alt+M)");
inline const juce::String showMixer = utf8 ("Показать микшер (Cmd+Alt+M)");
} // namespace tips
} // namespace Strings
