/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BeastMasteryHunterStrategy.h"
#include "Playerbots.h"

BeastMasteryHunterStrategy::BeastMasteryHunterStrategy(PlayerbotAI* botAI) : GenericHunterStrategy(botAI)
{
    // No custom ActionNodeFactory needed
}

// ===== Default Actions =====
std::vector<NextAction> BeastMasteryHunterStrategy::getDefaultActions()
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
void BeastMasteryHunterStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericHunterStrategy::InitTriggers(triggers);
    triggers.push_back(
        new TriggerNode(
            "intimidation",
            {
                NextAction("intimidation", 40.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "kill command",
            {
                NextAction("kill command", 18.5f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "target critical health",
            {
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "low mana",
            {
                NextAction("viper sting", 17.5f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "no stings",
            {
                NextAction("serpent sting", 17.0f)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "serpent sting on attacker",
            {
                NextAction("serpent sting on attacker", 16.5f)
            }
        )
    );
}
