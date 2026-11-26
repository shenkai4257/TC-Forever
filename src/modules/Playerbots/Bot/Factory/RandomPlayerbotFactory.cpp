/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RandomPlayerbotFactory.h"
#include "FieldHelper.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "DB2Stores.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "DatabaseEnv.h"
#include "LocaleColumns.h"
#include "Log.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SocialMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "World.h"

void RandomPlayerbotFactory::LoadLocalizedNameColumns()
{
    LocaleConstant const locale = sWorld->GetDefaultDbcLocale();
    auto hasDefaultLocaleColumn = [locale](std::string const& table)
    {
        for (LocaleColumn const& localeColumn : GetLocaleColumns(CharacterDatabase, table,
                 [](LocaleConstant columnLocale) { return std::string("name_") + localeNames[columnLocale]; }))
        {
            if (localeColumn.Locale == locale)
                return true;
        }

        return false;
    };

    _guildNamesLocalized = hasDefaultLocaleColumn("playerbots_guild_names");
}

std::string RandomPlayerbotFactory::GetGuildNameSelector(std::string const& column)
{
    return GetLocalizedNameSelector(_guildNamesLocalized, column);
}


std::string RandomPlayerbotFactory::GetLocalizedNameSelector(bool localized, std::string const& column)
{
    if (!localized)
        return column;

    LocaleConstant const locale = sWorld->GetDefaultDbcLocale();

    // Fall back to the enUS column when the localized one is NULL or empty
    return Trinity::StringFormat("COALESCE(NULLIF({}_{}, ''), {})", column, localeNames[locale], column);
}

constexpr RandomPlayerbotFactory::NameRaceAndGender RandomPlayerbotFactory::CombineRaceAndGender(uint8 race,
                                                                                                uint8 gender)
{
    NameRaceAndGender baseIndex;
    switch (race)
    {
        case RACE_ORC:        baseIndex = NameRaceAndGender::OrcMale; break;
        case RACE_DWARF:      baseIndex = NameRaceAndGender::DwarfMale; break;
        case RACE_NIGHTELF:   baseIndex = NameRaceAndGender::NightelfMale; break;
        case RACE_TAUREN:     baseIndex = NameRaceAndGender::TaurenMale; break;
        case RACE_GNOME:      baseIndex = NameRaceAndGender::GnomeMale; break;
        case RACE_TROLL:      baseIndex = NameRaceAndGender::TrollMale; break;
        case RACE_BLOODELF:   baseIndex = NameRaceAndGender::BloodelfMale; break;
        case RACE_DRAENEI:    baseIndex = NameRaceAndGender::DraeneiMale; break;
        case RACE_HUMAN:
        case RACE_UNDEAD_PLAYER:
        default:
            baseIndex = NameRaceAndGender::GenericMale;
            break;
    }

    return static_cast<NameRaceAndGender>(static_cast<uint8>(baseIndex) +
                                          ((gender >= GENDER_NONE) ? static_cast<uint8>(GENDER_MALE) : gender));
}

bool RandomPlayerbotFactory::IsValidRaceClassCombination(uint8 race, uint8 cls, uint32 expansion)
{
    // only the races a real client could create (see IsBotRaceAllowed)
    if (!IsBotRaceAllowed(race))
        return false;

    // only the race/class pairs the client's character creation offers (CharBaseInfo): the world
    // database's playercreateinfo still lists every retail combination
    if (!sDB2Manager.GetCharBaseInfo(Races(race), Classes(cls)))
        return false;

    // skip expansion races if not playing with expansion
    if (expansion < EXPANSION_THE_BURNING_CRUSADE && (race == RACE_BLOODELF || race == RACE_DRAENEI))
        return false;

    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls);
    return info != nullptr;
}

