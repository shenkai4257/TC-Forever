/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GossipHelloAction.h"
#include "NPCPackets.h"
#include "Event.h"
#include "GossipDef.h"
#include "PlayerbotTextMgr.h"
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
            botAI->TellError(PlayerbotTextMgr::instance().GetBotText("chat_gossip_unknown_option", {}));
        return false;
    }

    // 12.x selects by GossipOptionID on a structured packet instead of
    // (menuId, index) on a forged one. The select runs on the world thread: gossip options can
    // trigger SmartScripts (teleports, game events) and guild reads. The refreshed menu is read
    // inside the same step - TellGossipMenus must observe what the handler produced.
    GossipMenuItem const* picked = menu.GetItem(menuToSelect);
    ObjectGuid const gossipUnit = GetMaster()->GetTarget();
    uint32 const optionId = picked ? picked->GossipOptionID : uint32(menuToSelect);
    uint32 const gossipId = menu.GetMenuId();
    bool const tell = !silent;
    botAI->RunOnWorldThread("gossip select", [gossipUnit, optionId, gossipId, tell](Player* bot)
    {
        WorldPacket payload(CMSG_GOSSIP_SELECT_OPTION, 16);
        WorldPackets::NPC::GossipSelectOption select(std::move(payload));
        select.GossipUnit = gossipUnit;
        select.GossipOptionID = optionId;
        select.GossipID = gossipId;
        bot->GetSession()->HandleGossipSelectOptionOpcode(select);

        if (tell && sPlayerbotsMgr.GetPlayerbotAI(bot))
        {
            // Same job TellGossipMenus does, on the AI resolved at execution time.
            GossipMenu& newMenu = bot->PlayerTalkClass->GetGossipMenu();
            for (GossipMenuItem const& item : newMenu.GetMenuItems())
            {
                std::ostringstream out;
                out << "[" << item.OrderIndex << "] " << item.OptionText;
                sPlayerbotsMgr.GetPlayerbotAI(bot)->TellMasterNoFacing(out.str());
            }
        }
    });

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
            botAI->TellError(PlayerbotTextMgr::instance().GetBotText("chat_gossip_talk_first", {}));
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
