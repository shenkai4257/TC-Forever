/*
 * Classic talent application for the Classic 1.60 (Forever) trait system: the ported V2 apply walk
 * (PlayerbotV2 Api/PlayerbotAPI.cpp apply_classic_talents), converted to free functions. Every walk
 * guard is kept verbatim: the s_walked skip (same level/spec/ranks is not walked again), the
 * ApplyOnLogin hold check, the one-walk-per-world-loop duty cycle, the per-walk budget
 * (Playerbots.ClassicTalents.WalkBudgetMs) and the cfgVersion/fillParents freeze guards.
 */

#ifndef PLAYERBOTS_CLASSICTALENTSAPPLY_H
#define PLAYERBOTS_CLASSICTALENTSAPPLY_H

#include "Define.h"
#include <string>

class Player;

namespace Playerbot::Classic
{
enum class TalentsApplyResult : uint8
{
    Ok,             // ranks placed (or the bot already holds the build)
    Locked,         // in combat, duty cycle or budget says: ask again later
    NotKnown,       // no active trait config or no build for the class/spec
    InvalidTarget,  // the talent code's layout does not fit the bot's class tabs
    Other
};

// Applies the spec's static build (Api/ClassicTalentBuilds) rank by rank. A bot whose level,
// specialization and purchased ranks match its last walk is skipped (Ok without walking).
// force = true bypasses the one-walk-per-world-loop duty cycle but keeps the walk budget
// (factory context: batches of bots at creation, the world is quiet).
TalentsApplyResult ApplyClassicTalents(Player* bot, bool force = false);

// Learns an explicit Wowhead Forever talent calculator code in place of the static build
// (the T5 command face uses this).
TalentsApplyResult ApplyTalentCode(Player* bot, std::string const& code);
}
#endif
