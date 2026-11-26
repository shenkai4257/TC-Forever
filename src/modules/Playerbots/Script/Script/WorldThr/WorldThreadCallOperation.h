/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_WORLDTHREADCALLOPERATION_H
#define PLAYERBOTS_WORLDTHREADCALLOPERATION_H

#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotOperation.h"
#include <functional>
#include <string>

// Runs an arbitrary callable on the bot in the world tick window. Core registers packet handlers
// as PROCESS_THREADUNSAFE when they may only run while the map threads are idle (the world thread
// runs them between map updates); a bot action that needs one hands the call over through this
// operation instead of calling it on its own map worker thread. The callable receives the bot
// resolved by guid at execution time, so it must copy guids and values instead of capturing raw
// pointers to game objects. Effects are not visible when the operation is queued: outbound packets
// of the handler reach the bot on its next AI tick, like any other server packet.
class WorldThreadCallOperation : public PlayerbotOperation
{
public:
    WorldThreadCallOperation(ObjectGuid botGuid, std::string name, std::function<void(Player*)> call)
        : m_botGuid(botGuid), m_name(std::move(name)), m_call(std::move(call)) { }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindConnectedPlayer(m_botGuid);
        if (!bot)
            return false;

        m_call(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 50; }
    std::string GetName() const override { return m_name; }

    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
    std::string m_name;
    std::function<void(Player*)> m_call;
};

#endif
