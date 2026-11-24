/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PLAYERBOTS_BOTSESSION_H
#define _PLAYERBOTS_BOTSESSION_H

// BotSession - headless WorldSession for a bot.
//
// Adapted from PlayerbotV2 (src/modules/PlayerbotV2/Session/BotSession.{h,cpp},
// GPL-2.0, same project) with the V2-specific parts removed.
//
// Lifecycle:
//   1. BotSessionMgr::LoginBot builds the session and calls BeginLogin(guid).
//   2. BeginLogin mirrors WorldSession::HandleContinuePlayerLogin: stash the
//      in-flight guid, build a LoginQueryHolder, submit it through
//      CharacterDatabase.DelayQueryHolder and let the callback run the core
//      HandlePlayerLogin path - the same route a real client takes.
//   3. The core brings the Player in-world. The manager's tick then attaches
//      the bot AI through the registry (OnPlayerLogin hook in the future).
//
// The base class already provides the bot doors in the forever mainline:
//   - ctor takes isBot=true -> _isBot, m_Address="bot", no online=1 write
//   - dtor skips socket close and the account online=0 write for bots
//   - Update skips the idle-disconnect check for bots
//   - LogoutPlayer's offline write is narrowed to this character for bots
// What is still needed here: the base Update() dereferences m_Socket in its
// cleanup block, which a socketless session cannot survive - so Update is
// hidden (non-virtual, static dispatch by the manager) exactly as in V2.

#include "ObjectGuid.h"
#include "WorldSession.h"

#include <string>
#include <string_view>

namespace Playerbot
{
class BotSession final : public WorldSession
{
public:
    BotSession(uint32 accountId, std::string name);
    ~BotSession() override = default;

    BotSession(BotSession const&) = delete;
    BotSession& operator=(BotSession const&) = delete;

    // Submit the async login query holder for the given character guid.
    bool BeginLogin(ObjectGuid characterGuid);

    // Per-tick driver (world tick window only). Hides the non-virtual base
    // Update for the reason given above. Returns false when KickPlayer set
    // forceExit, so the manager can reap the session.
    bool Update(uint32 diff, class PacketFilter& updater);

    // Set forceExit directly: the base implementation requires a socket and
    // is a no-op for bots.
    void KickPlayer(std::string_view reason);

    // No client: drop every outbound packet.
    void SendPacket(WorldPacket const* packet, bool forced = false) override;
};
}

#endif