Player* RandomPlayerbotFactory::CreateRandomBot(WorldSession* session, uint8 cls, std::unordered_map<NameRaceAndGender, std::vector<std::string>>& nameCache)
{
    TC_LOG_DEBUG("playerbots", "Creating a new random bot for class: {}", cls);

    const bool alliance = static_cast<bool>(urand(0, 1));
    Trinity::RaceMask<uint64> const disabledRaces{ sWorld->GetUInt64Config(CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK) };

    std::vector<uint8> raceOptions;
    for (ChrRacesEntry const* raceEntry : sChrRacesStore)
    {
        uint8 const race = uint8(raceEntry->ID);

        // skip disabled with config races (the race mask is a 64-bit world config in 12.x)
        if (disabledRaces.HasRace(race))
            continue;

        // Try to get 50/50 faction distribution for random bot population balance.
        // Without this check, races from the faction with more class options would dominate.
        if (Player::TeamIdForRace(race) != (alliance ? TEAM_ALLIANCE : TEAM_HORDE))
            continue;

        if (IsValidRaceClassCombination(race, cls, sWorld->getIntConfig(CONFIG_EXPANSION)))
            raceOptions.push_back(race);
    }

    if (raceOptions.empty())
    {
        TC_LOG_ERROR("playerbots", "No races are available for class: {}", cls);
        return nullptr;
    }

    const uint8 race = raceOptions[urand(0, raceOptions.size() - 1)];
    const uint8 gender = urand(0, 1) ? GENDER_MALE : GENDER_FEMALE;
    const auto raceAndGender = CombineRaceAndGender(race, gender);

    std::string name;
    if (!nameCache.empty())
    {
        if (nameCache[raceAndGender].empty())
        {
            TC_LOG_ERROR("playerbots", "No names found for the specified race: {} and gender: {}",
                    race, gender);
            return nullptr;
        }

        uint32 i = urand(0, nameCache[raceAndGender].size() - 1);
        name = nameCache[raceAndGender][i];
        swap(nameCache[raceAndGender][i], nameCache[raceAndGender].back());
        nameCache[raceAndGender].pop_back();
    }
    else
    {
        name = CreateRandomBotName(raceAndGender);
    }

    if (name.empty())
    {
        TC_LOG_ERROR("playerbots", "Failed to get a valid random bot name");
        return nullptr;
    }

    // 1.60 appearance lives in ChrCustomization: the 3.3.5 CharSections table is gone.
    // Pick a random choice per option the race/gender supports; options or choices that
    // carry a requirement are skipped so Create()'s appearance validation cannot fail.
    std::unique_ptr<WorldPackets::Character::CharacterCreateInfo> characterInfo =
        std::make_unique<WorldPackets::Character::CharacterCreateInfo>();
    characterInfo->Name = name;
    characterInfo->Race = race;
    characterInfo->Class = cls;
    characterInfo->Sex = gender;

    if (std::vector<ChrCustomizationOptionEntry const*> const* options = sDB2Manager.GetCustomiztionOptions(race, gender))
    {
        size_t slot = 0;
        for (ChrCustomizationOptionEntry const* option : *options)
        {
            if (slot >= characterInfo->Customizations.size())
                break;

            if (option->ChrCustomizationReqID && sChrCustomizationReqStore.LookupEntry(option->ChrCustomizationReqID))
                continue;

            std::vector<ChrCustomizationChoiceEntry const*> const* choices = sDB2Manager.GetCustomiztionChoices(option->ID);
            if (!choices || choices->empty())
                continue;

            ChrCustomizationChoiceEntry const* choice = (*choices)[urand(0, choices->size() - 1)];
            if (!choice || (choice->ChrCustomizationReqID && sChrCustomizationReqStore.LookupEntry(choice->ChrCustomizationReqID)))
                continue;

            characterInfo->Customizations[slot].ChrCustomizationOptionID = option->ID;
            characterInfo->Customizations[slot].ChrCustomizationChoiceID = choice->ID;
            ++slot;
        }
    }

    Player* player = new Player(session);
    player->GetMotionMaster()->Initialize();
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), characterInfo.get()))
    {
        player->CleanupsBeforeDelete();
        delete player;

        TC_LOG_ERROR("playerbots", "Unable to create random bot - name: \"{}\", race: {}, class: {}",
                name.c_str(), race, cls);
        return nullptr;
    }

    player->setCinematic(2);
    player->SetAtLoginFlag(AT_LOGIN_NONE);

    TC_LOG_DEBUG("playerbots", "Random bot created - name: \"{}\", race: {}, class: {}",
            name.c_str(), race, cls);

    return player;
}

