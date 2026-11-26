/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_HUNTERACTIONS_H
#define PLAYERBOTS_HUNTERACTIONS_H

#include "AiObject.h"
#include "Event.h"
#include "GenericSpellActions.h"
#include "Unit.h"

class PlayerbotAI;
class Unit;

// Buff and Out of Combat Spells

class CastTrueshotAuraAction : public CastBuffSpellAction
{
public:
    CastTrueshotAuraAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "trueshot aura") {}
};


class CastAspectOfTheHawkAction : public CastBuffSpellAction
{
public:
    CastAspectOfTheHawkAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the hawk") {}
    bool isUseful() override;
};

class CastAspectOfTheMonkeyAction : public CastBuffSpellAction
{
public:
    CastAspectOfTheMonkeyAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the monkey") {}
};

class CastAspectOfTheWildAction : public CastBuffSpellAction
{
public:
    CastAspectOfTheWildAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the wild") {}
};

class CastAspectOfTheCheetahAction : public CastBuffSpellAction
{
public:
    CastAspectOfTheCheetahAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the cheetah") {}
};

class CastAspectOfThePackAction : public CastBuffSpellAction
{
public:
    CastAspectOfThePackAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the pack") {}
};

class CastAspectOfTheViperAction : public CastBuffSpellAction
{
public:
    CastAspectOfTheViperAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "aspect of the viper") {}
};

// Cooldown Spells

class CastRapidFireAction : public CastBuffSpellAction
{
public:
    CastRapidFireAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "rapid fire") {}
};

class CastDeterrenceAction : public CastBuffSpellAction
{
public:
    CastDeterrenceAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "deterrence") {}
};

class CastReadinessAction : public CastBuffSpellAction
{
public:
    CastReadinessAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "readiness") {}
};

class CastDisengageAction : public CastSpellAction
{
public:
    CastDisengageAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "disengage") {}
    bool Execute(Event event) override;
    bool isUseful() override;
};

// CC Spells

class CastScareBeastAction : public CastSpellAction
{
public:
    CastScareBeastAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "scare beast") {}
};

class CastScareBeastCcAction : public CastSpellAction
{
public:
    CastScareBeastCcAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "scare beast on cc") {}
    Value<Unit*>* GetTargetValue() override;
    bool Execute(Event event) override;
};

class CastFreezingTrap : public CastDebuffSpellAction
{
public:
    CastFreezingTrap(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "freezing trap") {}
    Value<Unit*>* GetTargetValue() override;
};

class CastWyvernStingAction : public CastDebuffSpellAction
{
public:
    CastWyvernStingAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "wyvern sting", true) {}
};


class CastConcussiveShotAction : public CastSnareSpellAction
{
public:
    CastConcussiveShotAction(PlayerbotAI* botAI) :
        CastSnareSpellAction(botAI, "concussive shot") {}
};

class CastIntimidationAction : public CastBuffSpellAction
{
public:
    CastIntimidationAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "intimidation", false, 5000) {}
    std::string const GetTargetName() override
    {
        return "pet target";
    }
};

// Threat Spells

class CastDistractingShotAction : public CastSpellAction
{
public:
    CastDistractingShotAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "distracting shot") {}
};


class CastFeignDeathAction : public CastBuffSpellAction
{
public:
    CastFeignDeathAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "feign death") {}
};

// Pet Spells

class FeedPetAction : public Action
{
public:
    FeedPetAction(PlayerbotAI* botAI) : Action(botAI, "feed pet") {}
    bool Execute(Event event) override;
};

class CastCallPetAction : public CastBuffSpellAction
{
public:
    CastCallPetAction(PlayerbotAI* botAI) : CastBuffSpellAction(botAI, "call pet") {}
};

class CastMendPetAction : public CastAuraSpellAction
{
public:
    CastMendPetAction(PlayerbotAI* botAI) : CastAuraSpellAction(botAI, "mend pet") {}
    std::string const GetTargetName() override
    {
        return "pet target";
    }
};

class CastRevivePetAction : public CastBuffSpellAction
{
public:
    CastRevivePetAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "revive pet") {}
};

