/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TradeAction.h"
#include "TradePackets.h"
#include "TradeData.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemCountValue.h"
#include "ItemVisitors.h"
#include "PlayerbotAI.h"

bool TradeAction::Execute(Event event)
{
    std::string const text = event.getParam();

    // If text starts with any excluded prefix, don't process it further.
    for (auto const& prefix : sPlayerbotAIConfig.TradeActionExcludedPrefixes)
    {
        if (text.find(prefix) == 0)
            return false;
    }

    if (!bot->GetTrader())
    {
        GuidVector guids = chat->parseGameobjects(text);
        Player* player = nullptr;

        for (auto& guid : guids)
            if (guid.IsPlayer())
                player = ObjectAccessor::FindPlayer(guid);

        if (!player && botAI->GetMaster())
            player = botAI->GetMaster();

        if (!player)
            return false;

        if (!player->GetTrader())
        {
            WorldPacket raw(CMSG_INITIATE_TRADE);
            WorldPackets::Trade::InitiateTrade packet(std::move(raw));
            packet.Guid = player->GetGUID();
            bot->GetSession()->HandleInitiateTradeOpcode(packet);
            return true;
        }
        else if (player->GetTrader() != bot)
            return false;
    }

    uint32 copper = chat->parseMoney(text);
    if (copper > 0)
    {
        WorldPacket raw(CMSG_SET_TRADE_GOLD);
        WorldPackets::Trade::SetTradeGold packet(std::move(raw));
        packet.Coinage = copper;
        bot->GetSession()->HandleSetTradeGoldOpcode(packet);
    }

    size_t pos = text.rfind(" ");
    int count = pos != std::string::npos ? atoi(text.substr(pos + 1).c_str()) : 1;

    std::vector<Item*> found = parseItems(text);
    if (found.empty())
        return false;

    uint32 traded = 0;
    for (Item* item : found)
    {
        if (!bot->GetTrader() || item->IsInTrade())
            continue;

        int8 slot = item->CanBeTraded() ? -1 : TRADE_SLOT_NONTRADED;
        if (TradeItem(item, slot) && slot != TRADE_SLOT_NONTRADED && ++traded >= uint32(count))
            break;
    }

    return true;
}

bool TradeAction::TradeItem(Item const* item, int8 slot)
{
    int8 tradeSlot = -1;
    Item* itemPtr = const_cast<Item*>(item);

    TradeData* pTrade = bot->GetTradeData();
    if ((slot >= 0 && slot < TRADE_SLOT_COUNT) && pTrade->GetItem(TradeSlots(slot)) == nullptr)
        tradeSlot = slot;

    if (slot == TRADE_SLOT_NONTRADED)
        pTrade->SetItem(TRADE_SLOT_NONTRADED, itemPtr);
    else
    {
        for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT && tradeSlot == -1; i++)
        {
            if (pTrade->GetItem(TradeSlots(i)) == itemPtr)
            {
                tradeSlot = i;

                WorldPacket raw(CMSG_CLEAR_TRADE_ITEM);
                WorldPackets::Trade::ClearTradeItem packet(std::move(raw));
                packet.TradeSlot = uint8(tradeSlot);
                bot->GetSession()->HandleClearTradeItemOpcode(packet);
                pTrade->SetItem(TradeSlots(i), nullptr);
                return true;
            }
        }

        for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT && tradeSlot == -1; i++)
        {
            if (pTrade->GetItem(TradeSlots(i)) == nullptr)
                tradeSlot = i;
        }
    }

    if (tradeSlot == -1)
        return false;

    WorldPacket raw(CMSG_SET_TRADE_ITEM);
    WorldPackets::Trade::SetTradeItem packet(std::move(raw));
    packet.TradeSlot = uint8(tradeSlot);
    packet.PackSlot = item->GetBagSlot();
    packet.ItemSlotInPack = item->GetSlot();
    bot->GetSession()->HandleSetTradeItemOpcode(packet);
    return true;
}
