/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BOTTICKEVENT_H
#define PLAYERBOTS_BOTTICKEVENT_H

#include "EventProcessor.h"

class Player;

// Drives a module-owned player once per player update: the bot AI, or the bot manager a real player
// owns (or both, for a self bot). The event lives in the player's own WorldObject::m_Events, which
// core drives from Unit::Update, so the tick runs on the same thread and at the same rate as the
// player itself - no core hook is involved.
//
// The event re-adds itself in Execute() and returns false, the same way core's SpellEvent keeps
// itself alive (Spell.cpp). It is deleted together with the player: ~EventProcessor() calls
// KillAllEvents(true).
class BotTickEvent : public BasicEvent
{
public:
    explicit BotTickEvent(Player* bot) : _bot(bot) { }

    bool Execute(uint64 eTime, uint32 pTime) override;

    // Installs a tick event on the player unless one is already queued. Safe to call when the AI or
    // manager is recreated: the event resolves both by GUID on every tick.
    static void Install(Player* bot);

    // True while this player's own tick is on the stack. Logging a bot out deletes the player object
    // synchronously, so a logout requested from inside its own tick has to be deferred - unwinding
    // through a destroyed object is not safe. The tick and the logout run on the same thread.
    static bool IsTickingPlayer(ObjectGuid guid);

private:
    Player* _bot;
};

#endif
