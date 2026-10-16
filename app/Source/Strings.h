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
inline const juce::String dialogPluginTitle = utf8 ("Плагин");
inline const juce::String windowPlugins = utf8 ("Плагины (AU / VST3)");
inline const juce::String windowAudioSettings = utf8 ("Настройки звука и MIDI");
} // namespace Strings
