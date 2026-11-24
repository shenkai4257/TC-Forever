/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ExternalEventHelper.h"
#include "ChatHelper.h"
#include "Playerbots.h"
#include "Trigger.h"

bool ExternalEventHelper::ParseChatCommand(std::string const command, Player* owner)
{
    if (HandleCommand(command, "", owner))
        return true;

    size_t i = std::string::npos;
    while (true)
    {
        size_t found = command.rfind(" ", i);
        if (found == std::string::npos || !found)
            break;

        std::string const name = command.substr(0, found);
        std::string const param = command.substr(found + 1);

        i = found - 1;

        if (HandleCommand(name, param, owner))
            return true;
    }

    if (!ChatHelper::parseableItem(command))
        return false;

    if (sPlayerbotAIConfig.EnableAutoTradeOnItemMention)
    {
        HandleCommand("c", command, owner);
        HandleCommand("t", command, owner);
    }

    return true;
}

void ExternalEventHelper::HandlePacket(std::map<uint32, std::string>& handlers, WorldPacket const& packet,
                                       Player* owner)
{
    uint32 opcode = packet.GetOpcode();
    auto const itr = handlers.find(opcode);
    if (itr == handlers.end())
        return;

    std::string const& name = itr->second;
    // Which packet fired which packet trigger (Logger.playerbots at debug).
    TC_LOG_DEBUG("playerbots", "packet 0x{:06X} -> trigger '{}'", opcode, name);

    Trigger* trigger = aiObjectContext->GetTrigger(name);
    if (!trigger)
        return;

    WorldPacket p(packet);
    trigger->ExternalEvent(p, owner);
}

bool ExternalEventHelper::HandleCommand(std::string const name, std::string const param, Player* owner)
{
    Trigger* trigger = aiObjectContext->GetTrigger(name);
    if (!trigger)
        return false;

    trigger->ExternalEvent(param, owner);

    return true;
}
