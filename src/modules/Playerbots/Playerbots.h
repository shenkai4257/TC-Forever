/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTS_H
#define PLAYERBOTS_PLAYERBOTS_H

#include "Define.h"

// 12.x's trimmed SKILL_* list has no first aid line (the client still uses
// skill line 129, which profession recipe items check).
constexpr uint32 SKILL_FIRST_AID = 129;

// Classic 1.60's bank has no flat item slots - everything is stored in the
// bank bag slots (63..72, Player.h BANK_SLOT_BAG_*). The ported AC loops over
// the flat range stay correct as no-ops instead of being rewritten everywhere.
constexpr uint8 BANK_SLOT_ITEM_START = 63;  // = BANK_SLOT_BAG_START
constexpr uint8 BANK_SLOT_ITEM_END = 63;    // = BANK_SLOT_BAG_START

// Classic 1.60 has no keyring container: keys live in the normal bags, so the
// ported keyring loops stay correct as no-ops.
constexpr uint8 KEYRING_SLOT_START = 0;
constexpr uint8 KEYRING_SLOT_END = 0;

#include "FieldHelper.h"
#include "PreparedStatementHelper.h"
#include "AiObjectContext.h"
#include "Group.h"
#include "Pet.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "SharedValueContext.h"
#include "PlayerbotCompat.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TravelNode.h"

std::vector<std::string> split(std::string const s, char delim);
void split(std::vector<std::string>& dest, std::string const str, char const* delim);
#ifndef WIN32
int strcmpi(char const* s1, char const* s2);
#endif

#define CAST_ANGLE_IN_FRONT (2.f * static_cast<float>(M_PI) / 3.f)
#define EMOTE_ANGLE_IN_FRONT (2.f * static_cast<float>(M_PI) / 6.f)

#define GET_PLAYERBOT_AI(object) sPlayerbotsMgr.GetPlayerbotAI(object)
#define GET_PLAYERBOT_MGR(object) sPlayerbotsMgr.GetPlayerbotMgr(object)

#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#define AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Get()

#define AI_VALUE_LAZY(type, name) context->GetValue<type>(name)->LazyGet()
#define AI_VALUE2_LAZY(type, name, param) context->GetValue<type>(name, param)->LazyGet()

#define AI_VALUE_REF(type, name) context->GetValue<type>(name)->RefGet()

#define SET_AI_VALUE(type, name, value) context->GetValue<type>(name)->Set(value)
#define SET_AI_VALUE2(type, name, param, value) context->GetValue<type>(name, param)->Set(value)
#define RESET_AI_VALUE(type, name) context->GetValue<type>(name)->Reset()
#define RESET_AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Reset()

#define PAI_VALUE(type, name) sPlayerbotsMgr.GetPlayerbotAI(player)->GetAiObjectContext()->GetValue<type>(name)->Get()
#define PAI_VALUE2(type, name, param) \
    sPlayerbotsMgr.GetPlayerbotAI(player)->GetAiObjectContext()->GetValue<type>(name, param)->Get()
#define GAI_VALUE(type, name) sSharedValueContext.getGlobalValue<type>(name)->Get()
#define GAI_VALUE2(type, name, param) sSharedValueContext.getGlobalValue<type>(name, param)->Get()

#endif
