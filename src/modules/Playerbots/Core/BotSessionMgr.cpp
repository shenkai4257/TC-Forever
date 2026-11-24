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

#include "BotSessionMgr.h"
#include "BotSession.h"

#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <fmt/format.h>

namespace Playerbot
{
BotSessionMgr::~BotSessionMgr()
{
    // Clearing the map runs each session's destructor, which logs out and
    // saves the player. Called from module Shutdown (world thread, after the
    // tick loop stopped).
    std::lock_guard lk(_mtx);
    _sessions.clear();
}

BotSessionMgr::LoginResult BotSessionMgr::LoginBot(ObjectGuid playerGuid)
{

    if (playerGuid.IsEmpty())
        return {false, "empty character guid"};

    if (ObjectAccessor::FindConnectedPlayer(playerGuid))
        return {false, "character already in-world"};

    {
        std::lock_guard lk(_mtx);
        if (_sessions.count(playerGuid))
            return {false, "login already in flight"};
    }

    // In-memory CharacterCache: no DB roundtrip per login.
    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
        return {false, "characters.account is 0 (cache miss / orphan character)"};

    std::string accountName;
    if (!sAccountMgr->GetName(accountId, accountName))
        accountName = fmt::format("acct{}", accountId);

    auto session = std::make_shared<BotSession>(accountId, std::move(accountName));
    if (!session->BeginLogin(playerGuid))
        return {false, "BotSession::BeginLogin failed (see server log)"};

    {
        std::lock_guard lk(_mtx);
        _sessions.emplace(playerGuid, session);
    }
    TC_LOG_INFO("playerbots", "[BotSessionMgr] login submitted for {} (account {})",
                playerGuid.ToString(), accountId);
    return {true, {}};
}

bool BotSessionMgr::LogoutBot(ObjectGuid playerGuid)
{

    std::shared_ptr<BotSession> sess = Find(playerGuid);
    if (!sess)
        return false;

    // KickPlayer flips forceExit; the next Update tick unwinds the session
    // through LogoutPlayer and we reap it.
    sess->KickPlayer("bot dismiss");
    return true;
}

uint32 BotSessionMgr::LogoutAll()
{

    std::vector<std::shared_ptr<BotSession>> snapshot;
    {
        std::lock_guard lk(_mtx);
        snapshot.reserve(_sessions.size());
        for (auto const& [guid, sess] : _sessions)
            snapshot.push_back(sess);
    }

    uint32 kicked = 0;
    for (auto const& sess : snapshot)
    {
        if (sess)
        {
            sess->KickPlayer("bot shutdown");
            ++kicked;
        }
    }
    return kicked;
}

size_t BotSessionMgr::Update(uint32 diff)
{

    // Snapshot under lock; tick and reap outside so a session destructor can
    // never run while the table lock is held.
    std::vector<std::pair<ObjectGuid, std::shared_ptr<BotSession>>> snapshot;
    {
        std::lock_guard lk(_mtx);
        snapshot.reserve(_sessions.size());
        for (auto const& [guid, sess] : _sessions)
            snapshot.emplace_back(guid, sess);
    }

    std::vector<ObjectGuid> dead;
    for (auto& [guid, sess] : snapshot)
    {
        WorldSessionFilter filter(sess.get());
        if (!sess->Update(diff, filter))
            dead.push_back(guid);
    }

    if (!dead.empty())
    {
        std::lock_guard lk(_mtx);
        for (ObjectGuid guid : dead)
            _sessions.erase(guid);
    }

    return snapshot.size() - dead.size();
}

size_t BotSessionMgr::active_count() const
{
    std::lock_guard lk(_mtx);
    return _sessions.size();
}

std::shared_ptr<BotSession> BotSessionMgr::Find(ObjectGuid playerGuid) const
{
    std::lock_guard lk(_mtx);
    auto it = _sessions.find(playerGuid);
    return it == _sessions.end() ? nullptr : it->second;
}

std::vector<ObjectGuid> BotSessionMgr::SnapshotGuids() const
{
    std::lock_guard lk(_mtx);
    std::vector<ObjectGuid> out;
    out.reserve(_sessions.size());
    for (auto const& [guid, sess] : _sessions)
        out.push_back(guid);
    return out;
}
}
