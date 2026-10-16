#pragma once

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
inline const juce::String addFolderTitle = utf8 ("Выберите папку для «Мест»");
inline const juce::String removeFolder = utf8 ("Убрать из «Мест»");
inline const juce::String noPlaces = utf8 ("Добавьте папку со звуками и MIDI-файлами");
inline const juce::String previewToggle = utf8 ("Предпрослушивание");
inline const juce::String labelColour = utf8 ("Цвет метки");
inline const juce::String labelNone = utf8 ("Без метки");
inline const juce::String renamePreset = utf8 ("Переименовать…");
inline const juce::String deletePreset = utf8 ("Удалить…");
inline const juce::String showInFinder = utf8 ("Показать в Finder");
inline const juce::String useOnTrack = utf8 ("Поставить на выбранную дорожку");
} // namespace browser

// Detail panel and device chain
namespace detail
{
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
namespace help
{
inline juce::String make (const juce::String& title, const juce::String& text) { return title + "\n" + text; }
inline const juce::String soon2 = utf8 ("Появится на этапе 2");
inline const juce::String soon3 = utf8 ("Появится на этапе 3");
inline const juce::String soon6 = utf8 ("Появится на этапе 6");
inline const juce::String soonFiles = utf8 ("Появится вместе с файлами проекта");

inline const juce::String logo = make (utf8 ("Призма"), utf8 ("Главное окно DAW. Цвета дорожек идут по спектру слева направо."));
inline const juce::String tempo = make (utf8 ("Темп"), utf8 ("Ударов в минуту. Тяните мышью, колесо меняет, двойной щелчок вводит число. Shift: точнее."));
inline const juce::String signature = make (utf8 ("Размер"), utf8 ("Размер такта проекта. Менять его можно будет в настройках проекта."));
inline const juce::String metronome = make (utf8 ("Метроном"), soon2);
inline const juce::String play = make (utf8 ("Играть"), utf8 ("Запускает и останавливает воспроизведение. Пробел делает то же самое."));
inline const juce::String stop = make (utf8 ("Стоп"), utf8 ("Останавливает воспроизведение и все клипы. Повторное нажатие возвращает в начало."));
inline const juce::String record = make (utf8 ("Запись"), soon2);
inline const juce::String position = make (utf8 ("Позиция"), utf8 ("Такты, доли и шестнадцатые от начала воспроизведения."));
inline const juce::String loop = make (utf8 ("Петля"), soon3);
inline const juce::String ribbon = make (utf8 ("Лента «10 мин»"), utf8 ("Последние 10 минут игры всегда в памяти. ") + soon2);
inline const juce::String morph = make (utf8 ("Морф A ↔ B"), utf8 ("Плавный переход между снимками ручек. ") + soon6);
inline const juce::String tabSession = make (utf8 ("Session"), utf8 ("Сетка клипов для живой игры."));
inline const juce::String tabArrangement = make (utf8 ("Arrangement"), soon3);
inline const juce::String version = make (utf8 ("История версий"), soonFiles);
inline const juce::String cpu = make (utf8 ("Нагрузка процессора"), utf8 ("Щелчок открывает настройки звука."));
inline const juce::String midiIn = make (utf8 ("MIDI-вход"), utf8 ("Мигает, когда приходят события с MIDI-устройств."));
inline const juce::String keysIndicator = make (utf8 ("Клавиатура компьютера"), utf8 ("Когда горит, буквенные клавиши играют ноты. Включается клавишей M."));

inline const juce::String browserPanel = make (utf8 ("Браузер"), utf8 ("Инструменты, пресеты, эффекты и звуки. Наполнение появится в следующей части этапа 1."));
inline const juce::String browserSearch = make (utf8 ("Поиск"), utf8 ("Ищет по всем категориям сразу. Cmd+F переводит сюда курсор, Esc очищает."));
inline juce::String browserCategory (const juce::String& name) { return make (name, utf8 ("Категория браузера. Источники появятся в следующей части этапа 1.")); }

inline const juce::String trackHeader = make (utf8 ("Заголовок дорожки"), utf8 ("Щелчок выделяет дорожку, двойной щелчок переименовывает, правая кнопка открывает меню. Правый край тянется, меняя ширину столбца."));
inline const juce::String clipSlot = make (utf8 ("Клип"), utf8 ("Щелчок запускает, двойной щелчок по пустому слоту создаёт клип. Клип можно перетащить в другой слот (Alt: копия)."));
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
inline const juce::String devicePresets = make (utf8 ("Пресеты устройства"), soonFiles);
inline const juce::String devicePresetsInstrument = make (utf8 ("Пресеты инструмента"), utf8 ("Пресеты Prisma и замена инструмента. Пользовательские пресеты появятся в следующей части этапа 1."));
inline const juce::String deviceFold = make (utf8 ("Свернуть"), utf8 ("Сворачивает устройство в узкую полосу."));
inline const juce::String deviceOpen = make (utf8 ("Открыть в окне"), utf8 ("Полный редактор устройства в отдельном окне."));
inline const juce::String deviceClose = make (utf8 ("Удалить устройство"), utf8 ("Убирает устройство из цепочки. Cmd+Z возвращает."));
inline const juce::String deviceHeader = make (utf8 ("Заголовок устройства"), utf8 ("Тяните, чтобы переставить устройство. Delete удаляет выделенное."));
inline const juce::String deviceKnob = make (utf8 ("Параметр устройства"), utf8 ("Тяните, колесо меняет, двойной щелчок возвращает значение по умолчанию. Shift: точнее."));
inline const juce::String wavetable = make (utf8 ("Wavetable"), utf8 ("Форма волны выбранной таблицы и положение внутри неё."));
inline const juce::String missingDevice = make (utf8 ("Плагин не найден"), utf8 ("Состояние сохранено и вернётся, когда плагин появится. Звук идёт насквозь."));
inline const juce::String dropZone = make (utf8 ("Конец цепочки"), utf8 ("Сюда можно будет перетащить эффект или плагин из браузера."));
inline const juce::String detailInfo = make (utf8 ("Выбранная дорожка"), utf8 ("Вход, мониторинг и число устройств дорожки."));
inline const juce::String infoBar = make (utf8 ("Строка подсказок"), utf8 ("Показывает название и действие элемента под мышью."));
} // namespace help
} // namespace Strings
