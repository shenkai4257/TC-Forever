/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AssassinationRogueStrategy.h"
#include "Playerbots.h"

class AssassinationRogueStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    AssassinationRogueStrategyActionNodeFactory()
    {
        creators["mutilate"] = &mutilate;
        creators["backstab"] = &backstab;
    }

private:
    static ActionNode* mutilate([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "mutilate",
            /*P*/ {},
            /*A*/ { NextAction("backstab") },
            /*C*/ {}
        );
    }
    static ActionNode* backstab([[maybe_unused]] PlayerbotAI* botAI)
    {
        return new ActionNode(
            "backstab",
            /*P*/ {},
            /*A*/ { NextAction("sinister strike") },
            /*C*/ {}
        );
    }
};

AssassinationRogueStrategy::AssassinationRogueStrategy(PlayerbotAI* botAI)
    : GenericRogueStrategy(botAI)
{
    actionNodeFactories.Add(new AssassinationRogueStrategyActionNodeFactory());
}

std::vector<NextAction> AssassinationRogueStrategy::getDefaultActions()
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

void AssassinationRogueStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericRogueStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "high energy available",
            {
                NextAction("garrote", ACTION_HIGH + 7),
                NextAction("ambush", ACTION_HIGH + 6)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "high energy available",
            {
                NextAction("mutilate", ACTION_NORMAL + 3)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "slice and dice",
            {
                NextAction("slice and dice", ACTION_HIGH + 5),
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "combo points 4 available",
            {
                NextAction("cold blood", ACTION_HIGH + 6),
                NextAction("eviscerate", ACTION_HIGH + 5)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "target with combo points almost dead",
            {
                NextAction("eviscerate", ACTION_HIGH + 4)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "expose armor",
            {
                NextAction("expose armor", ACTION_HIGH + 3),
            }
        )
    );
}
