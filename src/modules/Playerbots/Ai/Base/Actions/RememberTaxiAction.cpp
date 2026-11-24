/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RememberTaxiAction.h"
#include "AiObjectContext.h"
#include "Event.h"
#include "LastMovementValue.h"

bool RememberTaxiAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    switch (p.GetOpcode())
    {
        case CMSG_ACTIVATE_TAXI:
        {
            LastMovement& movement = context->GetValue<LastMovement&>("last taxi")->Get();
            movement.taxiNodes.clear();
            movement.taxiNodes.resize(2);

            p >> movement.taxiMaster >> movement.taxiNodes[0] >> movement.taxiNodes[1];
            return true;
        }
    }

    return false;
}
