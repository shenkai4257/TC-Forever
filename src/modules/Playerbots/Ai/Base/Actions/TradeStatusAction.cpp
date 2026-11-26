/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TradeStatusAction.h"
#include "TradePackets.h"
#include "PacketOperators.h"
#include "TradeData.h"
#include "CraftValue.h"
#include "Event.h"
#include "GuildTaskMgr.h"
#include "ItemUsageValue.h"
#include "ItemVisitors.h"
#include "PlayerbotMgr.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SetCraftAction.h"

bool TradeStatusAction::Execute(Event event)
{
    if (IsSelfBot(bot))
        return false;

    if (!bot->GetSession())
        return false;

    Player* trader = bot->GetTrader();
    if (!trader)
        return false;

    bool const traderIsGameClientPlayer = IsRealPlayer(trader) || IsSelfBot(trader);
    Player* master = GetMaster();

    // Bots refuse to trade with a person (whether active or using SelfBot) who is neither their
    // master nor a group member. Bot traders (other than SelfBots) are handled further down.
    if (trader != master && traderIsGameClientPlayer &&
        (!bot->GetGroup() || !bot->GetGroup()->IsMember(trader->GetGUID())))
    {
        bot->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                         "trade_busy_now", "I'm kind of busy now", {}),
                     LANG_UNIVERSAL, trader);
        CancelTrade();
        return false;
    }

    if (sPlayerbotAIConfig.EnableRandomBotTrading == 0 &&
        (sRandomPlayerbotMgr.IsRandomBot(bot)|| sRandomPlayerbotMgr.IsAddclassBot(bot)))
    {
        bot->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                         "trade_disabled", "Trading is disabled", {}),
                     LANG_UNIVERSAL, trader);
        CancelTrade();
        return false;
    }

    // Bots also refuse their own master when ungrouped and security withholds full access.
    if ((!bot->GetGroup() || !bot->GetGroup()->IsMember(trader->GetGUID())) &&
        (trader != master || !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, master)) &&
        traderIsGameClientPlayer)
    {
        CancelTrade();
        return false;
    }

    WorldPacket p(event.getPacket());
    p.rpos(0);
    // 12.x packs the trade status into bits (1 bit bnet-account flag, 5 bit status); a plain uint32
    // read sees garbage, so the bot never reacted to a trade request.
    bool partnerIsSameBnetAccount = false;
    uint8 status = 0;
    p >> WorldPackets::Bits<1>(partnerIsSameBnetAccount);
    p >> WorldPackets::Bits<5>(status);

    // 12.x signals "the other side accepted" with TRADE_STATUS_ACCEPTED; there is no
    // BACK_TO_TRADE status anymore (un-accepting sends TRADE_STATUS_UNACCEPTED).
    if (status == TRADE_STATUS_ACCEPTED)
    {
        // Accept and everything that must observe its result (trade data gone = completed, craft
        // progress, guild tasks, discount restore) form one world-thread step: the trade completes
        // on the world thread and the partner may sit on any other map.
        if (!CheckTrade())
            return false;

        ObjectGuid const traderGuid = trader->GetGUID();
        uint32 const discount = sRandomPlayerbotMgr.GetTradeDiscount(bot, trader);
        botAI->RunOnWorldThread("trade accept", [traderGuid, discount](Player* bot)
        {
            Player* trader = ObjectAccessor::FindConnectedPlayer(traderGuid);
            PlayerbotAI* botAI = trader ? sPlayerbotsMgr.GetPlayerbotAI(bot) : nullptr;
            if (!trader || !botAI || !trader->GetTradeData() || !bot->GetTradeData())
                return;

            std::map<uint32, uint32> givenItemIds, takenItemIds;
            for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
            {
                Item* item = trader->GetTradeData()->GetItem((TradeSlots)slot);
                if (item)
                    givenItemIds[item->GetTemplate()->GetId()] += item->GetCount();

                item = bot->GetTradeData()->GetItem((TradeSlots)slot);
                if (item)
                    takenItemIds[item->GetTemplate()->GetId()] += item->GetCount();
            }

            WorldPacket raw(CMSG_ACCEPT_TRADE);
            WorldPackets::Trade::AcceptTrade accept(std::move(raw));
            // the server checks the accept against the partner's current server state index
            accept.StateIndex = trader->GetTradeData()->GetServerStateIndex();
            bot->GetSession()->HandleAcceptTradeOpcode(accept);
            if (bot->GetTradeData())
            {
                sRandomPlayerbotMgr.SetTradeDiscount(bot, trader, discount);
                return;
            }

            for (std::map<uint32, uint32>::iterator i = givenItemIds.begin(); i != givenItemIds.end(); ++i)
            {
                uint32 itemId = i->first;
                uint32 count = i->second;

                CraftData& craftData = botAI->GetAiObjectContext()->GetValue<CraftData&>("craft")->Get();
                if (!craftData.IsEmpty() && craftData.IsRequired(itemId))
                    craftData.AddObtained(itemId, count);

                GuildTaskMgr::instance().CheckItemTask(itemId, count, trader, bot);
            }

            for (std::map<uint32, uint32>::iterator i = takenItemIds.begin(); i != takenItemIds.end(); ++i)
            {
                uint32 itemId = i->first;
                uint32 count = i->second;

                CraftData& craftData = botAI->GetAiObjectContext()->GetValue<CraftData&>("craft")->Get();
                if (!craftData.IsEmpty() && craftData.itemId == itemId)
                    craftData.Crafted(count);
            }
        });
        return true;
    }
    else if (status == TRADE_STATUS_PROPOSED)
    {
        // Classic 1.60: the invited side gets PROPOSED and the window only opens once it answers
        // CMSG_BEGIN_TRADE (INITIATED is what both sides get afterwards). The proposer gets PROPOSED
        // too, naming its partner, and must not begin.
        if (!bot->GetTradeData() || bot->GetTradeData()->IsAwaitingProposerBusy())
            return false;

        if (!bot->HasInArc(CAST_ANGLE_IN_FRONT, trader, sPlayerbotAIConfig.SightDistance))
            bot->SetFacingToObject(trader);

        BeginTrade();
        return true;
    }
    else if (status == TRADE_STATUS_INITIATED)
    {
        // the window is open on both sides now: show the master what the bot can offer
        ListItemsVisitor visitor;
        IterateItems(&visitor);

        botAI->TellMaster("=== Inventory ===");
        TellItems(visitor.items, visitor.soulbound);

        if (sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            uint32 discount = sRandomPlayerbotMgr.GetTradeDiscount(bot, botAI->GetMaster());
            if (discount)
            {
                std::ostringstream out;
                out << "Discount up to: " << chat->formatMoney(discount);
                botAI->TellMaster(out);
            }
        }
        return true;
    }
    return false;
}

