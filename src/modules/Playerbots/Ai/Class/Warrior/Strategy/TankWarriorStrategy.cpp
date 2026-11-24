/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TankWarriorStrategy.h"

class TankWarriorStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    TankWarriorStrategyActionNodeFactory()
    {
        creators["charge"] = &charge;
        creators["sunder armor"] = &sunder_armor;
        creators["commanding shout"] = &commanding_shout;
        creators["devastate"] = &devastate;
        creators["last stand"] = &last_stand;
        creators["taunt"] = &taunt;
        creators["taunt spell"] = &taunt;
    }

private:
    static ActionNode* last_stand(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "last stand",
            /*P*/ {},
            /*A*/ { NextAction("intimidating shout") },
            /*C*/ {}
        );
    }

    static ActionNode* devastate(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "devastate",
            /*P*/ {},
            /*A*/ { NextAction("sunder armor") },
            /*C*/ {}
        );
    }

    static ActionNode* commanding_shout(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "commanding shout",
            /*P*/ {},
            /*A*/ { NextAction("battle shout") },
            /*C*/ {}
        );
    }

    static ActionNode* sunder_armor(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "sunder armor",
            /*P*/ {},
            /*A*/ { NextAction("melee") },
            /*C*/ {}
        );
    }

    static ActionNode* charge(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "charge",
            /*P*/ {},
            /*A*/ { NextAction("reach melee") },
            /*C*/ {}
        );
    }

    static ActionNode* taunt(PlayerbotAI* /*botAI*/)
    {
        return new ActionNode(
            "taunt",
            /*P*/ {},
            /*A*/ { NextAction("shield slam") },
            /*C*/ {}
        );
    }
};

TankWarriorStrategy::TankWarriorStrategy(PlayerbotAI* botAI) : GenericWarriorStrategy(botAI)
{
    actionNodeFactories.Add(new TankWarriorStrategyActionNodeFactory());
}

std::vector<NextAction> TankWarriorStrategy::getDefaultActions()
{
    // Classic 1.60 order (V2 Apl_Classic_Warrior kTankRules): shield slam > revenge > sunder >
    // demoralizing shout > thunder clap > cleave > heroic strike.
    return {
        NextAction("shield slam", ACTION_DEFAULT + 0.9f),
        NextAction("revenge", ACTION_DEFAULT + 0.8f),
        NextAction("sunder armor", ACTION_DEFAULT + 0.7f),
        NextAction("demoralizing shout", ACTION_DEFAULT + 0.6f),
        NextAction("thunder clap", ACTION_DEFAULT + 0.5f),
        NextAction("cleave", ACTION_DEFAULT + 0.4f),
        NextAction("heroic strike", ACTION_DEFAULT + 0.3f),
        NextAction("melee", ACTION_DEFAULT)
    };
}

void TankWarriorStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericWarriorStrategy::InitTriggers(triggers);

    triggers.push_back(
        new TriggerNode(
            "enemy out of melee",
            {
                NextAction("charge", ACTION_MOVE + 10)
            }
        )
    );

    triggers.push_back(
        new TriggerNode(
            "thunder clap and rage",
            {
                NextAction("thunder clap", ACTION_MOVE + 11)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "defensive stance",
            {
                NextAction("defensive stance", ACTION_HIGH + 9)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "commanding shout",
            {
                NextAction("commanding shout", ACTION_HIGH + 8)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "bloodrage",
            {
                NextAction("bloodrage", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "sunder armor",
            {
                NextAction("devastate", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium rage available",
            {
                NextAction("shield slam", ACTION_HIGH + 2),
                NextAction("devastate", ACTION_HIGH + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "shield block",
            {
                NextAction("shield block", ACTION_INTERRUPT + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "revenge",
            {
                NextAction("revenge", ACTION_HIGH + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "disarm",
            {
                NextAction("disarm", ACTION_HIGH + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "lose aggro",
            {
                NextAction("taunt", ACTION_INTERRUPT + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "low health",
            {
                NextAction("shield wall", ACTION_MEDIUM_HEAL)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "critical health",
            {
                NextAction("last stand", ACTION_EMERGENCY + 3),
                NextAction("enraged regeneration", ACTION_EMERGENCY + 2)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "high aoe",
            {
                NextAction("challenging shout", ACTION_HIGH + 3)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "concussion blow",
            {
                NextAction("concussion blow", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "shield bash",
            {
                NextAction("shield bash", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "shield bash on enemy healer",
            {
                NextAction("shield bash on enemy healer", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "victory rush",
            {
                NextAction("victory rush", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "sword and board",
            {
                NextAction("shield slam", ACTION_INTERRUPT)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "rend",
            {
                NextAction("rend", ACTION_NORMAL + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
        "rend on attacker",
            {
                NextAction("rend on attacker", ACTION_NORMAL + 1)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "high rage available",
            {
                NextAction("heroic strike", ACTION_HIGH)
            }
        )
    );
    triggers.push_back(
        new TriggerNode(
            "medium rage available",
            {
                NextAction("thunder clap", ACTION_HIGH + 1)
            }
        )
    );
}
