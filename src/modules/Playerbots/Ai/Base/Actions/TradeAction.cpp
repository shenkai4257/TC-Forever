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
            ObjectGuid const partner = player->GetGUID();
            botAI->RunOnWorldThread("trade initiate", [partner](Player* bot) {
                WorldPacket raw(CMSG_INITIATE_TRADE);
                WorldPackets::Trade::InitiateTrade packet(std::move(raw));
                packet.Guid = partner;
                bot->GetSession()->HandleInitiateTradeOpcode(packet);
            });
            return true;
        }
        else if (player->GetTrader() != bot)
            return false;
    }

    uint32 copper = chat->parseMoney(text);
    if (copper > 0)
    {
        botAI->RunOnWorldThread("trade set gold", [copper](Player* bot) {
            WorldPacket raw(CMSG_SET_TRADE_GOLD);
            WorldPackets::Trade::SetTradeGold packet(std::move(raw));
            packet.Coinage = copper;
            bot->GetSession()->HandleSetTradeGoldOpcode(packet);
        });
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

        // The enchant slot is only for soulbound items; everything else (BoE included) goes to a
        // tradeable slot. CanBeTraded()'s auxiliary checks (loot GUID residue, unequip validation)
        // misfire for bots and would push unbound items into the enchant slot, making them
        // untradeable for the partner.
        int8 slot = item->IsSoulBound() ? TRADE_SLOT_NONTRADED : -1;
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

                botAI->RunOnWorldThread("trade clear item", [tradeSlot](Player* bot) {
                    WorldPacket raw(CMSG_CLEAR_TRADE_ITEM);
                    WorldPackets::Trade::ClearTradeItem packet(std::move(raw));
                    packet.TradeSlot = uint8(tradeSlot);
                    bot->GetSession()->HandleClearTradeItemOpcode(packet);
                });
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

    int8 const targetSlot = tradeSlot;
    uint8 const packSlot = item->GetBagSlot();
    uint8 const slotInPack = item->GetSlot();
    botAI->RunOnWorldThread("trade set item", [targetSlot, packSlot, slotInPack](Player* bot) {
        WorldPacket raw(CMSG_SET_TRADE_ITEM);
        WorldPackets::Trade::SetTradeItem packet(std::move(raw));
        packet.TradeSlot = uint8(targetSlot);
        packet.PackSlot = packSlot;
        packet.ItemSlotInPack = slotInPack;
        bot->GetSession()->HandleSetTradeItemOpcode(packet);
    });
    return true;
}
