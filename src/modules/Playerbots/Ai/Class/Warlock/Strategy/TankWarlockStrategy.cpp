/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TankWarlockStrategy.h"

TankWarlockStrategy::TankWarlockStrategy(PlayerbotAI* botAI) : GenericWarlockStrategy(botAI)
{
    // No custom ActionNodeFactory needed
}

std::vector<NextAction> TankWarlockStrategy::getDefaultActions()
{
        return {
        NextAction("corruption", 0.90f),
        NextAction("curse of agony", 0.80f),
        NextAction("immolate", 0.70f),
        NextAction("searing pain", 0.60f),
        NextAction("shadow bolt", 0.50f),
        NextAction("shoot", 0.40f),
    };
}

void TankWarlockStrategy::InitTriggers(std::vector<TriggerNode*>& /*triggers*/)
{
}
