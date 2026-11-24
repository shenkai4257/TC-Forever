/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AiObjectContext.h"
#include "ValueContext.h"

// Raid and dungeon value contexts are not ported (see BuildSharedActionContexts).
void AiObjectContext::BuildSharedValueContexts(
    SharedNamedObjectContextList<UntypedValue>& valueContexts)
{
    valueContexts.Add(new ValueContext());
}
