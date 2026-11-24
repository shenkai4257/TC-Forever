/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ActionContext.h"
#include "AiObjectContext.h"
#include "ChatActionContext.h"
#include "WorldPacketActionContext.h"

// Raid and dungeon action contexts are not ported: classic 1.60 ships no per-instance bot
// strategies here yet, so only the shared base contexts are registered.
void AiObjectContext::BuildSharedActionContexts(
    SharedNamedObjectContextList<Action>& actionContexts)
{
    actionContexts.Add(new ActionContext());
    actionContexts.Add(new ChatActionContext());
    actionContexts.Add(new WorldPacketActionContext());
}
