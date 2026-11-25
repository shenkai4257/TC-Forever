/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTOPERATIONS_H
#define PLAYERBOTS_PLAYERBOTOPERATIONS_H

#include "Group.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperation.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "UseMeetingStoneAction.h"
#include "WorldSession.h"
#include "World.h"

// Group invite operation
class GroupInviteOperation : public PlayerbotOperation
{
public:
    GroupInviteOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
        {
            TC_LOG_DEBUG("playerbots", "GroupInviteOperation: Bot or target not found");
            return false;
        }

        // Check if target is already in a group
        if (target->GetGroup())
        {
            TC_LOG_DEBUG("playerbots", "GroupInviteOperation: Target {} is already in a group", target->GetName());
            return false;
        }

        Group* group = bot->GetGroup();

        // Create group if bot doesn't have one
        if (!group)
        {
            group = new Group;
            if (!group->Create(bot))
            {
                delete group;
                TC_LOG_ERROR("playerbots", "GroupInviteOperation: Failed to create group for bot {}", bot->GetName());
                return false;
            }
            sGroupMgr->AddGroup(group);
            TC_LOG_DEBUG("playerbots", "GroupInviteOperation: Created new group for bot {}", bot->GetName());
        }

        // Convert to raid if needed (more than 5 members)
        if (!group->isRaidGroup() && group->GetMembersCount() >= 5)
        {
            group->ConvertToRaid();
            TC_LOG_DEBUG("playerbots", "GroupInviteOperation: Converted group to raid");
        }

        // Add member to group
        if (group->AddMember(target))
        {
            TC_LOG_DEBUG("playerbots", "GroupInviteOperation: Successfully added {} to group", target->GetName());
            if (sPlayerbotAIConfig.SummonWhenGroup && target->GetDistance(bot) > sPlayerbotAIConfig.SightDistance)
            {
                PlayerbotAI* targetAI = sPlayerbotsMgr.GetPlayerbotAI(target);
                if (targetAI)
                {
                    SummonAction summonAction(targetAI, "group summon");
                    summonAction.Teleport(bot, target, true);
                }
            }
            return true;
        }
        else
        {
            TC_LOG_ERROR("playerbots", "GroupInviteOperation: Failed to add {} to group", target->GetName());
            return false;
        }
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority (player-facing)

    std::string GetName() const override { return "GroupInvite"; }

    bool IsValid() const override
    {
        // Check if bot still exists and is online
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);
        return bot && target;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
};

// Deferred bot logout. LogoutPlayerBot deletes the player object synchronously, so when the request
// comes from the bot's own update tick (see BotTickEvent::IsTickingPlayer) it is handed to the world
// thread and executed in the tick window, where map threads are idle - the same window a real
// player's logout runs in.
class BotLogoutOperation : public PlayerbotOperation
{
public:
    explicit BotLogoutOperation(ObjectGuid botGuid) : m_botGuid(botGuid) { }

    bool Execute() override
    {
        if (!ObjectAccessor::FindConnectedPlayer(m_botGuid))
            return true;   // already gone

        if (Player* bot = ObjectAccessor::FindConnectedPlayer(m_botGuid))
            if (PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot))
                if (Player* master = botAI->GetMaster())
                if (PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(master))
                {
                    mgr->LogoutPlayerBot(m_botGuid);
                    return true;
                }

        sRandomPlayerbotMgr.LogoutPlayerBot(m_botGuid);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // a bot waiting to go offline

    std::string GetName() const override { return "BotLogout"; }

private:
    ObjectGuid m_botGuid;
};

// Remove member from group
class GroupRemoveMemberOperation : public PlayerbotOperation
{
public:
    GroupRemoveMemberOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            TC_LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(target->GetGUID()))
        {
            TC_LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Target is not in bot's group");
            return false;
        }

        group->RemoveMember(target->GetGUID());
        TC_LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Removed {} from group", target->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupRemoveMember"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
};

// Convert group to raid
class GroupConvertToRaidOperation : public PlayerbotOperation
{
public:
    GroupConvertToRaidOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            TC_LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Bot is not in a group");
            return false;
        }

        if (group->isRaidGroup())
        {
            TC_LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Group is already a raid");
            return true;  // Success - already in desired state
        }

        group->ConvertToRaid();
        TC_LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Converted group to raid");
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupConvertToRaid"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Set group leader
class GroupSetLeaderOperation : public PlayerbotOperation
{
public:
    GroupSetLeaderOperation(ObjectGuid botGuid, ObjectGuid newLeaderGuid)
        : m_botGuid(botGuid), m_newLeaderGuid(newLeaderGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);

        if (!bot || !newLeader)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            TC_LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(newLeader->GetGUID()))
        {
            TC_LOG_DEBUG("playerbots", "GroupSetLeaderOperation: New leader is not in the group");
            return false;
        }

        group->ChangeLeader(newLeader->GetGUID());
        group->SendUpdate();
        TC_LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Changed leader to {}", newLeader->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupSetLeader"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);
        return bot && newLeader;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_newLeaderGuid;
};

// Bot logout group cleanup operation
class BotLogoutGroupCleanupOperation : public PlayerbotOperation
{
public:
    BotLogoutGroupCleanupOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!botAI)
            return false;

        Group* group = bot->GetGroup();
        if (group && !bot->InBattleground() && !bot->InBattlegroundQueue() && IsRealPlayer(botAI->GetMaster()))
            PlayerbotRepository::instance().Save(botAI);

        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 70; }
    std::string GetName() const override { return "BotLogoutGroupCleanup"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Add player bot operation (for logging in bots from map threads)
class AddPlayerBotOperation : public PlayerbotOperation
{
public:
    AddPlayerBotOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        sRandomPlayerbotMgr.AddPlayerBot(m_botGuid, m_masterAccountId);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority

    std::string GetName() const override { return "AddPlayerBot"; }

    bool IsValid() const override
    {
        return !ObjectAccessor::FindConnectedPlayer(m_botGuid);
    }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId;
};

class OnBotLoginOperation : public PlayerbotOperation
{
public:
    OnBotLoginOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        // find and verify bot still exists
        Player* bot = ObjectAccessor::FindConnectedPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotHolder* holder = &RandomPlayerbotMgr::instance();
        if (m_masterAccountId)
        {
            WorldSession* masterSession = sWorld->FindSession(m_masterAccountId);
            Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;
            if (masterPlayer)
                holder = PlayerbotsMgr::instance().GetPlayerbotMgr(masterPlayer);
        }

        if (!holder)
            return false;

        holder->OnBotLogin(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 100; }
    std::string GetName() const override { return "OnBotLogin"; }

    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId = 0;
};

#endif
