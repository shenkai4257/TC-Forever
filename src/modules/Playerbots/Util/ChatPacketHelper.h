/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHATPACKETHELPER_H
#define PLAYERBOTS_CHATPACKETHELPER_H

#include "ChatPackets.h"
#include "ObjectGuid.h"
#include "WorldPacket.h"

#include <string>

// 3.3.5 built chat packets with ChatHandler::BuildChatPacket; 12.x models them as
// WorldPackets::Chat::Chat. These keep the ported call sites readable.
namespace PlayerbotChat
{
    inline void BuildChatPacket(WorldPacket& out, ChatMsg type, Language lang, ObjectGuid sender, std::string const& /*receiverName*/,
                                std::string const& text, uint32 flags, std::string const& senderName)
    {
        WorldPackets::Chat::Chat packet;
        packet.SlashCmd = uint8(type);
        packet._Language = uint32(lang);
        packet.SenderGUID = sender;
        packet.SenderName = senderName;
        packet.ChatText = text;
        packet._ChatFlags = flags;
        out = *packet.Write();
    }

    inline void BuildChatPacket(WorldPacket& out, ChatMsg type, Language lang, WorldObject const* sender, WorldObject const* receiver,
                                std::string const& text)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(type, lang, sender, receiver, text);
        out = *packet.Write();
    }
}

#endif
