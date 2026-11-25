/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "UseItemAction.h"
#include "TradeData.h"
#include "SpellPackets.h"
#include "GameObjectPackets.h"
#include "ItemTemplateHelper.h"
#include "AcceptQuestAction.h"
#include "ChatHelper.h"
#include "Event.h"
#include "ItemUsageValue.h"
#include "LootObjectStack.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "ServerFacade.h"

static constexpr uint32 SPELL_LEARNING_1 = 483;
static constexpr uint32 SPELL_LEARNING_2 = 55884;

bool UseItemAction::Execute(Event event)
{
    std::string name = event.getParam();
    if (name.empty())
        name = itemName;

    std::vector<Item*> items = AI_VALUE2(std::vector<Item*>, "inventory items", name);
    GuidVector gos = chat->parseGameobjects(name);

    if (gos.empty())
    {
        if (!items.empty())
        {
            return UseItemAuto(*items.begin());
        }
    }
    else
    {
        if (items.empty())
            return UseGameObject(*gos.begin());
        else
            return UseItemOnGameObject(*items.begin(), *gos.begin());
    }

    botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault("use_item_none_available",
                                                                      "No items (or game objects) available", {}));
    return false;
}

bool UseItemAction::UseGameObject(ObjectGuid guid)
{
    auto fail = [this](char const* name, char const* defaultText)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(name, defaultText, {}));
        return false;
    };

    GameObject* go = botAI->GetGameObject(guid);
    if (!go || !go->isSpawned())
        return fail("gameobject_unavailable_error", "Game object is no longer available");

    if (sPlayerbotAIConfig.DisallowedGameObjects.contains(go->GetEntry()))
        return fail("gameobject_disallowed_error", "Game object is disallowed by configuration");

    if (sPlayerbotAIConfig.LootDistance && bot->GetDistance(go) > sPlayerbotAIConfig.LootDistance)
        return fail("gameobject_outside_loot_distance_error", "Game object is outside the configured loot distance");

    if (go->HasFlag(GO_FLAG_NOT_SELECTABLE) ||
        (go->HasFlag(GO_FLAG_INTERACT_COND) && !go->ActivateToQuest(bot)))
        return fail("gameobject_not_eligible_error", "Game object is not currently eligible for interaction");

    if (!bot->IsAlive() || bot->IsInFlight() || (bot->IsCharmed() || bot->GetVehicle()) || bot->IsNonMeleeSpellCast(false) ||
        !bot->GetLootGUID().IsEmpty())
        return fail("gameobject_cannot_interact_error",
                    "Cannot interact while dead, flying, remotely controlled, casting, or looting");

    if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST || go->GetGOInfo()->GetLootId())
    {
        LootObject loot(bot, guid);
        if (!loot.IsLootPossible(bot))
            return fail("gameobject_cannot_loot_error",
                        "Cannot loot this object: check quest, skill, tools, key, and object state");

        bool inRange = go->IsAtInteractDistance(bot);
        if (botAI->HasStrategy("stay", BOT_STATE_NON_COMBAT) && bot->GetDistance(go) > CONTACT_DISTANCE)
            return fail("gameobject_stay_out_of_range_error", "Game object is out of reach while staying");

        bool canContinue =
            botAI->HasStrategy("loot", BOT_STATE_NON_COMBAT) || botAI->HasStrategy("gather", BOT_STATE_NON_COMBAT);
        if (!inRange && !canContinue)
            return fail("gameobject_approach_unavailable_error",
                        "Move closer or enable the loot or gather strategy to approach this object");

        LootObject previous = AI_VALUE(LootObject, "loot target");
        LootObjectStack* availableLoot = AI_VALUE(LootObjectStack*, "available loot");
        bool added = availableLoot->Add(guid);
        context->GetValue<LootObject>("loot target")->Set(loot);

        bool retryGuaranteed = inRange && (bot->isMoving() || bot->IsMounted());
        std::string objectName = chat->FormatGameobject(go);
        bool requested = botAI->DoSpecificAction(inRange ? "open loot" : "move to loot", Event(), true);
        if (!requested && !retryGuaranteed)
        {
            if (added && availableLoot->CanAttemptLoot(guid))
                availableLoot->Remove(guid);
            if (previous.guid != guid || availableLoot->CanAttemptLoot(previous.guid))
                context->GetValue<LootObject>("loot target")->Set(previous);
            else
                context->GetValue<LootObject>("loot target")->Set(LootObject());
            return fail("gameobject_open_failed_error", "Could not approach or open the game object");
        }

        botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            inRange && requested ? "gameobject_open_requested" : "gameobject_loot_queued",
            inRange && requested ? "Opening requested: %gameobject" : "Queued for looting: %gameobject",
            {{"%gameobject", objectName}}));
        return true;
    }

    if (go->GetGOInfo()->GetLockId() && go->HasFlag(GO_FLAG_LOCKED))
        return fail("gameobject_nonloot_locked_error", "This non-loot object requires an opening spell or key");

    if (!go->IsWithinDistInMap(bot, go->GetInteractionDistance()))
        return fail("gameobject_interact_out_of_range_error", "Move closer to interact with this game object");

    if (bot->isMoving())
        bot->StopMoving();
    ServerFacade::instance().SetFacingTo(bot, go);

    WorldPacket useRaw(CMSG_GAME_OBJ_USE);
    WorldPackets::GameObject::GameObjUse use(std::move(useRaw));
    use.Guid = guid;
    bot->GetSession()->HandleGameObjectUseOpcode(use);

    go = botAI->GetGameObject(guid);
    if (go && go->isSpawned() && go->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
    {
        WorldPacket reportRaw(CMSG_GAME_OBJ_REPORT_USE);
        WorldPackets::GameObject::GameObjReportUse report(std::move(reportRaw));
        report.Guid = guid;
        bot->GetSession()->HandleGameobjectReportUse(report);
    }

    botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "gameobject_interaction_requested", "Game object interaction requested", {}));
    return true;
}

