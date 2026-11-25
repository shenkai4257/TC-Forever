/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SeeSpellAction.h"
#include "SpellPackets.h"
#include "ByteBuffer.h"
#include "Event.h"
#include "Formations.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "RTSCValues.h"
#include "RtscAction.h"

std::set<uint32> const FISHING_SPELLS = {7620, 7731, 7732, 18248, 33095, 51294};

Creature* SeeSpellAction::CreateWps(Player* wpOwner, float x, float y, float z, float o, uint32 entry, Creature* /*lastWp*/,
                                    bool important)
{
    float dist = wpOwner->GetDistance(x, y, z);
    float delay = IN_MILLISECONDS * dist / wpOwner->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.ReactDelay;

    if (!important)
        delay *= 0.25;

    Creature* wpCreature = wpOwner->SummonCreature(entry, x, y, z - 1, o, TEMPSUMMON_TIMED_DESPAWN, Milliseconds(static_cast<int32>(delay)));
    if (!important)
        wpCreature->SetObjectScale(0.2f);

    return wpCreature;
}

bool SeeSpellAction::Execute(Event event)
{
    Player* master = botAI->GetMaster();
    if (!master)
        return false;

    // The master's cast request, read the way the core's CMSG_CAST_SPELL handler reads it (the 3.3.5
    // "cast count / spell id / flags" header does not exist in 12.x).
    WorldPacket p(event.getPacket());
    WorldPackets::Spells::SpellCastRequest castRequest;
    try
    {
        p.rpos(0);
        p >> castRequest;
    }
    catch (ByteBufferException const&)
    {
        // ignore a malformed cast request instead of crashing
        TC_LOG_WARN("playerbots", "SeeSpellAction: failed to read the cast request");
        return false;
    }

    uint32 const spellId = uint32(castRequest.SpellID);

    if (FISHING_SPELLS.find(spellId) != FISHING_SPELLS.end())
    {
        if (AI_VALUE(bool, "can fish") && sPlayerbotAIConfig.EnableFishingWithMaster)
        {
            botAI->ChangeStrategy("+master fishing", BOT_STATE_NON_COMBAT);
            return true;
        }
        return false;
    }

    if (spellId != RTSC_MOVE_SPELL)
        return false;

    if (!castRequest.Target.DstLocation)
    {
        // do not dereference a null destination; ignore cast requests without a destination
        TC_LOG_WARN("playerbots", "SeeSpellAction: the cast request does not carry a destination");
        return false;
    }

    WorldPosition spellPosition(master->GetMapId(), castRequest.Target.DstLocation->Location);

    SET_AI_VALUE(WorldPosition, "see spell location", spellPosition);

    bool selected = AI_VALUE(bool, "RTSC selected");
    bool inRange = spellPosition.distance(bot) <= 10;
    std::string const nextAction = AI_VALUE(std::string, "RTSC next spell action");

    if (nextAction.empty())
    {
        if (!inRange && selected)
            bot->SendPlaySpellVisualKit(6372, 0, 0);
        else if (inRange && !selected)
            bot->SendPlaySpellVisualKit(5036, 0, 0);

        SET_AI_VALUE(bool, "RTSC selected", inRange);

        if (selected)
            return MoveToSpell(spellPosition);

        return inRange;
    }
    else if (nextAction == "move")
    {
        return MoveToSpell(spellPosition);
    }
    else if (nextAction.find("save ") != std::string::npos)
    {
        std::string locationName;
        if (nextAction.find("save selected ") != std::string::npos)
        {
            if (!selected)
                return false;

            locationName = nextAction.substr(14);
        }
        else
            locationName = nextAction.substr(5);

        SetFormationOffset(spellPosition);

        SET_AI_VALUE2(WorldPosition, "RTSC saved location", locationName, spellPosition);

        Creature* wpCreature =
            bot->SummonCreature(15631, spellPosition.GetPositionX(), spellPosition.GetPositionY(), spellPosition.GetPositionZ(),
                                spellPosition.GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, Milliseconds(static_cast<int32>(2000.0f)));
        wpCreature->SetObjectScale(0.5f);
        RESET_AI_VALUE(std::string, "RTSC next spell action");

        return true;
    }

    return false;
}

bool SeeSpellAction::SelectSpell(WorldPosition& spellPosition)
{
    if (spellPosition.distance(bot) <= 5 || AI_VALUE(bool, "RTSC selected"))
    {
        SET_AI_VALUE(bool, "RTSC selected", true);
        bot->SendPlaySpellVisualKit(5036, 0, 0);
    }

    return true;
}

bool SeeSpellAction::MoveToSpell(WorldPosition& spellPosition, bool inFormation)
{
    if (inFormation)
        SetFormationOffset(spellPosition);

    if (botAI->HasStrategy("stay", botAI->GetState()))
    {
        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionInfo stayPosition = posMap["stay"];

        stayPosition.Set(spellPosition.GetPositionX(), spellPosition.GetPositionY(), spellPosition.GetPositionZ(), spellPosition.GetMapId());
        posMap["stay"] = stayPosition;
    }

    if (bot->IsWithinLOS(spellPosition.GetPositionX(), spellPosition.GetPositionY(), spellPosition.GetPositionZ()))
        return MoveNear(spellPosition.GetMapId(), spellPosition.GetPositionX(), spellPosition.GetPositionY(), spellPosition.GetPositionZ(), 0);

    return MoveTo(spellPosition.GetMapId(), spellPosition.GetPositionX(), spellPosition.GetPositionY(), spellPosition.GetPositionZ(), false,
                  false);
}

void SeeSpellAction::SetFormationOffset(WorldPosition& spellPosition)
{
    Player* master = botAI->GetMaster();

    Formation* formation = AI_VALUE(Formation*, "formation");

    WorldLocation formationLocation = formation->GetLocation();

    if (formationLocation.GetPositionX() != 0 || formationLocation.GetPositionY() != 0)
    {
        spellPosition -= WorldPosition(master);
        spellPosition += formationLocation;
    }
}
