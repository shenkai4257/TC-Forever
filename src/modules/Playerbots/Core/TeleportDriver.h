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

#ifndef _PLAYERBOTS_TELEPORT_DRIVER_H
#define _PLAYERBOTS_TELEPORT_DRIVER_H

// Teleport handshake completion for headless bots.
//
// Adapted from PlayerbotV2 (PlayerbotV2.cpp, DriveTeleportAck, GPL-2.0, same
// project).
//
// A real client answers Player::TeleportTo with CMSG_MOVE_TELEPORT_ACK (near
// teleport) or CMSG_MOVE_WORLDPORT_ACK (map change). A headless bot never
// answers, so the player sits in WaitingForTeleportAck and TeleportTo is
// silently dropped - the world position never advances (DragonCore policy
// finding D6). The module manufactures the ack every tick, calling the same
// session handlers the network layer would.
//
// Same-map teleports also stop the bot first: a spline from before the
// teleport would otherwise walk it straight back (V2 live observation, 2026-11-09).

namespace Playerbot
{
class BotSession;

// Drives any pending teleport ack. Call once per tick for every bot with a
// live session. No-op when no teleport is pending.
void DriveTeleportAck(BotSession* session);
}

#endif
