/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildCreateActions.h"
#include "ArenaTeam.h"
#include "BudgetValues.h"
#include "Event.h"
#include "GuildMgr.h"
#include "PetitionPackets.h"
#include "Playerbots.h"
#include "RandomPlayerbotFactory.h"
#include "ServerFacade.h"
#include "SharedDefines.h"

bool BuyPetitionAction::Execute(Event /*event*/)
{
    GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    for (GuidVector::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_PETITIONER, UNIT_NPC_FLAG_2_NONE);
        if (!pCreature)
            continue;

        std::string const guildName = RandomPlayerbotFactory::CreateRandomGuildName();
        if (guildName.empty())
            continue;

        WorldPacket raw(CMSG_PETITION_BUY);
        WorldPackets::Petition::PetitionBuy packet(std::move(raw));
        packet.Unit = pCreature->GetGUID();
        packet.Title = guildName;
        packet.Muid = 0;

        bot->GetSession()->HandlePetitionBuy(packet);

        return true;
    }

    return false;
}

bool BuyPetitionAction::isUseful() { return canBuyPetition(bot); };

bool BuyPetitionAction::canBuyPetition(Player* bot)
{
    if (bot->GetGuildId())
        return false;

    if (bot->GetGuildIdInvited())
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    AiObjectContext* context = botAI->GetAiObjectContext();

    if (AI_VALUE2(uint32, "item count", "Hitem:5863:"))
        return false;

    if (botAI->GetGuilderType() == GuilderType::SOLO)
        return false;

    if (botAI->GetGrouperType() == GrouperType::SOLO)
        return false;

    if (!botAI->HasStrategy("guild", BOT_STATE_NON_COMBAT))
        return false;

    uint32 cost = 1000;  // GUILD_CHARTER_COST;

    if (AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) < cost)
        return false;

    return true;
}

bool PetitionOfferAction::Execute(Event event)
{
    std::vector<Item*> petitions = AI_VALUE2(std::vector<Item*>, "inventory items", chat->FormatQItem(5863));

    if (petitions.empty())
        return false;

    ObjectGuid guid = event.getObject();

    Player* master = GetMaster();
    if (!master)
    {
        if (!guid)
            guid = bot->GetTarget();
    }
    else
    {
        if (!guid)
            guid = master->GetTarget();
    }

    if (!guid)
        return false;

    Player* player = ObjectAccessor::FindPlayer(guid);

    if (!player)
        return false;

    WorldPacket raw(CMSG_OFFER_PETITION);
    WorldPackets::Petition::OfferPetition data(std::move(raw));
    data.TargetPlayer = guid;
    data.ItemGUID = petitions.front()->GetGUID();

    QueryResult result =
        CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = {} AND petitionguid = {}",
                                player->GetSession()->GetAccountId(), petitions.front()->GetGUID().GetCounter());
    if (result)
    {
        return false;
    }

    bot->GetSession()->HandleOfferPetition(data);

    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = {}",
                                     petitions.front()->GetGUID().GetCounter());
    uint8 signs = result ? (uint8)result->GetRowCount() : 0;

    context->GetValue<uint8>("petition signs")->Set(signs);

    return true;
}

bool PetitionOfferAction::isUseful() { return !bot->GetGuildId(); }

bool PetitionOfferNearbyAction::Execute(Event /*event*/)
{
    uint32 found = 0;

    GuidVector nearGuids = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest friendly players")->Get();
    for (auto& i : nearGuids)
    {
        Player* player = ObjectAccessor::FindPlayer(i);

        if (!player)
            continue;

        if (player->GetGuildId())
            continue;

        if (player->GetGuildIdInvited())
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);

        if (botAI)
        {
            /*
            if (botAI->GetGrouperType() == SOLO && !botAI->HasGameClientMaster())   // Do not invite solo players.
                continue;

            */
            if (IsRealPlayer(botAI->GetMaster()))   // Do not invite bots that belong to an active player.
                continue;
        }
        else
        {
            if (!sPlayerbotAIConfig.RandomBotGroupNearby)
                return false;
        }

        if (ServerFacade::instance().GetDistance2d(bot, player) > sPlayerbotAIConfig.SightDistance)
            continue;

        // Parse rpg target to quest action.
        WorldPacket p(CMSG_QUEST_GIVER_ACCEPT_QUEST);
        p << i;
        p.rpos(0);

        if (PetitionOfferAction::Execute(Event("petition offer nearby", p)))
            found++;
    }

    return found > 0;
}

bool PetitionOfferNearbyAction::isUseful()
{
    return !bot->GetGuildId() && AI_VALUE2(uint32, "item count", chat->FormatQItem(5863)) &&
           AI_VALUE(uint8, "petition signs") < sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS);
}

