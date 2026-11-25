/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GenericRogueStrategy.h"
#include "Playerbots.h"

class GenericRogueStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    GenericRogueStrategyActionNodeFactory()
    {
        creators["use deadly poison on off hand"] = &use_deadly_poison_on_off_hand;
        creators["kick"] = &kick;
    }

private:
    static ActionNode* use_deadly_poison_on_off_hand([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "use deadly poison on off hand",
            /*P*/ {},
            /*A*/ { NextAction("use instant poison on off hand") },
            /*C*/ {}
        );
    }
    static ActionNode* kick([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "kick",
            /*P*/ {},
            /*A*/ { NextAction("kidney shot") },
            /*C*/ {}
        );
    }
};

GenericRogueStrategy::GenericRogueStrategy(PlayerbotAI* botAI) : CombatStrategy(botAI)
{
    actionNodeFactories.Add(new GenericRogueStrategyActionNodeFactory());
}

void GenericRogueStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    CombatStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "main hand weapon no enchant",
            {
                NextAction("use instant poison on main hand", ACTION_HIGH + 6)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "off hand weapon no enchant",
            {
                NextAction("use deadly poison on off hand", ACTION_HIGH + 5.5f)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "kick",
            {
                NextAction("kick", ACTION_INTERRUPT + 2)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "kick on enemy healer",
            {
                NextAction("kick on enemy healer", ACTION_INTERRUPT + 1)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "low health",
            {
                NextAction("evasion", ACTION_HIGH + 9),
                NextAction("feint", ACTION_HIGH + 8)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "medium threat",
            {
                NextAction("vanish", ACTION_HIGH)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "enemy out of melee",
            {
                NextAction("stealth", ACTION_HIGH + 3),
                NextAction("sprint", ACTION_HIGH + 2),
                NextAction("reach melee", ACTION_HIGH + 1)
            }
        )
    );
}

class StealthedRogueStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    StealthedRogueStrategyActionNodeFactory()
    {
        creators["ambush"] = &ambush;
        creators["sinister strike"] = &sinister_strike;
    }

private:
    static ActionNode* ambush([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "ambush",
            /*P*/ {},
            /*A*/ { NextAction("garrote") },
            /*C*/ {}
        );
    }

    static ActionNode* sinister_strike([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "sinister strike",
            /*P*/ {},
            /*A*/ { NextAction("cheap shot") },
            /*C*/ {}
        );
    }
};

StealthedRogueStrategy::StealthedRogueStrategy(PlayerbotAI* botAI) : Strategy(botAI)
{
    actionNodeFactories.Add(new StealthedRogueStrategyActionNodeFactory());
}

std::vector<NextAction> StealthedRogueStrategy::getDefaultActions()
{
        return {
        NextAction("slice and dice", 0.90f),
        NextAction("expose armor", 0.80f),
        NextAction("eviscerate", 0.70f),
        NextAction("backstab", 0.60f),
        NextAction("sinister strike", 0.50f),
        NextAction("melee", 0.10f),
    };
}

void StealthedRogueStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "combo points 5 available",
            {
                NextAction("eviscerate", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "kick",
            {
                NextAction("cheap shot", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "kick on enemy healer",
            {
                NextAction("cheap shot", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "behind target",
            {
                NextAction("ambush", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "not behind target",
            {
                NextAction("cheap shot", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "unstealth",
            {
                NextAction("unstealth", ACTION_NORMAL)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "no stealth",
            {
                NextAction("check stealth", ACTION_EMERGENCY)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "sprint",
            {
                NextAction("sprint", ACTION_INTERRUPT)
            }
        )
    );
}

void StealthStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "stealth",
            {
                NextAction("stealth", ACTION_INTERRUPT)
            }
        )
    );
}

void RogueAoeStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "light aoe",
            {
                NextAction("blade flurry", ACTION_HIGH + 4)
            }
        )
    );
}

void RogueBoostStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "adrenaline rush",
            {
                NextAction("adrenaline rush", ACTION_HIGH + 2)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "blade flurry",
            {
                NextAction("blade flurry", ACTION_HIGH + 4)
            }
        )
    );
}

void RogueCcStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode(
            "sap",
            {
                NextAction("stealth", ACTION_INTERRUPT),
                NextAction("sap", ACTION_INTERRUPT)
            }
        )
    );
}
