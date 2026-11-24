/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GossipHelloAction.h"
#include "NPCPackets.h"
#include "Event.h"
#include "GossipDef.h"
#include "Playerbots.h"

bool GossipHelloAction::Execute(Event event)
{
    ObjectGuid guid;

    WorldPacket& p = event.getPacket();
    if (p.empty())
    {
        Player* master = GetMaster();
        if (master)
            guid = master->GetTarget();
    }
    else
    {
        p.rpos(0);
        p >> guid;
    }

    std::string const text = event.getParam();
    int32 menuToSelect = -1;
    if (!text.empty())
        menuToSelect = atoi(text.c_str());

    return Execute(guid, menuToSelect, false);
}

void GossipHelloAction::TellGossipText(uint32 textId)
{
    if (!textId)
        return;

    // 12.x: NpcText holds up to MAX_NPC_TEXT_OPTIONS broadcast-text ids (AC's
    // plain Text_0/Text_1 pairs moved into BroadcastTextEntry).
    if (NpcText const* text = sObjectMgr->GetNpcText(textId))
    {
        for (uint8 i = 0; i < MAX_NPC_TEXT_OPTIONS; i++)
        {
            NpcTextData const& data = text->Data[i];
            if (!data.BroadcastTextID)
                continue;

            BroadcastTextEntry const* bct = sBroadcastTextStore.LookupEntry(data.BroadcastTextID);
            if (!bct)
                continue;

            std::string const text0 = bct->Text.Str[sWorld->GetDefaultDbcLocale()];
            if (!text0.empty())
                botAI->TellMasterNoFacing(text0);

            std::string const text1 = bct->Text1.Str[sWorld->GetDefaultDbcLocale()];
            if (!text1.empty())
                botAI->TellMasterNoFacing(text1);
        }
    }
}

void GossipHelloAction::TellGossipMenus()
{
    if (!bot->PlayerTalkClass)
        return;

    Creature* pCreature = bot->GetNPCIfCanInteractWith(GetMaster()->GetTarget(), UNIT_NPC_FLAG_NONE, UNIT_NPC_FLAG_2_NONE);
    GossipMenu& menu = bot->PlayerTalkClass->GetGossipMenu();
    if (pCreature)
    {
        uint32 textId = bot->GetGossipTextId(menu.GetMenuId(), pCreature);
        TellGossipText(textId);
    }

    GossipMenuItemContainer const& items = menu.GetMenuItems();
    for (GossipMenuItem const& item : items)
    {
        std::ostringstream out;
        out << "[" << item.OrderIndex << "] " << item.OptionText;
        botAI->TellMasterNoFacing(out.str());
    }
}

bool GossipHelloAction::ProcessGossip(int32 menuToSelect, bool silent)
{
    GossipMenu& menu = bot->PlayerTalkClass->GetGossipMenu();
    if (menuToSelect != -1 && !menu.GetItem(menuToSelect))
    {
        if (!silent)
            botAI->TellError("Unknown gossip option");
        return false;
    }

    // 12.x selects by GossipOptionID on a structured packet instead of
    // (menuId, index) on a forged one.
    GossipMenuItem const* picked = menu.GetItem(menuToSelect);
    WorldPacket payload(CMSG_GOSSIP_SELECT_OPTION, 16);
    WorldPackets::NPC::GossipSelectOption select(std::move(payload));
    select.GossipUnit = GetMaster()->GetTarget();
    select.GossipOptionID = picked ? picked->GossipOptionID : menuToSelect;
    select.GossipID = menu.GetMenuId();
    bot->GetSession()->HandleGossipSelectOptionOpcode(select);

    if (!silent)
        TellGossipMenus();

    return true;
}

bool GossipHelloAction::Execute(ObjectGuid guid, int32 menuToSelect, bool silent)
{
    if (!guid)
        return false;

    Creature* pCreature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE, UNIT_NPC_FLAG_2_NONE);
    if (!pCreature)
    {
        TC_LOG_DEBUG("playerbots",
                  "[PlayerbotMgr]: HandleMasterIncomingPacket - Received  CMSG_TALK_TO_GOSSIP {} not found or you can't "
                  "interact with him.",
                  guid.ToString().c_str());
        return false;
    }

    auto menuItemsBounds =
        sObjectMgr->GetGossipMenuItemsMapBounds(pCreature->GetCreatureTemplate()->GossipMenuIds[0]);
    if (menuItemsBounds.begin() == menuItemsBounds.end())
        return false;

    if (menuToSelect == -1)
    {
        WorldPacket payload(CMSG_TALK_TO_GOSSIP, 16);
        WorldPackets::NPC::Hello hello(std::move(payload));
        hello.Unit = guid;
        bot->GetSession()->HandleGossipHelloOpcode(hello);
        bot->SetFacingToObject(pCreature);

        if (!silent)
        {
            std::ostringstream out;
            out << "--- " << pCreature->GetName() << " ---";
            botAI->TellMasterNoFacing(out.str());
            TellGossipMenus();
        }
    }
    else if (!bot->PlayerTalkClass)
    {
        if (!silent)
            botAI->TellError("I need to talk first");
        return false;
    }
    else
    {
        if (!ProcessGossip(menuToSelect, silent))
            return false;
    }

    bot->TalkedToCreature(pCreature->GetEntry(), pCreature->GetGUID());
    return true;
}
