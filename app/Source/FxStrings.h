// Names of the built-in effects (docs/spec/06-effects-mixing.md), their parameters, choices, units and categories.
// The effects area (core/src/fx, see docs/agents/EFFECTS.md) owns this file: a new effect adds its Russian names here and
// nothing in Strings.h. Ids come from core; whatever has no translation here shows the English name from core. In
// English, parameters, choices and units come from core as they are.
#pragma once

#include "Strings.h"

#include "daw/fx/Effect.h"

namespace Strings
{
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
    if (const auto it = map.find (type); it != map.end())
        return utf8 (it->second);
    for (const auto& t : daw::fx::effectTypes()) // a new effect shows its English name from core until it gets a translation
        if (type == juce::String (t.id))
            return juce::String (t.name);
    return type;
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
} // namespace Strings