bool UseItemAction::UseItemAuto(Item* item) { return UseItem(item, ObjectGuid::Empty, nullptr); }

bool UseItemAction::UseItemOnGameObject(Item* item, ObjectGuid go) { return UseItem(item, go, nullptr); }

bool UseItemAction::UseItemOnItem(Item* item, Item* itemTarget) { return UseItem(item, ObjectGuid::Empty, itemTarget); }

bool UseItemAction::UseItem(Item* item, ObjectGuid goGuid, Item* itemTarget, Unit* unitTarget)
{
    if (bot->CanUseItem(item) != EQUIP_ERR_OK)
        return false;

    if (bot->IsNonMeleeSpellCast(false))
        return false;

    uint8 bagIndex = item->GetBagSlot();
    uint8 slot = item->GetSlot();
    uint8 cast_count = 1;
    ObjectGuid item_guid = item->GetGUID();
    uint8 castFlags = 0;
    uint32 targetFlag = TARGET_FLAG_NONE;
    GameObject* goTarget = !goGuid.IsEmpty() ? botAI->GetGameObject(goGuid) : nullptr;
    if (!goGuid.IsEmpty() && (!goTarget || !goTarget->isSpawned()))
        return false;

    uint32 spellId = 0;
    ItemTemplate const* itemProto = item->GetTemplate();
    bool const isGenericLearnItem =
        PlayerbotItem::GetSpellId(itemProto, 0) == SPELL_LEARNING_1 || PlayerbotItem::GetSpellId(itemProto, 0) == SPELL_LEARNING_2;

    if (isGenericLearnItem)
    {
        if (bot->HasSpell(PlayerbotItem::GetSpellId(itemProto, 1)))
            return false;
    }
    else if (PlayerbotItem::GetSpellId(itemProto, 0))
    {
        // Older/direct layout: Spells[0] itself teaches the spell(s), via one or
        // more SPELL_EFFECT_LEARN_SPELL effects (not necessarily in effect slot 0).
        if (SpellInfo const* learnSpellInfo = sSpellMgr->GetSpellInfo(PlayerbotItem::GetSpellId(itemProto, 0), DIFFICULTY_NONE))
        {
            bool foundLearnEffect = false;
            bool allKnown = true;
            for (SpellEffectInfo const& effect : learnSpellInfo->GetEffects())
            {
                if (effect.Effect != SPELL_EFFECT_LEARN_SPELL || !effect.TriggerSpell)
                    continue;

                foundLearnEffect = true;
                if (!bot->HasSpell(effect.TriggerSpell))
                {
                    allKnown = false;
                    break;
                }
            }

            if (foundLearnEffect && allKnown)
                return false;
        }
    }

    // Only check the slot-0 spell for generic-learn items; slot 1 is the taught spell id.
    for (ItemEffectEntry const* itemEffect : itemProto->Effects)
    {
        if (!itemEffect || itemEffect->SpellID <= 0)
            continue;

        if (isGenericLearnItem && itemEffect->LegacySlotIndex != 0)
            continue;

        spellId = itemEffect->SpellID;
        bool canCast = goTarget ? botAI->CanCastSpell(spellId, goTarget, false, item)
                                : botAI->CanCastSpell(spellId, bot, false, itemTarget, item);
        if (!canCast)
            return false;
    }

    bool targetSelected = false;
    ObjectGuid targetUnitGuid = ObjectGuid::Empty;
    ObjectGuid targetItemGuid = ObjectGuid::Empty;

    // 12.x takes a structured UseItem with a SpellCastRequest (see GenericSpellActions).
    auto sendUseItem = [&]()
    {
        WorldPacket raw(CMSG_USE_ITEM);
        WorldPackets::Spells::UseItem useItem(std::move(raw));
        useItem.PackSlot = bagIndex;
        useItem.Slot = slot;
        useItem.CastItem = item_guid;
        useItem.Cast.CastID = item_guid;
        useItem.Cast.SpellID = int32(spellId);
        useItem.Cast.SendCastFlags = castFlags;
        useItem.Cast.Target.Flags = targetFlag;
        useItem.Cast.Target.Unit = targetUnitGuid;
        useItem.Cast.Target.Item = targetItemGuid;
        bot->GetSession()->HandleUseItemOpcode(useItem);
    };

    std::string itemText = chat->FormatItem(item->GetTemplate());
    std::string targetText;

    if (item->GetTemplate()->GetMaxStackSize() > 1)
    {
        uint32 count = item->GetCount();
        if (count > 1)
            itemText += " (" + std::to_string(count) + " available)";
        else
            itemText += " (the last one!)";
    }

    if (goTarget)
    {
        targetFlag = TARGET_FLAG_GAMEOBJECT;

        targetUnitGuid = goGuid;
        targetText = chat->FormatGameobject(goTarget);
        targetSelected = true;
    }

    if (itemTarget)
    {
        targetFlag = TARGET_FLAG_ITEM;
        targetItemGuid = itemTarget->GetGUID();
        targetText = chat->FormatItem(itemTarget->GetTemplate());
        targetSelected = true;
    }

    Player* master = GetMaster();
    if (!targetSelected && item->GetTemplate()->GetClass() != ITEM_CLASS_CONSUMABLE && master &&
        IsRealPlayer(botAI->GetMaster()) && !selfOnly)
    {
        if (ObjectGuid masterSelection = master->GetTarget(); !masterSelection.IsEmpty())
        {
            Unit* unit = botAI->GetUnit(masterSelection);
            if (unit)
            {
                targetFlag = TARGET_FLAG_UNIT;
                targetUnitGuid = masterSelection;
                targetText = unit->GetName();
                targetSelected = true;
            }
        }
    }

    if (!targetSelected && item->GetTemplate()->GetClass() != ITEM_CLASS_CONSUMABLE && unitTarget)
    {
        targetFlag = TARGET_FLAG_UNIT;
        targetUnitGuid = unitTarget->GetGUID();
        targetText = unitTarget->GetName();
        targetSelected = true;
    }

    if (uint32 questId = item->GetTemplate()->GetStartQuest())
    {
        if (Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId))
        {
            // The master-driven "use <item>" path accepts what the player could; the automatic
            // path filters with QuestAction::CanAcceptQuest before calling UseItem.
            AcceptQuestAction acceptQuestAction(botAI);
            return acceptQuestAction.AcceptQuest(qInfo, item_guid);
        }
    }

    bot->ClearUnitState(UNIT_STATE_CHASE);
    bot->ClearUnitState(UNIT_STATE_FOLLOW);

    if (bot->isMoving())
    {
        bot->StopMoving();
        botAI->SetNextCheckDelay(sPlayerbotAIConfig.GlobalCoolDown);
        return false;
    }

    for (ItemEffectEntry const* itemEffect : item->GetTemplate()->Effects)
    {
        if (!itemEffect || !itemEffect->SpellID)
            continue;

        uint32 spellId = itemEffect->SpellID;

        if (!botAI->CanCastSpell(spellId, bot, false))
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (spellInfo->Targets & TARGET_FLAG_ITEM)
        {
            Item* itemForSpell = AI_VALUE2(Item*, "item for spell", spellId);
            if (!itemForSpell)
                continue;

            if (itemForSpell->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
                continue;

            if (bot->GetTrader())
            {
                if (selfOnly)
                    return false;

                targetFlag = TARGET_FLAG_TRADE_ITEM;
                targetItemGuid = ObjectGuid::TradeItem;
                targetSelected = true;
                targetText = "traded item";
            }
            else
            {
                targetFlag = TARGET_FLAG_ITEM;
                targetItemGuid = itemForSpell->GetGUID();
                targetSelected = true;
                targetText = chat->FormatItem(itemForSpell->GetTemplate());
            }
            uint32 castTime = spellInfo->CalcCastTime();
            botAI->SetNextCheckDelay(castTime + sPlayerbotAIConfig.ReactDelay);
        }

        break;
    }

    if (!targetSelected)
    {
        targetFlag = TARGET_FLAG_UNIT;

        // Use the actual target if provided
        if (unitTarget)
        {
            targetUnitGuid = unitTarget->GetGUID();
            targetSelected = true;

            if (unitTarget == bot || !unitTarget->IsInWorld() || unitTarget->IsDuringRemoveFromWorld())
                targetText = "self";
            else if (unitTarget->IsHostileTo(bot))
                targetText = "self";
            else
                targetText = unitTarget->GetName();
        }
        else
        {
            targetUnitGuid = bot->GetGUID();
            targetSelected = true;
            targetText = "self";
        }
    }

    ItemTemplate const* proto = item->GetTemplate();
    bool isDrink = PlayerbotItem::GetSpellCategory(proto, 0) == 59;
    bool isFood = PlayerbotItem::GetSpellCategory(proto, 0) == 11;
    if (proto->GetClass() == ITEM_CLASS_CONSUMABLE &&
        (proto->GetSubClass() == ITEM_SUBCLASS_FOOD_DRINK || proto->GetSubClass() == ITEM_SUBCLASS_CONSUMABLE) && (isFood || isDrink))
    {
        if (bot->IsInCombat())
            return false;

        // bot->SetStandState(UNIT_STAND_STATE_SIT);
        bot->CastStop();
        float hp = bot->GetHealthPct();
        float mp = bot->GetPower(POWER_MANA) * 100.0f / bot->GetMaxPower(POWER_MANA);
        float p = 0.f;
        if (isDrink && isFood)
        {
            p = std::min(hp, mp);
            TellConsumableUse(item, "Feasting", p);
        }
        else if (isDrink)
        {
            p = mp;
            TellConsumableUse(item, "Drinking", p);
        }
        else if (isFood)
        {
            p = std::min(hp, mp);
            TellConsumableUse(item, "Eating", p);
        }

        if (!bot->IsInCombat() && !bot->InBattleground())
            SetDuration((uint32)std::max(10.0f * IN_MILLISECONDS, 27.0f * IN_MILLISECONDS * (100 - p) / 100.0f));

        if (!bot->IsInCombat() && bot->InBattleground())
            SetDuration((uint32)std::max(10.0f * IN_MILLISECONDS, 20.0f * IN_MILLISECONDS * (100 - p) / 100.0f));

        // botAI->SetNextCheckDelay(27000.0f * (100 - p) / 100.0f);
        //  botAI->SetNextCheckDelay(20000);
        sendUseItem();

        return true;
    }

    if (!spellId)
        return false;

    // botAI->SetNextCheckDelay(sPlayerbotAIConfig.GlobalCoolDown);
    std::string useText =
        targetSelected
            ? PlayerbotTextMgr::instance().GetBotTextOrDefault("use_item_on_target", "Using %item on %target",
                                                               {{"%item", itemText}, {"%target", targetText}})
            : PlayerbotTextMgr::instance().GetBotTextOrDefault("use_item", "Using %item", {{"%item", itemText}});
    botAI->TellMasterNoFacing(useText);
    sendUseItem();
    return true;
}

