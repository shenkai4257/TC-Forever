/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ReleaseSpiritAction.h"
#include "Corpse.h"
#include "Event.h"
#include "Log.h"
#include "NearestNpcsValue.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "MiscPackets.h"
#include "NPCPackets.h"
#include "SpellPackets.h"
#include "ServerFacade.h"

// ReleaseSpiritAction implementation
bool ReleaseSpiritAction::Execute(Event event)
{
    if (bot->IsAlive())
    {
        if (!bot->InBattleground())
        {
            botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "release_spirit_not_dead_wait", "I am not dead, will wait here", {}));
            // -follow in bg is overwriten each tick with +follow
            // +stay in bg causes stuttering effect as bot is cycled between +stay and +follow each tick
            botAI->ChangeStrategy("-follow,+stay", BOT_STATE_NON_COMBAT);
        }

        return false;
    }

    if (bot->GetCorpse() && bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "release_spirit_already_spirit", "I am already a spirit", {}));
        return false;
    }

    WorldPacket const& packet = event.getPacket();
    const std::string message = !packet.empty() && packet.GetOpcode() == CMSG_REPOP_REQUEST
        ? PlayerbotTextMgr::instance().GetBotTextOrDefault("release_spirit_releasing", "Releasing...", {})
        : PlayerbotTextMgr::instance().GetBotTextOrDefault("release_spirit_meet_graveyard", "Meet me at the graveyard", {});
    botAI->TellMasterNoFacing(message);

    IncrementDeathCount();
    bot->DurabilityRepairAll(false, 1.0f, false);
    LogRelease("released");

    WorldPacket rawRepop(CMSG_REPOP_REQUEST);
    WorldPackets::Misc::RepopRequest repop(std::move(rawRepop));
    bot->GetSession()->HandleRepopRequest(repop);

    return true;
}

void ReleaseSpiritAction::IncrementDeathCount() const
{
    // BG deaths don't count, matching PlayerbotAI::DoNextAction.
    if (bot->InBattleground())
        return;

    // Death Count to prevent skeleton piles
    Player* master = botAI->GetMaster();
    if (!master || GET_PLAYERBOT_AI(master))
    {
        uint32 deathCount = AI_VALUE(uint32, "death count");
        context->GetValue<uint32>("death count")->Set(deathCount + 1);
    }
}

void ReleaseSpiritAction::LogRelease(std::string const& releaseMsg) const
{
    const std::string teamPrefix = bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H";

    TC_LOG_DEBUG("playerbots", "Bot {} {}:{} <{}> {}",
        bot->GetGUID().ToString().c_str(),
        teamPrefix,
        bot->GetLevel(),
        bot->GetName().c_str(),
        releaseMsg.c_str());
}

// AutoReleaseSpiritAction implementation
bool AutoReleaseSpiritAction::Execute(Event /*event*/)
{
    IncrementDeathCount();
    bot->DurabilityRepairAll(false, 1.0f, false);
    LogRelease("auto released");

    WorldPacket rawRepop(CMSG_REPOP_REQUEST);
    WorldPackets::Misc::RepopRequest repop(std::move(rawRepop));
    bot->GetSession()->HandleRepopRequest(repop);

    LogRelease("releases spirit");

    botAI->SetNextCheckDelay(1000);
    return true;
}

bool AutoReleaseSpiritAction::isUseful()
{
    if (!bot->isDead() || bot->InArena())
        return false;

    if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        return false;

    return ShouldAutoRelease();
}

bool AutoReleaseSpiritAction::ShouldAutoRelease() const
{
    if (!bot->GetGroup())
        return true;

    Player* groupLeader = botAI->GetGroupLeader();
    if (!groupLeader || groupLeader == bot)
        return true;

    if (!IsRealPlayer(botAI->GetMaster()))
        return true;

    if (IsRealPlayer(botAI->GetMaster()) &&
        groupLeader->GetMapId() == bot->GetMapId() &&
        bot->GetMap() &&
        (bot->GetMap()->IsRaid() || bot->GetMap()->IsDungeon()))
    {
        return false;
    }

    return ServerFacade::instance().IsDistanceGreaterThan(
        AI_VALUE2(float, "distance", "group leader"),
        sPlayerbotAIConfig.SightDistance);
}

bool RepopAction::Execute(Event /*event*/)
{
    WorldSafeLocsEntry const* graveyard = GetGrave(
        AI_VALUE(uint32, "death count") > 10 ||
        CalculateDeadTime() > 30 * MINUTE
    );

    if (!graveyard)
        return false;

    PerformGraveyardTeleport(graveyard);
    return true;
}

bool RepopAction::isUseful()
{
    return !bot->InBattleground();
}

int64 RepopAction::CalculateDeadTime() const
{
    if (Corpse* corpse = bot->GetCorpse())
        return time(nullptr) - corpse->GetGhostTime();

    return bot->isDead() ? 0 : 60 * MINUTE;
}

void RepopAction::PerformGraveyardTeleport(WorldSafeLocsEntry const* graveyard) const
{
    bot->RemoveAurasWithInterruptFlags(SpellAuraInterruptFlags::LeaveWorld | SpellAuraInterruptFlags::EnterWorld);
    bot->TeleportTo(graveyard->Loc.GetMapId(), graveyard->Loc.GetPositionX(), graveyard->Loc.GetPositionY(),
                    graveyard->Loc.GetPositionZ(), 0.f);
    RESET_AI_VALUE(bool, "combat::self target");
    RESET_AI_VALUE(WorldPosition, "current position");
}

// SelfResurrectAction implementation for Warlock's Soulstone Resurrection/Shaman's Reincarnation
bool SelfResurrectAction::Execute(Event /*event*/)
{
    if (!bot->IsAlive() && !(bot->m_activePlayerData->SelfResSpells.empty()))
    {
        WorldPacket rawSelfRes(CMSG_SELF_RES);
        WorldPackets::Spells::SelfRes selfRes(std::move(rawSelfRes));
        bot->GetSession()->HandleSelfResOpcode(selfRes);
        return true;
    }
    return false;
}
bool SelfResurrectAction::isUseful()
{
    return !bot->IsAlive() && !(bot->m_activePlayerData->SelfResSpells.empty());
}
