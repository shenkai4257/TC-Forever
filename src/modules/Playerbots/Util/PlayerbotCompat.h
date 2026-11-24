/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTCOMPAT_H
#define PLAYERBOTS_PLAYERBOTCOMPAT_H

#include "Define.h"
#include "RaceMask.h"

// 12.x dropped the 3.3.5 DEFAULT_MAX_LEVEL compile-time constant. This fork runs the
// classic (1.60) client with a level cap of 60, which is what the ported caches need.
inline constexpr uint32 DEFAULT_MAX_LEVEL = 60;

namespace PlayerbotCompat
{
    // RACEMASK_ALL_PLAYABLE in 12.x covers the retail race set; the ported item filters
    // were written against the ten classic races, so keep them explicit here.
    inline constexpr Trinity::RaceMask<uint64> ClassicPlayableRaces =
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_HUMAN) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_ORC) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_DWARF) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_NIGHTELF) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_UNDEAD_PLAYER) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_TAUREN) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_GNOME) |
        Trinity::RaceMask<uint64>::GetMaskForRace(RACE_TROLL);

    inline bool HasClassicPlayableRace(Trinity::RaceMask<int32, 2> mask)
    {
        return !(static_cast<Trinity::RaceMask<uint64>>(mask) & ClassicPlayableRaces).IsEmpty();
    }
}

#endif
