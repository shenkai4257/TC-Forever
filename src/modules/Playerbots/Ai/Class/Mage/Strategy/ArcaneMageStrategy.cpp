/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ArcaneMageStrategy.h"
#include "Playerbots.h"

// ===== Single Target Strategy =====
ArcaneMageStrategy::ArcaneMageStrategy(PlayerbotAI* botAI) : GenericMageStrategy(botAI)
{
}

// ===== Default Actions =====
std::vector<NextAction> ArcaneMageStrategy::getDefaultActions()
{
    return {
        NextAction("arcane missiles", 5.5f),
        NextAction("fire blast", 5.3f),       // cast while moving if arcane barrage isn't available/learned
        NextAction("frostbolt", 5.2f),        // for arcane immune targets
        NextAction("shoot", 5.1f)
    };
}

// ===== Trigger Initialization ===
void ArcaneMageStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericMageStrategy::InitTriggers(triggers);

    // Proc Trigger
}