bool PetitionTurnInAction::Execute(Event /*event*/)
{
    GuidVector vendors = botAI->GetAiObjectContext()->GetValue<GuidVector>("nearest npcs")->Get();
    std::vector<Item*> petitions = AI_VALUE2(std::vector<Item*>, "inventory items", chat->FormatQItem(5863));

    if (petitions.empty())
        return false;

    for (GuidVector::iterator i = vendors.begin(); i != vendors.end(); ++i)
    {
        ObjectGuid vendorguid = *i;
        Creature* pCreature = bot->GetNPCIfCanInteractWith(vendorguid, UNIT_NPC_FLAG_PETITIONER, UNIT_NPC_FLAG_2_NONE);
        if (!pCreature)
            continue;

        Item* petition = petitions.front();

        if (!petition)
            return false;

        WorldPacket raw(CMSG_TURN_IN_PETITION);
        WorldPackets::Petition::TurnInPetition data(std::move(raw));
        data.Item = petition->GetGUID();

        bot->GetSession()->HandleTurnInPetition(data);

        if (bot->GetGuildId())
        {
            // Ensure that bot has at least 10g for HandleSetEmblem can be managed core side
            // (EMBLEM_PRICE = 10 * GOLD in core)
            static constexpr uint32 REQUIRED = 10 * GOLD;
            uint32 have = bot->GetMoney();               // actual money earned by bot in copper
            if (have < REQUIRED)
            {
                bot->ModifyMoney(int32(REQUIRED - have)); // add only the missing amount to bot to reach 10g
            }

            Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());

            // 1.60 colors must come from the DB2 stores (see PlayerbotGuildMgr::SetRandomEmblem).
            auto pickColor = [](auto const& store) -> uint32
            {
                std::vector<uint32> ids;
                for (auto const* entry : store)
                    ids.push_back(entry->ID);
                return ids.empty() ? 0 : ids[urand(0, uint32(ids.size()) - 1)];
            };

            uint32 const bg = pickColor(sGuildColorBackgroundStore);
            uint32 const bc = pickColor(sGuildColorBorderStore);
            uint32 const cl = pickColor(sGuildColorEmblemStore);
            uint32 const br = urand(0, 7);
            uint32 const st = urand(0, 180);
            EmblemInfo emblemInfo(st, cl, br, bc, bg);

            guild->HandleSetEmblem(emblemInfo); // official core handling

            // LANG_GUILD_VETERAN -> can invite
            guild->HandleSetRankInfo(GuildRankId(2), {}, uint32(GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK | GR_RIGHT_INVITE));
        }

        return true;
    }

    TravelTarget* oldTarget = context->GetValue<TravelTarget*>("travel target")->Get();

    // Select a new target to travel to.
    TravelTarget newTarget = TravelTarget(botAI);

    bool foundTarget = SetNpcFlagTarget(&newTarget, {UNIT_NPC_FLAG_PETITIONER});

    if (!foundTarget || !newTarget.isActive())
        return false;

    newTarget.setRadius(INTERACTION_DISTANCE);

    setNewTarget(&newTarget, oldTarget);

    return true;
}

bool PetitionTurnInAction::isUseful()
{
    bool inCity = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->GetFlags().HasFlag(AreaFlags::LinkedChat))  // 12.x has no dedicated capital flag; cities are linked-chat areas
            inCity = true;
    }

    return inCity && !bot->GetGuildId() && AI_VALUE2(uint32, "item count", chat->FormatQItem(5863)) &&
           AI_VALUE(uint8, "petition signs") >= sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS) &&
           !context->GetValue<TravelTarget*>("travel target")->Get()->isTraveling();
}

bool BuyTabardAction::Execute(Event /*event*/)
{
    bool canBuy = botAI->DoSpecificAction("buy", Event("buy tabard", "Hitem:5976:"), true);
    if (canBuy && AI_VALUE2(uint32, "item count", chat->FormatQItem(5976)))
        return true;

    TravelTarget* oldTarget = context->GetValue<TravelTarget*>("travel target")->Get();

    // Select a new target to travel to.
    TravelTarget newTarget = TravelTarget(botAI);

    bool foundTarget = SetNpcFlagTarget(&newTarget, {UNIT_NPC_FLAG_TABARDDESIGNER}, "Tabard Vendor", {5976});

    if (!foundTarget || !newTarget.isActive())
        return false;

    newTarget.setRadius(INTERACTION_DISTANCE);

    setNewTarget(&newTarget, oldTarget);

    return true;
};

bool BuyTabardAction::isUseful()
{
    bool inCity = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->GetFlags().HasFlag(AreaFlags::LinkedChat))  // 12.x has no dedicated capital flag; cities are linked-chat areas
            inCity = true;
    }

    return inCity && bot->GetGuildId() && !AI_VALUE2(uint32, "item count", chat->FormatQItem(5976)) &&
           AI_VALUE2(uint32, "free money for", uint32(NeedMoneyFor::guild)) >= 10000 &&
           !context->GetValue<TravelTarget*>("travel target")->Get()->isTraveling();
}
