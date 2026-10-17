#include "daw/project/VersionHistory.h"

#include <algorithm>
#include <map>

namespace daw::project
{
std::vector<int> versionsToPrune (const std::vector<VersionInfo>& versions)
{
    std::vector<const VersionInfo*> autosaves;
    for (const auto& v : versions)
        if (v.kind == VersionKind::autosave && ! v.labelled)
            autosaves.push_back (&v);

    // newest first; the number breaks ties of equal times
    std::sort (autosaves.begin(), autosaves.end(), [] (const VersionInfo* a, const VersionInfo* b) {
        return a->timeMs != b->timeMs ? a->timeMs > b->timeMs : a->number > b->number;
    });

    constexpr int64_t kHourMs = 3600LL * 1000LL;
    const auto hourOf = [] (int64_t ms) { return ms >= 0 ? ms / kHourMs : -((-ms + kHourMs - 1) / kHourMs); }; // floor, also before 1970

    std::vector<int> remove;
    std::map<int64_t, bool> hourTaken;
    for (size_t i = 0; i < autosaves.size(); ++i)
    {
        if ((int) i < kRecentAutosavesKept)
            continue;
        auto [it, inserted] = hourTaken.emplace (hourOf (autosaves[i]->timeMs), true);
        if (! inserted)
            remove.push_back (autosaves[i]->number); // an older one of the same hour is already kept
    }
    std::sort (remove.begin(), remove.end());
    return remove;
}
} // namespace daw::project
