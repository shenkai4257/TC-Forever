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

#ifndef _PLAYERBOTS_BOTSESSIONMGR_H
#define _PLAYERBOTS_BOTSESSIONMGR_H

// BotSessionMgr - owns the headless WorldSessions of the module.
//
// Adapted from PlayerbotV2 (src/modules/PlayerbotV2/Session/BotSessionMgr.{h,cpp},
// GPL-2.0, same project).
//
// Bot sessions are NOT registered with sWorld->AddSession(). That path kicks
// any other session on the same account, which would log out the GM spawning
// bots from their own characters; we keep a parallel table instead and trade
// the world's queueing/MaxPlayers logic (irrelevant for bots) for predictable
// lifecycles.
//
// Every method here runs on the world thread inside the module tick window
// (Policy::AssertWorldThread guards the entries).

#include "ObjectGuid.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Playerbot
{
class BotSession;

class BotSessionMgr
{
public:
    BotSessionMgr() = default;
    ~BotSessionMgr();

    BotSessionMgr(BotSessionMgr const&) = delete;
    BotSessionMgr& operator=(BotSessionMgr const&) = delete;

    struct LoginResult { bool ok = false; std::string reason; };

    // Begin a headless login for the character with this guid.
    LoginResult LoginBot(ObjectGuid playerGuid);

    // Ask an active bot session to log out (idempotent).
    bool LogoutBot(ObjectGuid playerGuid);

    // Ask every active bot session to log out. Returns how many were asked.
    uint32 LogoutAll();

    // Per-tick driver: advances each session (async login callbacks, logout
    // dance) and reaps finished ones. Returns the number of live sessions.
    size_t Update(uint32 diff);

    size_t active_count() const;

    // Session for a character, or nullptr. Valid only during the tick window.
    std::shared_ptr<BotSession> Find(ObjectGuid playerGuid) const;

    // Snapshot of live guids (diagnostics).
    std::vector<ObjectGuid> SnapshotGuids() const;

private:
    mutable std::mutex _mtx;
    std::unordered_map<ObjectGuid, std::shared_ptr<BotSession>> _sessions;
};
}

#endif