void UseItemAction::TellConsumableUse(Item* item, std::string const action, float percent)
{
    if (!sPlayerbotAIConfig.AnnounceConsumableUse)
        return;

    std::ostringstream out;
    out << action << " " << chat->FormatItem(item->GetTemplate());

    if (item->GetTemplate()->GetMaxStackSize() > 1)
        out << "/x" << item->GetCount();

    out << " (" << round(percent) << "%)";
    botAI->TellMasterNoFacing(out.str());
}

bool UseItemAction::isPossible() { return getName() == "use" || AI_VALUE2(uint32, "item count", itemName) > 0; }

bool UseSpellItemAction::isUseful() { return AI_VALUE2(bool, "spell cast useful", getName()); }

bool UseHealingPotion::isUseful() { return AI_VALUE2(bool, "combat", "self target"); }

bool UseManaPotion::isUseful() { return AI_VALUE2(bool, "combat", "self target"); }

bool UseHearthStone::Execute(Event event)
{
    if (bot->isMoving())
    {
        MotionMaster& mm = *bot->GetMotionMaster();
        bot->StopMoving();
        mm.Clear();
    }

    bool used = UseItemAction::Execute(event);
    if (used)
    {
        RESET_AI_VALUE(bool, "combat::self target");
        RESET_AI_VALUE(WorldPosition, "current position");
        botAI->SetNextCheckDelay(10 * IN_MILLISECONDS);
    }

    return used;
}