std::string const RandomPlayerbotFactory::CreateRandomBotName(NameRaceAndGender raceAndGender)
{
    std::string botName = "";
    int tries = 3;
    while (--tries)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT n.name "
            "FROM playerbots_names n "
            "LEFT OUTER JOIN characters c ON c.name = n.name "
            "WHERE c.guid IS NULL and n.gender = '{}' "
            "ORDER BY RAND() LIMIT 1",
            static_cast<uint8>(raceAndGender));
        if (!result)
        {
            break;
        }

        Field* fields = result->Fetch();
        botName = PlayerbotFieldGet<std::string>(fields[0]);
        if (ObjectMgr::CheckPlayerName(botName, sWorld->GetDefaultDbcLocale(), true) == CHAR_NAME_SUCCESS)  // Checks for reservation & profanity, too
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
            PlayerbotSetData(stmt, 0, botName);

            if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
                continue;

            return botName;
        }
    }

    // CONLANG NAME GENERATION
    TC_LOG_ERROR("playerbots", "No more names left for random bots. Attempting conlang name generation.");
    const std::string groupCategory = "SCVKRU";
    const std::string groupFormStart[2][4] = {{"SV", "SV", "VK", "RV"}, {"V", "SU", "VS", "RV"}};
    const std::string groupFormMid[2][6] = {{"CV", "CVC", "CVC", "CVK", "VC", "VK"},
                                            {"CV", "CVC", "CVK", "KVC", "VC", "KV"}};
    const std::string groupFormEnd[2][4] = {{"CV", "VC", "VK", "CV"}, {"RU", "UR", "VR", "V"}};
    const std::string groupLetter[2][6] = {
        // S           C                            V               K           R         U
        {"dtspkThfS", "bcCdfghjkmnNqqrrlsStTvwxyz", "aaeeiouA", "ppttkkbdg", "lmmnrr", "AEO"},
        {"dtskThfS", "bcCdfghjkmmnNqrrlssStTvwyz", "aaaeeiiuAAEIO", "ppttkbbdg", "lmmnrrr", "AEOy"}};
    const std::string replaceRule[2][17] = {
        {"ST", "ka", "ko", "ku", "kr", "S", "T", "C", "N", "jj", "AA", "AI", "A", "E", "O", "I", "aa"},
        {"sth", "ca", "co", "cu", "cr", "sh", "th", "ch", "ng", "dg", "A", "ayu", "ai", "ei", "ou", "iu", "ae"}};

    const auto gender = static_cast<uint8>(raceAndGender) % 2;

    tries = 10;
    while (--tries)
    {
        botName.clear();
        // Build name from groupForms
        // Pick random start group
        botName = groupFormStart[gender][rand() % 4];
        // Pick up to 2 and then up to 1 additional middle group
        for (int i = 0; i < rand() % 3 + rand() % 2; i++)
        {
            botName += groupFormMid[gender][rand() % 6];
        }
        // Pick up to 1 end group
        botName += rand() % 2 ? groupFormEnd[gender][rand() % 4] : "";
        // If name is single letter add random end group
        botName += (botName.size() < 2) ? groupFormEnd[gender][rand() % 4] : "";

        // Replace Catagory value with random Letter from that Catagory's Letter string for a given bot gender
        for (size_t i = 0; i < botName.size(); i++)
        {
            botName[i] = groupLetter[gender][groupCategory.find(botName[i])]
                                    [rand() % groupLetter[gender][groupCategory.find(botName[i])].size()];
        }

        // Itterate over replace rules
        for (int i = 0; i < 17; i++)
        {
            int j = botName.find(replaceRule[0][i]);
            while (j > -1)
            {
                botName.replace(j, replaceRule[0][i].size(), replaceRule[1][i]);
                j = botName.find(replaceRule[0][i]);
            }
        }

        // Capitalize first letter
        botName[0] -= 32;

        if (ObjectMgr::CheckPlayerName(botName, sWorld->GetDefaultDbcLocale(), true) != CHAR_NAME_SUCCESS) // Checks for reservation & profanity, too
        {
            botName.clear();
            continue;
        }
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
        PlayerbotSetData(stmt, 0, botName);

        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            botName.clear();
            continue;
        }
        return botName;
    }

    // TRUE RANDOM NAME GENERATION
    TC_LOG_ERROR("playerbots", "Con​lang name generation failed. True random name fallback.");
    tries = 10;
    while (--tries)
    {
        for (uint8 i = 0; i < 10; i++)
        {
            botName += (i == 0 ? 'A' : 'a') + rand() % 26;
        }
        if (ObjectMgr::CheckPlayerName(botName, sWorld->GetDefaultDbcLocale(), true) != CHAR_NAME_SUCCESS)  // Checks for reservation & profanity, too
        {
            botName.clear();
            continue;
        }
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
        PlayerbotSetData(stmt, 0, botName);

        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            botName.clear();
            continue;
        }
        return botName;
    }
    TC_LOG_ERROR("playerbots", "Random name generation failed.");
    botName.clear();
    return botName;
}