class CastBestialWrathAction : public CastBuffSpellAction
{
public:
    CastBestialWrathAction(PlayerbotAI* botAI) :
        CastBuffSpellAction(botAI, "bestial wrath", false, 5000) {}
    std::string const GetTargetName() override
    {
        return "pet target";
    }
};

// Direct Damage Spells

class CastAutoShotAction : public CastSpellAction
{
public:
    CastAutoShotAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "auto shot") {}
    ActionThreatType getThreatType() override
    {
        return ActionThreatType::None;
    }
    bool isUseful() override;
};

class CastArcaneShotAction : public CastSpellAction
{
public:
    CastArcaneShotAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "arcane shot") {}
    bool isUseful() override;
};

class CastAimedShotAction : public CastSpellAction
{
public:
    CastAimedShotAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "aimed shot") {}
};

class CastChimeraShotAction : public CastSpellAction
{
public:
    CastChimeraShotAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "chimera shot") {}
};



// DoT/Debuff Spells

class CastHuntersMarkAction : public CastDebuffSpellAction
{
public:
    CastHuntersMarkAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "hunter's mark") {}
    bool isUseful() override
    {
        // Bypass TTL check
        return CastAuraSpellAction::isUseful();
    }
};

class CastTranquilizingShotAction : public CastSpellAction
{
public:
    CastTranquilizingShotAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "tranquilizing shot") {}
};

class CastViperStingAction : public CastDebuffSpellAction
{
public:
    CastViperStingAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "viper sting", true) {}
    bool isUseful() override;
};

class CastSerpentStingAction : public CastDebuffSpellAction
{
public:
    CastSerpentStingAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "serpent sting", true) {}
    bool isUseful() override
    {
        // Bypass TTL check
        return CastAuraSpellAction::isUseful();
    }
};

class CastScorpidStingAction : public CastDebuffSpellAction
{
public:
    CastScorpidStingAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "scorpid sting", true) {}
    bool isUseful() override
    {
        // Bypass TTL check
        return CastAuraSpellAction::isUseful();
    }
};

class CastSerpentStingOnAttackerAction : public CastDebuffSpellOnAttackerAction
{
public:
    CastSerpentStingOnAttackerAction(PlayerbotAI* botAI)
        : CastDebuffSpellOnAttackerAction(botAI, "serpent sting", true) {}
    bool isUseful() override
    {
        // Bypass TTL check
        return CastAuraSpellAction::isUseful();
    }
};

class CastImmolationTrapAction : public CastSpellAction
{
public:
    CastImmolationTrapAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "immolation trap") {}
    bool isUseful() override;
};

class CastExplosiveTrapAction : public CastSpellAction
{
public:
    CastExplosiveTrapAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "explosive trap") {}
};

class CastBlackArrowAction : public CastDebuffSpellAction
{
public:
    CastBlackArrowAction(PlayerbotAI* botAI) :
        CastDebuffSpellAction(botAI, "black arrow", true) {}
    bool isUseful() override
    {
        if (botAI->HasStrategy("trap weave", BOT_STATE_COMBAT))
            return false;
        // Bypass TTL check
        return CastAuraSpellAction::isUseful();
    }
};


// Rank 4

// Rank 3

// Rank 2

// Rank 1

// Melee Spells

class CastWingClipAction : public CastSpellAction
{
public:
    CastWingClipAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "wing clip") {}
    bool isUseful() override;
    std::vector<NextAction> getPrerequisites() override;
};

class CastRaptorStrikeAction : public CastSpellAction
{
public:
    CastRaptorStrikeAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "raptor strike") {}
};

class CastMongooseBiteAction : public CastSpellAction
{
public:
    CastMongooseBiteAction(PlayerbotAI* botAI) :
        CastSpellAction(botAI, "mongoose bite") {}
};

// AoE Spells

class CastMultiShotAction : public CastSpellAction
{
public:
    CastMultiShotAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "multi-shot") {}
};

class CastVolleyAction : public CastSpellAction
{
public:
    CastVolleyAction(PlayerbotAI* botAI) : CastSpellAction(botAI, "volley") {}
    ActionThreatType getThreatType() override
    {
        return ActionThreatType::Aoe;
    }
};

#endif
