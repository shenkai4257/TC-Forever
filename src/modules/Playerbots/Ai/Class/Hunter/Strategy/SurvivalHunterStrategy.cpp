/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SurvivalHunterStrategy.h"
#include "Playerbots.h"

SurvivalHunterStrategy::SurvivalHunterStrategy(PlayerbotAI* botAI) : GenericHunterStrategy(botAI)
{
}

// ===== Default Actions =====
std::vector<NextAction> SurvivalHunterStrategy::getDefaultActions()
{
        return {
        NextAction("multi-shot", 0.90f),
        NextAction("serpent sting", 0.80f),
        NextAction("aimed shot", 0.70f),
        NextAction("arcane shot", 0.60f),
        NextAction("auto shot", 0.50f),
    };
}

// ===== Trigger Initialization ===
void SurvivalHunterStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericHunterStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "black arrow",
            {
                NextAction("black arrow", 16.5f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "low mana",
            {
                NextAction("viper sting", 16.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "no stings",
            {
                NextAction("serpent sting", 15.5f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "serpent sting on attacker",
            {
                NextAction("serpent sting on attacker", 15.0f)
            }
        )
    );
}