void TradeStatusAction::BeginTrade()
{
    Player* trader = bot->GetTrader();
    if (!trader || (GET_PLAYERBOT_AI(trader) && !IsSelfBot(trader)))
        return;

    botAI->RunOnWorldThread("trade begin", [](Player* bot) {
        WorldPacket raw(CMSG_BEGIN_TRADE);
        WorldPackets::Trade::BeginTrade begin(std::move(raw));
        bot->GetSession()->HandleBeginTradeOpcode(begin);
    });
}

void TradeStatusAction::CancelTrade()
{
    // A refusal answers the proposer the way a client's decline does (CMSG_IGNORE_TRADE): a plain
    // CANCELLED leaves the Classic 1.60 proposer's client waiting in "already trading".
    bot->TradeCancel(true, TRADE_STATUS_PLAYER_BUSY);
}

bool TradeStatusAction::CheckTrade()
{
    Player* trader = bot->GetTrader();
    if (!bot->GetTradeData() || !trader || !trader->GetTradeData())
        return false;

    if (!botAI->HasGameClientMaster() && GET_PLAYERBOT_AI(bot->GetTrader()) &&
        !IsSelfBot(bot->GetTrader()))
    {
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
        {
            Item* item = bot->GetTradeData()->GetItem((TradeSlots)slot);
            if (item)
                break;
        }
        bool isGettingItem = false;
        for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
        {
            Item* item = trader->GetTradeData()->GetItem((TradeSlots)slot);
            if (item)
            {
                isGettingItem = true;
                break;
            }
        }

        if (isGettingItem)
        {
            if (bot->GetGroup() && bot->GetGroup()->IsMember(bot->GetTrader()->GetGUID()) &&
                botAI->HasGameClientMaster())
            {
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_thank_you_player",
                    "Thank you %player",
                    {{"%player", chat->FormatWorldobject(bot->GetTrader())}}));
            }
            else
            {
                bot->Say(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                             "trade_thank_you_player",
                             "Thank you %player",
                             {{"%player", chat->FormatWorldobject(bot->GetTrader())}}),
                         (bot->GetTeamId() == TEAM_ALLIANCE ? LANG_COMMON : LANG_ORCISH));
            }
        }
        return isGettingItem;
    }

    uint32 accountId = bot->GetSession()->GetAccountId();
    if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
    {
        int32 botItemsMoney = CalculateCost(bot, true);
        int32 botMoney = bot->GetTradeData()->GetMoney() + botItemsMoney;
        int32 playerItemsMoney = CalculateCost(trader, false);
        int32 playerMoney = trader->GetTradeData()->GetMoney() + playerItemsMoney;
        if (playerMoney || botMoney)
            botAI->PlaySound(playerMoney < botMoney ? TEXT_EMOTE_SIGH : TEXT_EMOTE_THANK);

        return true;
    }

    int32 botItemsMoney = CalculateCost(bot, true);
    int32 botMoney = bot->GetTradeData()->GetMoney() + botItemsMoney;
    int32 playerItemsMoney = CalculateCost(trader, false);
    int32 playerMoney = trader->GetTradeData()->GetMoney() + playerItemsMoney;
    if (botItemsMoney > 0 && sPlayerbotAIConfig.EnableRandomBotTrading == 2 &&
        (sRandomPlayerbotMgr.IsRandomBot(bot)|| sRandomPlayerbotMgr.IsAddclassBot(bot)))
    {
        bot->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                         "trade_selling_disabled", "Selling is disabled.", {}),
                     LANG_UNIVERSAL, trader);
        return false;
    }
    if (playerItemsMoney && sPlayerbotAIConfig.EnableRandomBotTrading == 3 &&
        (sRandomPlayerbotMgr.IsRandomBot(bot)|| sRandomPlayerbotMgr.IsAddclassBot(bot)))
    {
        bot->Whisper(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                         "trade_buying_disabled", "Buying is disabled.", {}),
                     LANG_UNIVERSAL, trader);
        return false;
    }
    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = bot->GetTradeData()->GetItem((TradeSlots)slot);
        if (item && !item->GetTemplate()->GetSellPrice() && !item->GetTemplate()->IsConjuredConsumable())
        {
            std::ostringstream out;
            botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "trade_item_not_for_sale",
                "%item - This is not for sale",
                {{"%item", chat->FormatItem(item->GetTemplate())}}));
            botAI->PlaySound(TEXT_EMOTE_NO);
            return false;
        }

        item = trader->GetTradeData()->GetItem((TradeSlots)slot);
        if (item)
        {
            std::ostringstream out;
            out << item->GetTemplate()->GetId();
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
            if ((botMoney && !item->GetTemplate()->GetBuyPrice()) || usage == ITEM_USAGE_NONE)
            {
                std::ostringstream out;
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_item_not_needed",
                    "%item - I don't need this",
                    {{"%item", chat->FormatItem(item->GetTemplate())}}));
                botAI->PlaySound(TEXT_EMOTE_NO);
                return false;
            }
        }
    }

    if (!botMoney && !playerMoney)
        return true;

    if (!botItemsMoney && !playerItemsMoney)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "trade_no_items_error", "There are no items to trade", {}));
        return false;
    }

    int32 discount = (int32)sRandomPlayerbotMgr.GetTradeDiscount(bot, trader);
    int32 delta = playerMoney - botMoney;
    int32 moneyDelta = (int32)trader->GetTradeData()->GetMoney() - (int32)bot->GetTradeData()->GetMoney();
    bool success = false;
    if (delta < 0)
    {
        if (delta + discount >= 0)
        {
            if (moneyDelta < 0)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_discount_buy_only", "You can use discount to buy items only", {}));
                botAI->PlaySound(TEXT_EMOTE_NO);
                return false;
            }
            success = true;
        }
    }
    else
        success = true;

    if (success)
    {
        sRandomPlayerbotMgr.AddTradeDiscount(bot, trader, delta);
        switch (urand(0, 4))
        {
            case 0:
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_success_pleasure", "A pleasure doing business with you", {}));
                break;
            case 1:
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_success_fair_trade", "Fair trade", {}));
                break;
            case 2:
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_success_thanks", "Thanks", {}));
                break;
            case 3:
                botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "trade_success_off_with_you", "Off with you", {}));
                break;
        }

        botAI->PlaySound(TEXT_EMOTE_THANK);
        return true;
    }

    std::ostringstream out;
    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "trade_want_money_for_this",
        "I want %money for this",
        {{"%money", chat->formatMoney(-(delta + discount))}}));
    botAI->PlaySound(TEXT_EMOTE_NO);
    return false;
}

int32 TradeStatusAction::CalculateCost(Player* player, bool sell)
{
    Player* trader = bot->GetTrader();
    TradeData* data = player->GetTradeData();
    if (!data)
        return 0;

    uint32 sum = 0;
    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = data->GetItem((TradeSlots)slot);
        if (!item)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        if (proto->GetQuality() < ITEM_QUALITY_NORMAL)
            return 0;

        CraftData& craftData = AI_VALUE(CraftData&, "craft");
        if (!craftData.IsEmpty())
        {
            if (player == trader && !sell && craftData.IsRequired(proto->GetId()))
                continue;

            if (player == bot && sell && craftData.itemId == proto->GetId() && craftData.IsFulfilled())
            {
                sum += item->GetCount() * SetCraftAction::GetCraftFee(craftData);
                continue;
            }
        }

        if (sell)
            sum += item->GetCount() * proto->GetSellPrice() * sRandomPlayerbotMgr.GetSellMultiplier(bot);

        else
            sum += item->GetCount() * proto->GetBuyPrice() * sRandomPlayerbotMgr.GetBuyMultiplier(bot);

    }

    return sum;
}