// Calculates the total number of required accounts, either using the specified randomBotAccountCount
// or determining it dynamically based on MaxRandomBots and AddClassAccountPoolSize.
// The system also factors in the types of existing account, as assigned by AssignAccountTypes().
uint32 RandomPlayerbotFactory::CalculateTotalAccountCount()
{
    // Reset account types if features are disabled
    // Reset is done here to precede needed accounts calculations
    if (sPlayerbotAIConfig.MaxRandomBots == 0 || sPlayerbotAIConfig.AddClassAccountPoolSize == 0)
    {
        if (sPlayerbotAIConfig.MaxRandomBots == 0)
        {
            CharacterDatabase.DirectPExecute("UPDATE playerbots_account_type SET account_type = 0 WHERE account_type = {}", uint8(1));
            TC_LOG_INFO("playerbots", "MaxRandomBots set to 0, any RNDbot accounts (type 1) will be unassigned (type 0)");
        }
        if (sPlayerbotAIConfig.AddClassAccountPoolSize == 0)
        {
            CharacterDatabase.DirectPExecute("UPDATE playerbots_account_type SET account_type = 0 WHERE account_type = {}", uint8(2));
            TC_LOG_INFO("playerbots", "AddClassAccountPoolSize set to 0, any AddClass accounts (type 2) will be unassigned (type 0)");
        }

        // Wait for DB to reflect the change, up to 1 second max. This is needed to make sure other logs don't show wrong info
        for (int waited = 0; waited < 1000; waited += 50)
        {
            QueryResult res = CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM playerbots_account_type WHERE account_type IN ({}, {})",
                int32(sPlayerbotAIConfig.MaxRandomBots == 0 ? 1 : -1),
                int32(sPlayerbotAIConfig.AddClassAccountPoolSize == 0 ? 2 : -1));

            if (!res || PlayerbotFieldGet<uint64>(res->Fetch()[0]) == 0)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(50));     // Extra 50ms fixed delay for safety.
        }
    }

    // Check existing account types
    uint32 existingRndBotAccounts = 0;
    uint32 existingAddClassAccounts = 0;
    uint32 existingUnassignedAccounts = 0;

    QueryResult typeCheck = CharacterDatabase.Query(
        "SELECT account_type, COUNT(*) FROM playerbots_account_type GROUP BY account_type");
    if (typeCheck)
    {
        do
        {
            Field* fields = typeCheck->Fetch();
            uint8 accountType = PlayerbotFieldGet<uint8>(fields[0]);
            uint32 count = static_cast<uint32>(PlayerbotFieldGet<uint64>(fields[1]));

            if (accountType == 0) existingUnassignedAccounts = count;
            else if (accountType == 1) existingRndBotAccounts = count;
            else if (accountType == 2) existingAddClassAccounts = count;
        } while (typeCheck->NextRow());
    }

    // Determine divisor based on the requested A&H faction ratio
    int divisor = CalculateAvailableCharsPerAccount();

    // Calculate max bots
    int maxBots = sPlayerbotAIConfig.MaxRandomBots;

    // Calculate number of accounts needed for RNDbots
    // Result is rounded up for maxBots not cleanly divisible by the divisor
    uint32 neededRndBotAccounts = (maxBots + divisor - 1) / divisor;
    uint32 neededAddClassAccounts = sPlayerbotAIConfig.AddClassAccountPoolSize;

    // Start with existing total
    uint32 existingTotal = existingRndBotAccounts + existingAddClassAccounts + existingUnassignedAccounts;

    // Calculate shortfalls after using unassigned accounts
    uint32 availableUnassigned = existingUnassignedAccounts;
    uint32 additionalAccountsNeeded = 0;

    // Check RNDbot needs
    if (neededRndBotAccounts > existingRndBotAccounts)
    {
        uint32 rndBotShortfall = neededRndBotAccounts - existingRndBotAccounts;
        if (rndBotShortfall <= availableUnassigned)
            availableUnassigned -= rndBotShortfall;
        else
        {
            additionalAccountsNeeded += (rndBotShortfall - availableUnassigned);
            availableUnassigned = 0;
        }
    }

    // Check AddClass needs
    if (neededAddClassAccounts > existingAddClassAccounts)
    {
        uint32 addClassShortfall = neededAddClassAccounts - existingAddClassAccounts;
        if (addClassShortfall <= availableUnassigned)
            availableUnassigned -= addClassShortfall;
        else
        {
            additionalAccountsNeeded += (addClassShortfall - availableUnassigned);
            availableUnassigned = 0;
        }
    }

    // Return existing total plus any additional accounts needed
    uint32 calculatedTotal = existingTotal + additionalAccountsNeeded;

    // Manually set randomBotAccountCount meets the requirements
    if (sPlayerbotAIConfig.RandomBotAccountCount >= calculatedTotal)
        return sPlayerbotAIConfig.RandomBotAccountCount;
    // Manually set randomBotAccountCount doesn't meet the requirements. Using calculated value
    if (sPlayerbotAIConfig.RandomBotAccountCount > 0)
        TC_LOG_WARN("playerbots", "RandomBotAccountCount ({}) is lower than the required calculated value ({}). Using the calculated value instead.",
            sPlayerbotAIConfig.RandomBotAccountCount, calculatedTotal);

    return calculatedTotal;
}

