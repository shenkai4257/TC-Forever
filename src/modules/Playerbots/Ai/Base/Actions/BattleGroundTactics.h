/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BATTLEGROUNDTACTICS_H
#define PLAYERBOTS_BATTLEGROUNDTACTICS_H

#include "Action.h"
#include "MovementActions.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <string>

class ChatHandler;
class PlayerbotAI;
struct Battleground;
struct BGStrategyData;

// Battleground tactics are stubbed for now. 12.x runs Alterac Valley, Warsong Gulch and Arathi
// Basin through core scripts and the module has no access to their objective state, so the action
// classes exist only to keep the action contexts and strategies compiling. A dedicated
// battleground task will restore the tactics on top of the scripted state.
class BGTactics : public MovementAction
{
public:
    static bool HandleConsoleCommand(ChatHandler* /*handler*/, char const* /*args*/) { return true; }
    static uint8 GetBotStrategyForTeam(Battleground* /*bg*/, TeamId /*teamId*/) { return 0; }
    static void SetBotStrategies(uint32 /*instanceId*/, BGStrategyData const& /*data*/) { }
    static void ClearBotStrategies(uint32 /*instanceId*/) { }
    static uint8 GetWsgDefenderCount(Battleground* /*bg*/, TeamId /*teamId*/) { return 0; }
    static bool ShouldYieldWsgTactics(PlayerbotAI* /*botAI*/) { return false; }

    BGTactics(PlayerbotAI* botAI, std::string const name = "bg tactics") : MovementAction(botAI, name) { }

    bool Execute(Event /*event*/) override { return false; }
    bool isUseful() override { return false; }
};

class WsgObjectiveRefreshAction : public BGTactics
{
public:
    WsgObjectiveRefreshAction(PlayerbotAI* botAI) : BGTactics(botAI, "refresh wsg objective") { }
    bool isUseful() override { return false; }
    bool Execute(Event /*event*/) override { return false; }
};

class WsgEscortFollowAction : public BGTactics
{
public:
    WsgEscortFollowAction(PlayerbotAI* botAI) : BGTactics(botAI, "protect fc") { }
    bool isUseful() override { return false; }
    bool Execute(Event /*event*/) override { return false; }
};

class WsgObjectiveRouteAction : public BGTactics
{
public:
    WsgObjectiveRouteAction(PlayerbotAI* botAI, std::string const name = "bg wsg route") : BGTactics(botAI, name) { }
    bool isUseful() override { return false; }
    bool Execute(Event /*event*/) override { return false; }
};

class WsgObjectiveApproachAction : public WsgObjectiveRouteAction
{
public:
    WsgObjectiveApproachAction(PlayerbotAI* botAI) : WsgObjectiveRouteAction(botAI, "bg wsg approach") { }
};

class WsgObjectiveCorridorAction : public WsgObjectiveRouteAction
{
public:
    WsgObjectiveCorridorAction(PlayerbotAI* botAI) : WsgObjectiveRouteAction(botAI, "bg wsg corridor") { }
};

class WsgObjectiveDirectAction : public WsgObjectiveRouteAction
{
public:
    WsgObjectiveDirectAction(PlayerbotAI* botAI) : WsgObjectiveRouteAction(botAI, "bg wsg direct") { }
};

class WsgSupportAttackAction : public AttackAction
{
public:
    WsgSupportAttackAction(PlayerbotAI* botAI) : AttackAction(botAI, "attack wsg support") { }
    Unit* GetTarget() override { return nullptr; }
    bool isUseful() override { return false; }
};

class WsgRegroupAction : public DropTargetAction
{
public:
    WsgRegroupAction(PlayerbotAI* botAI) : DropTargetAction(botAI) { }
    std::string const getName() override { return "wsg regroup"; }
    bool isUseful() override { return false; }
};

// Strategy payload kept for the RandomPlayerbotMgr bookkeeping that still references the type.
struct BGStrategyData
{
    uint8 allianceStrategy = 0;
    uint8 hordeStrategy = 0;
};

#endif
