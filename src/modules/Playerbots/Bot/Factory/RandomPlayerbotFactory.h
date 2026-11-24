/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RANDOMPLAYERBOTFACTORY_H
#define PLAYERBOTS_RANDOMPLAYERBOTFACTORY_H

#include "Battleground.h"
#include "Common.h"
#include "DBCEnums.h"
#include "SharedDefines.h"
#include <atomic>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class WorldSession;

class RandomPlayerbotFactory
{
public:
    enum class NameRaceAndGender : uint8
    {
        // Generic is the category used for human & undead
        GenericMale = 0,
        GenericFemale,
        GnomeMale,
        GnomeFemale,
        DwarfMale,
        DwarfFemale,
        NightelfMale,
        NightelfFemale,
        DraeneiMale,
        DraeneiFemale,
        OrcMale,
        OrcFemale,
        TrollMale,
        TrollFemale,
        TaurenMale,
        TaurenFemale,
        BloodelfMale,
        BloodelfFemale
    };

    static constexpr NameRaceAndGender CombineRaceAndGender(uint8 race, uint8 gender);

    RandomPlayerbotFactory() {};
    virtual ~RandomPlayerbotFactory() {}

    Player* CreateRandomBot(WorldSession* session, uint8 cls, std::unordered_map<NameRaceAndGender, std::vector<std::string>>& names);
    static void CreateRandomBots();
    static std::string const CreateRandomGuildName();
    static uint32 CalculateTotalAccountCount();
    static uint32 CalculateAvailableCharsPerAccount();
    static void LoadLocalizedNameColumns();
    static std::string GetGuildNameSelector(std::string const& column);


private:

    static bool IsValidRaceClassCombination(uint8 race, uint8 class_, uint32 expansion);
    std::string const CreateRandomBotName(NameRaceAndGender raceAndGender);

    static std::string GetLocalizedNameSelector(bool localized, std::string const& column);

    // Whether the name table has a column for the default DBC locale
    static inline std::atomic<bool> _guildNamesLocalized{false};
};

#endif
