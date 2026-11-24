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

#include "BotSession.h"
#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "LoginQueryHolder.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSocket.h"

namespace Playerbot
{
namespace
{
// The battlenet account a real login of this game account would carry. A name
// query for a bot and the client's whisper checks key on it; a game-account id
// with no matching battlenet row would make the bot "not exist on any account".
uint32 ResolveBnetAccountId(uint32 gameAccountId)
{
    uint32 bnet = ::Battlenet::AccountMgr::GetIdByGameAccount(gameAccountId);
    return bnet ? bnet : gameAccountId;
}
}

BotSession::BotSession(uint32 accountId, std::string name)
    : WorldSession(
        accountId,
        std::move(name),
        ResolveBnetAccountId(accountId),
        std::string(),                          // bnet email
        std::shared_ptr<WorldSocket>(),         // null socket (headless)
        SEC_PLAYER,
        EXPANSION_LEVEL_CURRENT,
        0,                                      // mute time
        std::string(),                          // OS
        Minutes(0),                             // timezone offset
        0,                                      // client build
        ClientBuild::VariantId{},
        LOCALE_enUS,
        0,                                      // recruiter
        false,                                  // is recruiter
        true)                                   // isBot=true
{
    // Loads toys/heirlooms/mounts/appearances and account data the same way a
    // real login does; the handshake packets it emits are dropped by
    // SendPacket below.
    InitializeSession();
}

bool BotSession::BeginLogin(ObjectGuid characterGuid)
{
    if (PlayerLoading() || GetPlayer())
    {
        TC_LOG_WARN("playerbots", "[BotSession] BeginLogin while already loading/loaded (account {})", GetAccountId());
        return false;
    }

    m_playerLoading = characterGuid;

    auto holder = std::make_shared<LoginQueryHolder>(GetAccountId(), characterGuid);
    if (!holder->Initialize())
    {
        TC_LOG_ERROR("playerbots", "[BotSession] LoginQueryHolder::Initialize failed for {}", characterGuid.ToString());
        m_playerLoading.Clear();
        return false;
    }

    AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete(
        [this](SQLQueryHolderBase const& done)
        {
            HandlePlayerLogin(static_cast<LoginQueryHolder const&>(done));
        });

    TC_LOG_INFO("playerbots", "[BotSession] async login submitted for {} (account {})",
                characterGuid.ToString(), GetAccountId());
    return true;
}

void BotSession::SendPacket(WorldPacket const* /*packet*/, bool /*forced*/)
{
    // No client: drop everything. (Guild-charter capture arrives with the
    // guild feature, not M0.)
}

bool BotSession::Update(uint32 /*diff*/, PacketFilter& updater)
{
    // The base Update is also called for every player from Map::Update
    // (MapSessionFilter pass, possibly on a map update thread). Everything
    // below belongs to the world pass only; a bot has no packets for the map
    // pass to handle.
    if (!updater.ProcessUnsafe())
        return true;

    // The base Update ends with `if (!m_Socket[REALM]) return false;`, which
    // fires every tick for a socketless bot - run only what matters here.
    ProcessQueryCallbacks();

    if (forceExit)
    {
        if (_player && !m_playerLogout)
        {
            // A bot must not stay on a battleground group's member list as an
            // "Offline" ghost after it logs out (V2 live observation).
            if (Group* group = _player->GetGroup(); group && group->isBGGroup())
                group->RemoveMember(_player->GetGUID(), GROUP_REMOVEMETHOD_LEAVE);
            LogoutPlayer(true);
        }
        return false;
    }
    return true;
}

void BotSession::KickPlayer(std::string_view reason)
{
    TC_LOG_INFO("playerbots", "[BotSession] KickPlayer: account={} reason='{}'", GetAccountId(), reason);
    forceExit = true;
}
}
