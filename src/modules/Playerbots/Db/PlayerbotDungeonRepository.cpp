/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotDungeonRepository.h"
#include "FieldHelper.h"
#include "PlayerbotsDatabase.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"    // Required due to a poor implementation by AC
#include "Timer.h"
#include "World.h"

std::vector<DungeonSuggestion> const PlayerbotDungeonRepository::GetDungeonSuggestions()
{
    return m_dungeonSuggestions;
}

void PlayerbotDungeonRepository::LoadDungeonSuggestions()
{
    TC_LOG_INFO("server.loading", "Loading playerbots dungeon suggestions...");
    uint32 oldMSTime = getMSTime();

    uint32 count = 0;
    auto statement = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_DUNGEON_SUGGESTION);
    uint8 const expansion = sWorld->getIntConfig(CONFIG_EXPANSION);
    PlayerbotSetData(statement, 0, expansion);

    PreparedQueryResult result = PlayerbotsDatabase.Query(statement);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            std::string const name = PlayerbotFieldGet<std::string>(fields[0]);
            uint8 const difficulty = PlayerbotFieldGet<uint8>(fields[1]);
            uint8 const min_level = PlayerbotFieldGet<uint8>(fields[2]);
            uint8 const max_level = PlayerbotFieldGet<uint8>(fields[3]);
            std::string const abbrevation = PlayerbotFieldGet<std::string>(fields[4]);
            std::string const strategy = PlayerbotFieldGet<std::string>(fields[5]);

            DungeonSuggestion const row = {
                name, static_cast<Difficulty>(difficulty), min_level, max_level, abbrevation, strategy};

            m_dungeonSuggestions.push_back(row);
            ++count;
        } while (result->NextRow());
    }

    TC_LOG_INFO("server.loading", "{} playerbots dungeon suggestions loaded in {} ms", count,
             GetMSTimeDiffToNow(oldMSTime));
}
