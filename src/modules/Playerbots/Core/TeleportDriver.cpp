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

#include "Core/TeleportDriver.h"
#include "Core/BotSession.h"

#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MovementPackets.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace Playerbot
{
void DriveTeleportAck(BotSession* session)
{
    if (!session)
        return;

    Player* player = session->GetPlayer();
    // Deliberately no IsInWorld() gate: a far teleport removes the player from
    // the old map (IsInWorld=false) BEFORE the worldport ack, and manufacturing
    // that pending ack is exactly this driver's job - the gate would deadlock
    // every cross-map teleport (bot stuck between maps, not in-world).
    if (!player)
        return;

    if (player->GetTeleportState() == TeleportState::WaitingForWorldPortAck ||
        player->GetTeleportState() == TeleportState::WaitingForSuspendTokenResponse)
    {
        // A move generator from the old map must not resume toward its target
        // on the new map (V2: a bot walked back into the exit portal).
        if (!player->IsInFlight())
        {
            player->StopMoving();
            player->GetMotionMaster()->Clear();
            player->GetMotionMaster()->MoveIdle();
        }
        session->HandleMoveWorldportAck();
        return;
    }

    if (player->GetTeleportState() == TeleportState::WaitingForTeleportAck)
    {
        // The pre-teleport spline would walk the bot back; end it first.
        // Old spot saved before the ack: the observer resync below is
        // delivered from there so pre-teleport observers stay inside the
        // broadcast range (AC 3.3.5 pattern).
        Position const preTeleportPos = player->GetPosition();

        if (!player->IsInFlight() && !player->GetTransport())
        {
            player->StopMoving();
            player->GetMotionMaster()->Clear();
            player->GetMotionMaster()->MoveIdle();
        }

        WorldPacket data(CMSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
        data << player->GetGUID();
        data << int32(0);
        data << int32(GameTime::GetGameTimeMS());
        WorldPackets::Movement::MoveTeleportAck ackPacket(std::move(data));
        ackPacket.Read();
        session->HandleMoveTeleportAck(ackPacket);

        // A headless bot never sends client movement packets, so the only
        // observer-facing position update for the whole teleport is the
        // MoveUpdateTeleport the core broadcasts BEFORE the relocation while
        // m_movementInfo still holds the old spot - nearby clients freeze
        // there (real players self-heal with their own movement packets, a
        // bot never does). Re-send it afterwards the way AC 3.3.5 does:
        // fresh MovementInfo, broadcast from the old position.
        player->m_movementInfo.guid = player->GetGUID();
        player->m_movementInfo.time = GameTime::GetGameTimeMS();
        player->m_movementInfo.pos.Relocate(*player);

        Position const newPos = player->GetPosition();

        WorldPackets::Movement::MoveUpdateTeleport resync;
        resync.Status = &player->m_movementInfo;
        if (MovementForces const* forces = player->GetMovementForces())
            resync.MovementForces = *forces->GetForces();

        player->Relocate(preTeleportPos);
        player->SendMessageToSet(resync.Write(), false);
        player->Relocate(newPos);

        TC_LOG_DEBUG("playerbots", "[Teleport] {} observer resync: old ({:.1f}, {:.1f}, {:.1f}) -> new ({:.1f}, {:.1f}, {:.1f})",
            player->GetName(), preTeleportPos.GetPositionX(), preTeleportPos.GetPositionY(), preTeleportPos.GetPositionZ(),
            newPos.GetPositionX(), newPos.GetPositionY(), newPos.GetPositionZ());
    }
}
}
