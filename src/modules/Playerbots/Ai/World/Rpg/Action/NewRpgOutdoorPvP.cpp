/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgOutdoorPvP.h"

// Outdoor PvP objectives (Nagrand, Terokkar, ...) are TBC content and not part of the
// 1.6x realm. The action stays registered but never selects an objective so the RPG
// state machine falls through to the other activities.
bool NewRpgOutdoorPvpAction::Execute(Event /*event*/)
{
    return false;
}

OPvPCapturePoint* NewRpgOutdoorPvpAction::SelectNewObjective(OutdoorPvP::OPvPCapturePointMap const& /*capturePointMap*/)
{
    return nullptr;
}

bool NewRpgOutdoorPvpAction::PatrolCapturePoint(GameObject* /*objectiveGO*/, float /*radius*/)
{
    return false;
}
