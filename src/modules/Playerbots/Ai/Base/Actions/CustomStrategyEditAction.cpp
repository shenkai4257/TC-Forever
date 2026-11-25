/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CustomStrategyEditAction.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "CustomStrategy.h"
#include "Event.h"
#include "Playerbots.h"

bool CustomStrategyEditAction::Execute(Event event)
{
    std::string text = event.getParam();
    size_t pos = text.find(" ");
    if (pos == std::string::npos)
        return PrintHelp();

    std::string const name = text.substr(0, pos);
    text = text.substr(pos + 1);

    pos = text.find(" ");
    if (pos == std::string::npos)
        pos = text.size();

    std::string const idx = text.substr(0, pos);
    text = pos >= text.size() ? "" : text.substr(pos + 1);

    return idx == "?" ? Print(name) : Edit(name, atoi(idx.c_str()), text);
}

bool CustomStrategyEditAction::PrintHelp()
{
    botAI->TellMaster("=== Custom strategies ===");

    uint32 owner = botAI->GetBot()->GetGUID().GetCounter();

    if (QueryResult result = CharacterDatabase.PQuery("SELECT DISTINCT name FROM playerbots_custom_strategy WHERE owner = {}", owner))
    {
        do
        {
            Field* fields = result->Fetch();
            std::string const name = PlayerbotFieldGet<std::string>(fields[0]);
            botAI->TellMaster(name);
        } while (result->NextRow());
    }

    botAI->TellMaster("Usage: cs <name> <idx> <command>");
    return false;
}

bool CustomStrategyEditAction::Print(std::string const name)
{
    std::ostringstream out;
    out << "=== " << name << " ===";
    botAI->TellMaster(out.str());

    uint32 owner = botAI->GetBot()->GetGUID().GetCounter();

    std::string escapedName = name;
    CharacterDatabase.EscapeString(escapedName);
    if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT idx, action_line FROM playerbots_custom_strategy WHERE owner = {} AND name = '{}' ORDER BY idx",
            owner, escapedName))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 idx = PlayerbotFieldGet<uint32>(fields[0]);
            std::string const action = PlayerbotFieldGet<std::string>(fields[1]);

            PrintActionLine(idx, action);
        } while (result->NextRow());
    }

    return true;
}

bool CustomStrategyEditAction::Edit(std::string const name, uint32 idx, std::string const command)
{
    uint32 owner = botAI->GetBot()->GetGUID().GetCounter();

    std::string escapedName = name;
    std::string escapedCommand = command;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.EscapeString(escapedCommand);

    if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT action_line FROM playerbots_custom_strategy WHERE owner = {} AND name = '{}' AND idx = {}",
            owner, escapedName, idx))
    {
        if (command.empty())
            CharacterDatabase.DirectPExecute(
                "DELETE FROM playerbots_custom_strategy WHERE name = '{}' AND owner = {} AND idx = {}",
                escapedName, owner, idx);
        else
            CharacterDatabase.DirectPExecute(
                "UPDATE playerbots_custom_strategy SET action_line = '{}' WHERE name = '{}' AND owner = {} AND idx = {}",
                escapedCommand, escapedName, owner, idx);
    }
    else
    {
        CharacterDatabase.DirectPExecute(
            "INSERT INTO playerbots_custom_strategy (name, owner, idx, action_line) VALUES ('{}', {}, {}, '{}')",
            escapedName, owner, idx, escapedCommand);
    }

    PrintActionLine(idx, command);

    std::ostringstream ss;
    ss << "custom::" << name;

    if (Strategy* strategy = botAI->GetAiObjectContext()->GetStrategy(ss.str()))
    {
        if (CustomStrategy* cs = dynamic_cast<CustomStrategy*>(strategy))
        {
            cs->Reset();
            botAI->ReInitCurrentEngine();
        }
    }

    return true;
}

bool CustomStrategyEditAction::PrintActionLine(uint32 idx, std::string const command)
{
    std::ostringstream out;
    out << "#" << idx << " " << command;
    botAI->TellMaster(out.str());
    return true;
}