bool UseHearthStone::isUseful() { return !bot->InBattleground(); }

bool UseRandomRecipe::Execute(Event /*event*/)
{
    std::vector<Item*> recipes = AI_VALUE2(std::vector<Item*>, "inventory items", "recipe");

    std::string recipeName = "";

    for (auto& recipe : recipes)
    {
        recipeName = recipe->GetTemplate()->GetName(LocaleConstant(LOCALE_enUS));
    }

    if (recipeName.empty())
        return false;

    bool used = UseItemAction::Execute(Event(name, recipeName));

    if (used)
        botAI->SetNextCheckDelay(3.0 * IN_MILLISECONDS);

    return used;
}

bool UseRandomRecipe::isUseful()
{
    return !bot->IsInCombat() && !IsRealPlayer(botAI->GetMaster()) && !bot->InBattleground();
}

bool UseRandomRecipe::isPossible() { return AI_VALUE2(uint32, "item count", "recipe") > 0; }

bool UseStartQuestItem::Execute(Event /*event*/)
{
    for (Item* questItem : AI_VALUE2(std::vector<Item*>, "inventory items", "start quest"))
    {
        Quest const* qInfo = sObjectMgr->GetQuestTemplate(questItem->GetTemplate()->GetStartQuest());
        if (!QuestAction::CanAcceptQuest(bot, qInfo))
            continue;

        // UseItem can fail before the start-quest accept (e.g. a transient cast state), so keep
        // looking at the other eligible items instead of stopping at the first one.
        if (UseItem(questItem, ObjectGuid::Empty, nullptr, nullptr))
        {
            botAI->SetNextCheckDelay(sPlayerbotAIConfig.GlobalCoolDown);
            return true;
        }
    }

    return false;
}

bool UseStartQuestItem::isUseful()
{
    return !bot->IsInCombat() && !IsRealPlayer(botAI->GetMaster()) && !bot->InBattleground() &&
           !bot->HasUnitState(UNIT_STATE_IN_FLIGHT);
}

bool UseStartQuestItem::isPossible()
{
    for (Item* questItem : AI_VALUE2(std::vector<Item*>, "inventory items", "start quest"))
        if (QuestAction::CanAcceptQuest(bot, sObjectMgr->GetQuestTemplate(questItem->GetTemplate()->GetStartQuest())))
            return true;

    return false;
}
