/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotRepository.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "AiObjectContext.h"

void PlayerbotRepository::Load(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    if (QueryResult result = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `playerbots_db_store` WHERE `guid` = {}", guid))
    {
        std::vector<std::string> values;
        do
        {
            Field* fields = result->Fetch();
            std::string const key = PlayerbotFieldGet<std::string>(fields[0]);
            std::string const value = PlayerbotFieldGet<std::string>(fields[1]);

            if (key == "value")
                values.push_back(value);
            else if (key == "co")
            {
                botAI->ClearStrategies(BOT_STATE_COMBAT);
                botAI->ChangeStrategy("+chat", BOT_STATE_COMBAT);
                botAI->ChangeStrategy(value, BOT_STATE_COMBAT);
            }
            else if (key == "nc")
            {
                botAI->ClearStrategies(BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy("+chat", BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy(value, BOT_STATE_NON_COMBAT);
            }
            else if (key == "dead")
                botAI->ChangeStrategy(value, BOT_STATE_DEAD);
        } while (result->NextRow());

        botAI->GetAiObjectContext()->GetUntypedValue("outfit list");

        botAI->GetAiObjectContext()->Load(values);
    }
}

void PlayerbotRepository::Save(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    trans->Append(Trinity::StringFormat("DELETE FROM `playerbots_db_store` WHERE `guid` = {}", guid).c_str());

    std::vector<std::string> data = botAI->GetAiObjectContext()->Save();
    for (std::vector<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(trans, guid, "value", *i);
    }

    SaveValue(trans, guid, "co", FormatStrategies("co", botAI->GetStrategies(BOT_STATE_COMBAT)));
    SaveValue(trans, guid, "nc", FormatStrategies("nc", botAI->GetStrategies(BOT_STATE_NON_COMBAT)));
    SaveValue(trans, guid, "dead", FormatStrategies("dead", botAI->GetStrategies(BOT_STATE_DEAD)));

    CharacterDatabase.DirectCommitTransaction(trans);
}

std::string const PlayerbotRepository::FormatStrategies(std::string const /*type*/, std::vector<std::string> strategies)
{
    std::ostringstream out;
    for (std::vector<std::string>::iterator i = strategies.begin(); i != strategies.end(); ++i)
        out << "+" << (*i).c_str() << ",";

    std::string const res = out.str();
    return res.substr(0, res.size() - 1);
}

void PlayerbotRepository::Reset(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    CharacterDatabase.DirectPExecute("DELETE FROM `playerbots_db_store` WHERE `guid` = {}", guid);
}

void PlayerbotRepository::SaveValue(CharacterDatabaseTransaction trans, uint32 guid, std::string const key,
                                    std::string const value)
{
    std::string escapedKey = key;
    std::string escapedValue = value;
    CharacterDatabase.EscapeString(escapedKey);
    CharacterDatabase.EscapeString(escapedValue);

    trans->Append(Trinity::StringFormat("INSERT INTO `playerbots_db_store` (`guid`, `key`, `value`) VALUES ({}, '{}', '{}')",
                                        guid, escapedKey, escapedValue).c_str());
}
