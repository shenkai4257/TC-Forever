/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_GUILDMANAGEMENTACTIONS_H
#define PLAYERBOTS_GUILDMANAGEMENTACTIONS_H

#include "Action.h"

class Player;
class PlayerbotAI;
class WorldPacket;

class GuidManageAction : public Action
{
public:
    GuidManageAction(PlayerbotAI* botAI, std::string const name = "guild manage")
        : Action(botAI, name)
    {
    }

    bool Execute(Event event) override;
    bool isUseful() override { return false; }

protected:
    // 12.x guild management is structured per handler (name or guid), so each action
    // builds its own packet and calls the session handler directly.
    virtual bool SendManage(Player* member) = 0;
    virtual Player* GetPlayer(Event event);
    virtual bool PlayerIsValid(Player* member);
    virtual uint8 GetRankId(Player* member);
};

class GuildInviteAction : public GuidManageAction
{
public:
    GuildInviteAction(PlayerbotAI* botAI, std::string const name = "guild invite")
        : GuidManageAction(botAI, name)
    {
    }

    bool isUseful() override;

protected:
    bool SendManage(Player* member) override;
    bool PlayerIsValid(Player* member) override;
};

class GuildPromoteAction : public GuidManageAction
{
public:
    GuildPromoteAction(PlayerbotAI* botAI, std::string const name = "guild promote")
        : GuidManageAction(botAI, name)
    {
    }

    bool isUseful() override;

protected:
    bool SendManage(Player* member) override;
    bool PlayerIsValid(Player* member) override;
};

class GuildDemoteAction : public GuidManageAction
{
public:
    GuildDemoteAction(PlayerbotAI* botAI, std::string const name = "guild demote")
        : GuidManageAction(botAI, name)
    {
    }

    bool isUseful() override;

protected:
    bool SendManage(Player* member) override;
    bool PlayerIsValid(Player* member) override;
};

class GuildRemoveAction : public GuidManageAction
{
public:
    GuildRemoveAction(PlayerbotAI* botAI, std::string const name = "guild remove")
        : GuidManageAction(botAI, name)
    {
    }

    bool isUseful() override;

protected:
    bool SendManage(Player* member) override;
    bool PlayerIsValid(Player* member) override;
};

class GuildManageNearbyAction : public Action
{
public:
    GuildManageNearbyAction(PlayerbotAI* botAI) : Action(botAI, "guild manage nearby") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class GuildLeaveAction : public Action
{
public:
    GuildLeaveAction(PlayerbotAI* botAI) : Action(botAI, "guild leave") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

#endif