uint32 RandomPlayerbotFactory::CalculateAvailableCharsPerAccount()
{
    uint32 availableChars = 9;

    uint32 hordeRatio = sPlayerbotAIConfig.RandomBotHordeRatio;
    uint32 allianceRatio = sPlayerbotAIConfig.RandomBotAllianceRatio;

    // horde : alliance = 50 : 50 -> 0%
    // horde : alliance = 0 : 50 -> 50%
    // horde : alliance = 10 : 50 -> 40%
    float unavailableRatio = static_cast<float>((std::max(hordeRatio, allianceRatio) - std::min(hordeRatio, allianceRatio))) /
        (std::max(hordeRatio, allianceRatio) * 2);

    // Conservative floor to ensure enough characters (may result in more accounts than needed).
    if (unavailableRatio != 0)
        availableChars = availableChars - availableChars * unavailableRatio;

    return availableChars;
}

void RandomPlayerbotFactory::CreateRandomBots()
{
    /* multi-thread here is meaningless? since the async db operations */

    if (sPlayerbotAIConfig.DeleteRandomBotAccounts)
    {
        // Collect bot account ids from the login database so the cleanup below
        // never needs a cross-database subquery (the login database may live on
        // a different server than the character database)
        std::vector<uint32> botAccounts;
        QueryResult accountResult = LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '{}%' ORDER BY id",
            sPlayerbotAIConfig.RandomBotAccountPrefix.c_str());
        if (accountResult)
        {
            do
            {
                botAccounts.push_back(accountResult->Fetch()->GetUInt32());
            } while (accountResult->NextRow());
        }

        std::string botAccountIds;
        for (uint32 accountId : botAccounts)
        {
            if (!botAccountIds.empty())
                botAccountIds += ", ";
            botAccountIds += std::to_string(accountId);
        }
        if (botAccountIds.empty())
            botAccountIds = "0";    // account ids start at 1, so this matches nothing

        TC_LOG_INFO("playerbots", "Deleting all random bot characters and accounts ({} bot accounts found)...", botAccounts.size());

        // First execute all the cleanup SQL commands
        // Clear playerbots_random_bots and playerbots_account_type
        CharacterDatabase.DirectExecute("DELETE FROM playerbots_random_bots");
        CharacterDatabase.DirectExecute("DELETE FROM playerbots_account_type");

        // Get the character database name dynamically (used in same-server subqueries below)
        std::string characterDBName = CharacterDatabase.GetConnectionInfo()->database;

        // Delete every character that does not belong to a player account. Deleting by the bot
        // account list alone leaves the characters of earlier pools behind once their account has
        // been deleted: a reused bot account id then makes them belong to a new account again, and
        // the pool is "recreated" with the old skills, spells and riding still on it.
        QueryResult playerAccountResult = LoginDatabase.PQuery("SELECT id FROM account WHERE username NOT LIKE '{}%'",
            sPlayerbotAIConfig.RandomBotAccountPrefix.c_str());
        std::string playerAccountIds;
        if (playerAccountResult)
        {
            do
            {
                if (!playerAccountIds.empty())
                    playerAccountIds += ", ";
                playerAccountIds += std::to_string(playerAccountResult->Fetch()->GetUInt32());
            } while (playerAccountResult->NextRow());
        }
        if (playerAccountIds.empty())
            playerAccountIds = "0";    // account ids start at 1, so this keeps every character

        CharacterDatabase.Execute(("DELETE FROM characters WHERE account NOT IN (" + playerAccountIds + ")").c_str());

        // Wait for the characters to be deleted before proceeding to dependent deletes
        while (CharacterDatabase.QueueSize())
        {
            std::this_thread::sleep_for(1s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));    // Extra 100ms fixed delay for safety.

        // Clean up orphaned entries in playerbots_guild_tasks
        CharacterDatabase.DirectExecute(("DELETE FROM playerbots_guild_tasks WHERE owner NOT IN (SELECT guid FROM " + characterDBName + ".characters)").c_str());

        // Clean up orphaned entries in playerbots_db_store (explicit id list, no cross-database subquery)
        CharacterDatabase.DirectExecute(("DELETE FROM playerbots_db_store WHERE guid NOT IN (SELECT guid FROM " + characterDBName + ".characters WHERE account NOT IN (" + botAccountIds + "))").c_str());

        // Clean up orphaned records in character-related tables
        CharacterDatabase.PExecute("DELETE FROM character_account_data WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_achievement WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_achievement_progress WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_action WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_aura WHERE guid NOT IN (SELECT guid FROM characters)");
        // 12.x has no character_entry_point table (the 3.3.5 per-character entry point was dropped).
        CharacterDatabase.PExecute("DELETE FROM character_glyphs WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_homebind WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE owner_guid NOT IN (SELECT guid FROM characters) AND owner_guid > 0");

        // Clean up pet data
        CharacterDatabase.PExecute("DELETE FROM character_pet WHERE owner NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM pet_aura WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.PExecute("DELETE FROM pet_spell WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.PExecute("DELETE FROM pet_spell_cooldown WHERE guid NOT IN (SELECT id FROM character_pet)");

        // Clean up character data
        CharacterDatabase.PExecute("DELETE FROM character_queststatus WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_queststatus_rewarded WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_reputation WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_skills WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_social WHERE friend NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_spell WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_spell_cooldown WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM character_talent WHERE guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM corpse WHERE guid NOT IN (SELECT guid FROM characters)");

        // Clean up group data
        CharacterDatabase.PExecute("DELETE FROM `groups` WHERE leaderGuid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM group_member WHERE memberGuid NOT IN (SELECT guid FROM characters)");

        // Clean up mail
        CharacterDatabase.PExecute("DELETE FROM mail WHERE receiver NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM mail_items WHERE receiver NOT IN (SELECT guid FROM characters)");

        // Clean up guild data
        CharacterDatabase.PExecute("DELETE FROM guild WHERE leaderguid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM guild_bank_eventlog WHERE guildid NOT IN (SELECT guildid FROM guild)");
        CharacterDatabase.PExecute("DELETE FROM guild_member WHERE guildid NOT IN (SELECT guildid FROM guild) OR guid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM guild_rank WHERE guildid NOT IN (SELECT guildid FROM guild)");

        // Clean up petition data
        CharacterDatabase.PExecute("DELETE FROM petition WHERE ownerguid NOT IN (SELECT guid FROM characters)");
        CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE ownerguid NOT IN (SELECT guid FROM characters) OR playerguid NOT IN (SELECT guid FROM characters)");

        // Finally, delete the bot accounts themselves
        TC_LOG_INFO("playerbots", "Deleting random bot accounts...");
        QueryResult results = LoginDatabase.PQuery("SELECT id FROM account WHERE username LIKE '{}%'",
                                             sPlayerbotAIConfig.RandomBotAccountPrefix.c_str());
        int32 deletion_count = 0;
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                uint32 accId = PlayerbotFieldGet<uint32>(fields[0]);
                TC_LOG_DEBUG("playerbots", "Deleting account accID: {}({})...", accId, ++deletion_count);
                AccountMgr::DeleteAccount(accId);
            } while (results->NextRow());
        }

        uint32 timer = getMSTime();

        // After ALL deletions, make sure data is commited to DB
        LoginDatabase.PExecute("COMMIT");
        CharacterDatabase.PExecute("COMMIT");

        // Wait for all pending database operations to complete
        while (LoginDatabase.QueueSize() || CharacterDatabase.QueueSize() || WorldDatabase.QueueSize())
        {
            std::this_thread::sleep_for(1s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));    // Extra 100ms fixed delay for safety.

        // Flush tables to ensure all data in memory are written to disk
        LoginDatabase.PExecute("FLUSH TABLES");
        CharacterDatabase.PExecute("FLUSH TABLES");
        CharacterDatabase.DirectExecute("FLUSH TABLES");

        TC_LOG_INFO("playerbots", ">> Random bot accounts and data deleted in {} ms", GetMSTimeDiffToNow(timer));
        TC_LOG_INFO("playerbots", "Please reset the Playerbots.DeleteRandomBotAccounts to 0 and restart the server...");
        World::StopNow(SHUTDOWN_EXIT_CODE);
        return;
    }

    TC_LOG_INFO("playerbots", "Creating random bot accounts...");
    std::unordered_map<NameRaceAndGender, std::vector<std::string>> nameCache;
    std::vector<std::future<void>> account_creations;
    int account_creation = 0;

    // Calculates the total number of required accounts.
    uint32 totalAccountCount = CalculateTotalAccountCount();
    uint32 timer = getMSTime();

    for (uint32 accountNumber = 0; accountNumber < totalAccountCount; ++accountNumber)
    {
        std::ostringstream out;
        out << sPlayerbotAIConfig.RandomBotAccountPrefix << accountNumber;
        std::string const accountName = out.str();

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME);
        PlayerbotSetData(stmt, 0, accountName);
        PreparedQueryResult result = LoginDatabase.Query(stmt);
        if (result)
        {
            continue;
        }
        account_creation++;
        std::string password = "";
        if (sPlayerbotAIConfig.RandomBotRandomPassword)
        {
            for (int i = 0; i < 10; i++)
            {
                password += (char)urand('!', 'z');
            }
        }
        else
            password = accountName;

        sAccountMgr->CreateAccount(accountName, password);

        TC_LOG_DEBUG("playerbots", "Account {} created for random bots", accountName.c_str());
    }
    if (account_creation)
    {
        TC_LOG_INFO("playerbots", "Waiting for {} accounts loading into database ({} queries)...", account_creation, LoginDatabase.QueueSize());
        /* wait for async accounts create to make character create correctly */

        while (LoginDatabase.QueueSize())
        {
            std::this_thread::sleep_for(1s);
        }
        TC_LOG_INFO("playerbots", ">> {} Accounts loaded into database in {} ms", account_creation, GetMSTimeDiffToNow(timer));
    }

    TC_LOG_INFO("playerbots", "Creating random bot characters...");
    uint32 totalRandomBotChars = 0;
    std::vector<std::pair<Player*, uint32>> playerBots;
    std::vector<WorldSession*> sessionBots;
    int bot_creation = 0;
    timer = getMSTime();
    bool nameCached = false;
    for (uint32 accountNumber = 0; accountNumber < totalAccountCount; ++accountNumber)
    {
        std::ostringstream out;
        out << sPlayerbotAIConfig.RandomBotAccountPrefix << accountNumber;
        std::string const accountName = out.str();

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME);
        PlayerbotSetData(stmt, 0, accountName);
        PreparedQueryResult result = LoginDatabase.Query(stmt);
        if (!result)
            continue;

        Field* fields = result->Fetch();
        uint32 accountId = PlayerbotFieldGet<uint32>(fields[0]);

        sPlayerbotAIConfig.RandomBotAccounts.push_back(accountId);

        uint32 count = AccountMgr::GetCharactersCount(accountId);
        if (count >= 10)
        {
            continue;
        }

        if (!nameCached)
        {
            nameCached = true;
            TC_LOG_INFO("playerbots", "Creating cache for names per gender and race...");
            QueryResult result = CharacterDatabase.PQuery("SELECT name, gender FROM playerbots_names");
            if (!result)
            {
                TC_LOG_ERROR("playerbots", "No more unused names left");
                return;
            }
            do
            {
                Field* fields = result->Fetch();
                std::string name = PlayerbotFieldGet<std::string>(fields[0]);
                NameRaceAndGender raceAndGender = static_cast<NameRaceAndGender>(PlayerbotFieldGet<uint8>(fields[1]));
                if (sObjectMgr->CheckPlayerName(name, sWorld->GetDefaultDbcLocale(), true) == CHAR_NAME_SUCCESS)
                {
                    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHECK_NAME);
                    PlayerbotSetData(stmt, 0, name);

                    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
                        continue;

                    nameCache[raceAndGender].push_back(name);
                }

            } while (result->NextRow());
        }

        TC_LOG_DEBUG("playerbots", "Creating random bot characters for account: [{}/{}]", accountNumber + 1, totalAccountCount);
        RandomPlayerbotFactory factory;

        // 12.x expanded the WorldSession constructor (session-less bot still passes isBot = true).
        WorldSession* session = new WorldSession(accountId, std::string(), 0, std::string(), nullptr, SEC_PLAYER,
                                                uint8(EXPANSION_CLASSIC), time_t(0), std::string(), Minutes(0), 0,
                                                ClientBuild::VariantId{ClientBuild::Platform::Win_x64, ClientBuild::Arch::x64,
                                                                       ClientBuild::Type::Retail},
                                                sWorld->GetDefaultDbcLocale(), 0, false, true);
        sessionBots.push_back(session);

        for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES - count; ++cls)
        {
            // skip nonexistent classes
            if (!((1 << (cls - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(cls))
                continue;

            // skip disabled with config classes
            if ((1 << (cls - 1)) & sWorld->getIntConfig(CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK))
                continue;

            Player* playerBot = factory.CreateRandomBot(session, cls, nameCache);
            if (!playerBot)
            {
                TC_LOG_ERROR("playerbots", "Fail to create character for account {}", accountId);
                continue;
            }

            playerBot->SaveToDB(true);
            sCharacterCache->AddCharacterCacheEntry(playerBot->GetGUID(), accountId, playerBot->GetName(),
                                                    playerBot->GetGender(), playerBot->GetRace(),
                                                    playerBot->GetClass(), playerBot->GetLevel(), false);
            playerBot->CleanupsBeforeDelete();
            delete playerBot;
            bot_creation++;
        }
    }

    if (bot_creation)
    {
        TC_LOG_INFO("playerbots", "Waiting for {} characters loading into database ({} queries)...", bot_creation, CharacterDatabase.QueueSize());
        /* wait for characters load into database, or characters will fail to loggin */
        while (CharacterDatabase.QueueSize())
        {
            std::this_thread::sleep_for(1s);
        }
        TC_LOG_INFO("playerbots", ">> {} Characters loaded into database in {} ms", bot_creation, GetMSTimeDiffToNow(timer));
    }

    for (WorldSession* session : sessionBots)
        delete session;

    for (uint32 accountId : sPlayerbotAIConfig.RandomBotAccounts)
    {
        totalRandomBotChars += AccountMgr::GetCharactersCount(accountId);
    }

    TC_LOG_INFO("server.loading", ">> {} random bot accounts with {} characters available",
            sPlayerbotAIConfig.RandomBotAccounts.size(), totalRandomBotChars);
}

std::string const RandomPlayerbotFactory::CreateRandomGuildName()
{
    std::string guildName = "";

    QueryResult result = CharacterDatabase.PQuery("SELECT MAX(name_id) FROM playerbots_guild_names");
    if (!result)
    {
        TC_LOG_ERROR("playerbots", "No more names left for random guilds");
        return guildName;
    }

    Field* fields = result->Fetch();
    uint32 maxId = PlayerbotFieldGet<uint32>(fields[0]);

    uint32 id = urand(0, maxId);
    std::string nameExpr = GetGuildNameSelector("n.name");
    result = CharacterDatabase.PQuery(
        "SELECT {} FROM playerbots_guild_names n "
        "LEFT OUTER JOIN guild e ON e.name = {} "
        "WHERE e.guildid IS NULL AND n.name_id >= {} LIMIT 1",
        nameExpr, nameExpr, id);
    if (!result)
    {
        TC_LOG_ERROR("playerbots", "No more names left for random guilds");
        return guildName;
    }

    fields = result->Fetch();
    guildName = PlayerbotFieldGet<std::string>(fields[0]);

    return guildName;
}
