#include "ClassicTalentsApply.h"
#include "ClassicSpec.h"
#include "ClassicTalentBuilds.h"
#include "TalentCode.h"
#include "Config.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include "TraitMgr.h"
#include "TraitPackets.h"
#include "World.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
using ::Playerbot::Classic::TalentNode;
using ::Playerbot::Classic::TalentPick;

uint32 CountActiveTraitRanks(Player* bot)
{
    int32 const active_id = bot->m_activePlayerData->ActiveCombatTraitConfigID;
    UF::TraitConfig const* cfg = bot->GetTraitConfig(active_id);
    if (!cfg)
        return 0;
    uint32 ranks = 0;
    for (auto const& e : cfg->Entries)
        ranks += uint32(std::max<int32>(0, e.Rank));
    return ranks;
}

static std::vector<TraitEdgeEntry const*> const& TraitEdgesInto(int32 nodeId)
{
    static std::unordered_map<int32, std::vector<TraitEdgeEntry const*>> byRight;
    static std::once_flag once;
    static std::vector<TraitEdgeEntry const*> const none;
    std::call_once(once, []
    {
        for (TraitEdgeEntry const* edge : sTraitEdgeStore)
            byRight[edge->RightTraitNodeID].push_back(edge);
    });
    auto itr = byRight.find(nodeId);
    return itr != byRight.end() ? itr->second : none;
}

// Every talent with points in a row its tab has opened (5 points per row above it in the same tab), as Wowhead's calculator and a
// player's talent frame require. TraitMgr::ValidateConfig lets Forever talents through on fewer (T4, 11-12: an Arms bot held Anger
// Management on 9 points above it, a mage Arcane Subtlety with no point in the top row), and Wowhead dropped those from the bots'
// links. True when the class has no tab layout (nothing to check against).
// Playerbots.ClassicTalents.RowsPerTab = 0 turns the check off for our builds: the Forever trees of 70790-70862 have one trait currency
// for all three tabs, so the game opens a row by the points spent in any tab, and the builds were made for that (ClassicTalentBuilds.cpp).
// A code the owner gives always keeps the rule (Wowhead does).
template <typename Entries>
static bool TalentRowsOpen(Player const* bot, Entries const& entries, bool ownerCode = false)
{
    static bool const rowsPerTab = sConfigMgr->GetBoolDefault("Playerbots.ClassicTalents.RowsPerTab", true, true);
    if (!rowsPerTab && !ownerCode)
        return true;
    std::vector<std::vector<::Playerbot::Classic::TalentSlot>> const* tabs = ::Playerbot::Classic::GetTalentTabs(bot);
    if (!tabs)
        return true;
    std::vector<std::vector<uint8_t>> points;
    ::Playerbot::Classic::TalentCode::Layout layout;
    for (auto const& tab : *tabs)
    {
        std::vector<uint8_t>& held = points.emplace_back();
        std::vector<::Playerbot::Classic::TalentCode::Slot>& slots = layout.emplace_back();
        for (::Playerbot::Classic::TalentSlot const& slot : tab)
        {
            int32 rank = 0;
            for (auto const& e : entries)
                if (e.TraitNodeID == slot.TraitNodeID && e.TraitNodeEntryID == slot.TraitNodeEntryID)
                    rank = e.Rank;
            held.push_back(uint8_t(std::clamp<int32>(rank, 0, 9)));
            slots.push_back({ uint8(std::clamp<int32>(slot.MaxRanks, 0, 9)), slot.row });
        }
    }
    return ::Playerbot::Classic::TalentCode::RowsOpen(points, layout);
}

// The owner's own build, a Wowhead Forever talent calculator code ("change talents <link>", Classic::SetTalentCode): learned as
// written in place of ours. The ranks are bought in TalentCode::LevellingOrder (the tab with most points first, row by row), so a
// bot below 60 fills its main tab first and each new level's point goes on down the build. Like the build walk below, a bot whose
// level, code and ranks are what its last walk left is not walked again.

} // namespace

namespace Playerbot::Classic
{

TalentsApplyResult ApplyTalentCode(Player* bot, UF::TraitConfig const& active, std::string const& code)
{
    namespace TCode = ::Playerbot::Classic::TalentCode;
    std::vector<std::vector<::Playerbot::Classic::TalentSlot>> const* tabs = ::Playerbot::Classic::GetTalentTabs(bot);
    TCode::Parsed const parsed = TCode::Parse(code);
    if (!tabs || !parsed.ok())
        return TalentsApplyResult::NotKnown;
    TCode::Layout layout;
    for (auto const& tab : *tabs)
    {
        std::vector<TCode::Slot>& slots = layout.emplace_back();
        for (::Playerbot::Classic::TalentSlot const& slot : tab)
            slots.push_back({ uint8(std::clamp<int32>(slot.MaxRanks, 0, 9)), slot.row });
    }
    TCode::Check const check = TCode::Validate(parsed, layout);
    if (!check.ok())
        return TalentsApplyResult::InvalidTarget;

    auto const heldRanks = [](auto const& entries)
    {
        std::map<std::pair<int32, int32>, int32> ranks;
        for (auto const& e : entries)
            if (e.Rank > 0)
                ranks[{ e.TraitNodeID, e.TraitNodeEntryID }] = e.Rank;
        return ranks;
    };
    auto const countRanks = [](std::map<std::pair<int32, int32>, int32> const& ranks)
    {
        uint32 n = 0;
        for (auto const& [node, rank] : ranks)
            n += uint32(rank);
        return n;
    };
    struct CodeWalk { uint8 level; size_t code; uint32 ranks; };
    static std::mutex s_lock;
    static std::unordered_map<uint64, CodeWalk> s_walked;
    uint64 const key = bot->GetGUID().GetCounter();
    size_t const codeHash = std::hash<std::string>{}(code);
    uint32 const heldNow = countRanks(heldRanks(active.Entries));
    {
        std::lock_guard lock(s_lock);
        if (auto itr = s_walked.find(key); itr != s_walked.end() && itr->second.level == uint8(bot->GetLevel()) && itr->second.code == codeHash
            && itr->second.ranks == heldNow)
            return TalentsApplyResult::Ok;
    }

    // From the free (granted) ranks only: the old talents go, as a respec would take them.
    WorldPackets::Traits::TraitConfig cfg(active);
    std::erase_if(cfg.Entries, [](WorldPackets::Traits::TraitEntry const& e) { return e.GrantedRanks == 0; });
    for (WorldPackets::Traits::TraitEntry& e : cfg.Entries)
        e.Rank = 0;

    static uint32 const walkBudgetMs = sConfigMgr->GetIntDefault("Playerbots.ClassicTalents.WalkBudgetMs", 250, true);
    uint32 const startMs = getMSTime();
    uint32 validations = 0;
    std::vector<TCode::Step> const steps = TCode::LevellingOrder(parsed);
    // Walk the steps again until a pass buys nothing: a talent whose prerequisite sits to its right in the same row opens on the next pass.
    for (bool progress = true; progress && getMSTimeDiff(startMs, getMSTime()) <= walkBudgetMs; )
    {
        progress = false;
        for (TCode::Step const& step : steps)
        {
            ::Playerbot::Classic::TalentSlot const& slot = (*tabs)[step.tab][step.slot];
            auto itr = std::ranges::find_if(cfg.Entries, [&](WorldPackets::Traits::TraitEntry const& e)
            {
                return e.TraitNodeID == slot.TraitNodeID && e.TraitNodeEntryID == slot.TraitNodeEntryID;
            });
            if (itr == cfg.Entries.end())
            {
                WorldPackets::Traits::TraitEntry& added = cfg.Entries.emplace_back();
                added.TraitNodeID = slot.TraitNodeID;
                added.TraitNodeEntryID = slot.TraitNodeEntryID;
                itr = std::prev(cfg.Entries.end());
            }
            int32 const target = std::min<int32>(step.ranks, slot.MaxRanks - itr->GrantedRanks);
            while (itr->Rank < target)
            {
                ++itr->Rank;
                ++validations;
                if (TraitMgr::ValidateConfig(cfg, bot) != TraitMgr::LearnResult::Ok || !TalentRowsOpen(bot, cfg.Entries, true))
                {
                    --itr->Rank;
                    break;
                }
                progress = true;
            }
            if (!itr->Rank && !itr->GrantedRanks)
                cfg.Entries.erase(itr);
        }
    }
    TraitMgr::ValidateConfig(cfg, bot);   // leave the config in its validated state (subtree caches)

    auto const want = heldRanks(cfg.Entries);
    uint32 const placed = countRanks(want);
    bool const already = want == heldRanks(active.Entries);
    TC_LOG_INFO("playerbots", "[API::talents] {} L{} talent code {} ({}/{}/{}): {} of {} points {} ({} validations, {} ms)", bot->GetName(),
        uint32(bot->GetLevel()), code, check.perTab.size() > 0 ? check.perTab[0] : 0, check.perTab.size() > 1 ? check.perTab[1] : 0,
        check.perTab.size() > 2 ? check.perTab[2] : 0, placed, check.total, already ? "already in place" : "committed", validations,
        getMSTimeDiff(startMs, getMSTime()));
    if (!already)
        bot->UpdateTraitConfig(std::move(cfg), 0, /*withCastTime*/ false);
    {
        std::lock_guard lock(s_lock);
        if (s_walked.size() > 8192)
            s_walked.clear();
        s_walked[key] = { uint8(bot->GetLevel()), codeHash, placed };
    }
    return placed > 0 ? TalentsApplyResult::Ok : TalentsApplyResult::Locked;
}


TalentsApplyResult ApplyTalentCode(Player* bot, std::string const& code)
{
    if (!bot)
        return TalentsApplyResult::Other;

    UF::TraitConfig const* active = bot->GetTraitConfig(bot->m_activePlayerData->ActiveCombatTraitConfigID);
    if (!active)
        return TalentsApplyResult::NotKnown;

    return ApplyTalentCode(bot, *active, code);
}

TalentsApplyResult ApplyClassicTalents(Player* bot, bool force /*= false*/)
{
    if (!bot) return TalentsApplyResult::Other;
    if (bot->IsInCombat()) return TalentsApplyResult::Locked;

    UF::TraitConfig const* active = bot->GetTraitConfig(bot->m_activePlayerData->ActiveCombatTraitConfigID);
    if (!active) return TalentsApplyResult::NotKnown;

    // The owner gave it a build of its own: that one, not ours.
    if (std::string const code = ::Playerbot::Classic::GetTalentCode(bot->GetGUID()); !code.empty())
        return ApplyTalentCode(bot, *active, code);

    uint32 const spec = ::Playerbot::Classic::EffectiveSpecId(bot);
    auto const build = ::Playerbot::Classic::GetTalentBuild(bot->GetClass(), spec);
    if (build.empty()) return TalentsApplyResult::NotKnown;

    // The walk below validates the whole trait config once per rank and per refused pick (TraitMgr::FillOwnedCurrenciesMap and
    // MeetsTraitCondition each time), which is seconds of world thread for one bot. Companions ask for it on every upkeep and a group of
    // them logs in together (169 freeze: 60 s), so a bot whose level, specialization and purchased ranks are what its last walk left
    // is not walked again, and walks that are still needed are spaced by a duty cycle (the caller is told Locked and asks again later).
    struct TalentWalkState { uint8 level; uint32 spec; uint32 ranks; };
    static std::mutex s_walkLock;
    static std::unordered_map<uint64, TalentWalkState> s_walked;
    static std::atomic<uint32> s_nextWalkMs{ 0 };
    uint64 const walkKey = bot->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(s_walkLock);
        auto const walked = s_walked.find(walkKey);
        if (walked != s_walked.end() && walked->second.level == uint8(bot->GetLevel()) && walked->second.spec == spec
            && walked->second.ranks == CountActiveTraitRanks(bot))
            return TalentsApplyResult::Ok;
    }
    // Playerbots.ClassicTalents.ApplyOnLogin = 0 (default): a bot that already holds all the points of its level on the build its talents
    // say it has (171 froze with 17 walks in one second at login) is not walked again; one that changed spec or still has points to place is.
    static bool const applyOnLogin = sConfigMgr->GetBoolDefault("Playerbots.ClassicTalents.ApplyOnLogin", false, true);
    if (!applyOnLogin)
    {
        uint32 const held = CountActiveTraitRanks(bot);
        int32 const points = std::max<int32>(0, int32(bot->GetLevel()) - 9);
        // The bot must hold the build as it is defined now: every pick, in the build's order, up to the points of its level, at the ranks the
        // build lists. A build definition that changed (talent names corrected to Forever's nodes) or a spec pinned by a scenario fails this
        // and the bot is walked once; a pick the class trees have no node for cannot be held and does not count against the bot.
        bool holdsBuild = held > 0 && int32(held) >= points;
        // A bot walked before the row rule (TalentRowsOpen) may hold talents in rows its tab has not opened: walked once more.
        if (holdsBuild && !TalentRowsOpen(bot, active->Entries))
            holdsBuild = false;
        if (holdsBuild)
        {
            UF::TraitConfig const* activeCfg = bot->GetTraitConfig(bot->m_activePlayerData->ActiveCombatTraitConfigID);
            int32 placed = 0;
            for (::Playerbot::Classic::TalentPick const& pick : build)
            {
                if (placed >= points)
                    break;
                if (pick.filler)
                    continue;
                placed += pick.ranks;
                ::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, pick.name);
                if (!node)
                    continue;
                int32 rank = 0;
                for (UF::TraitEntry const& e : activeCfg->Entries)
                    if (e.TraitNodeID == node->TraitNodeID && e.TraitNodeEntryID == node->TraitNodeEntryID)
                        rank = e.Rank + e.GrantedRanks;
                if (rank < std::min<int32>(pick.ranks, node->MaxRanks))
                {
                    holdsBuild = false;
                    break;
                }
            }
        }
        if (holdsBuild)
        {
            std::lock_guard<std::mutex> lock(s_walkLock);
            s_walked[walkKey] = { uint8(bot->GetLevel()), spec, held };
            return TalentsApplyResult::Ok;
        }
    }
    // At most one walk per world update, whatever the duty cycle says.
    static std::atomic<uint32> s_lastWalkLoop{ 0 };
    uint32 const loopNow = World::m_worldLoopCounter.load();
    if (loopNow == s_lastWalkLoop.load() || int32(getMSTime() - s_nextWalkMs.load()) < 0)
        return TalentsApplyResult::Locked;
    s_lastWalkLoop.store(loopNow);
    uint32 const walkStartMs = getMSTime();

    // Start from the free (granted) ranks only and buy the build rank by
    // rank. ValidateConfig enforces tier gates and the points the bot's
    // level grants: the first rank it refuses ends that talent, and the
    // walk moves on (a later talent may sit in an open tier).
    WorldPackets::Traits::TraitConfig cfg(*active);
    std::erase_if(cfg.Entries, [](WorldPackets::Traits::TraitEntry const& e) { return e.GrantedRanks == 0; });
    for (WorldPackets::Traits::TraitEntry& e : cfg.Entries)
        e.Rank = 0;

    // A talent's tree: its TraitTree and sub-tree, and the currencies it spends. Forever puts a class's three Classic
    // tabs in one TraitTree; each tab's tier gates count that tab's own currency, which is what tells them apart.
    using TreeKey = std::tuple<uint32, int32, std::vector<int32>>;
    std::map<int32, TreeKey> treeByNode;
    auto treeOf = [&](int32 nodeId) -> TreeKey const&
    {
        auto [itr, inserted] = treeByNode.try_emplace(nodeId);
        if (inserted)
        {
            TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(nodeId);
            itr->second = { node ? node->TraitTreeID : 0, node ? node->TraitSubTreeID : 0, TraitMgr::GetCostCurrenciesForNode(nodeId) };
        }
        return itr->second;
    };
    std::vector<TreeKey> buildTrees;   // the trees the build spends in, in the order it first names them

    std::string unresolved;   // build talents the class trees have no node for, by name
    std::map<std::string, int32> rankOf;      // pick name -> ranks held when its last visit ended
    std::map<std::string, int32> refusedBy;   // pick name -> the last LearnResult that stopped its ranks (the reason a talent is missing)

    // ValidateConfig answers from the config and the player alone, and the player does not change during the walk, so a rank it refused stays refused
    // until another rank is bought. `cfgVersion` counts the ranks bought; a node or pick refused at the current version is not validated again.
    // Without it fillParents filled every ancestor once per path to it (a node under two parents that share a parent: that one twice, and so on
    // five levels up), and every refused pick, with its parents, was validated again after each leftover rank. On the 70532 client data (234,
    // 2026-10-23) the walk held the world thread past the 60 s freeze detector, inside ValidateConfig, when Karrz targeted or cast.
    uint32 cfgVersion = 0;
    std::map<int32, uint32> parentsTriedAt;      // node id -> cfgVersion when its parents were last filled
    std::map<std::string, uint32> pickTriedAt;   // pick name -> cfgVersion when its ranks were last refused

    // Whatever the data, one walk may not hold the world thread longer than this: a walk cut short keeps the ranks it validated, says so once per
    // class and spec, and the bot is not walked again until its level, spec or ranks change.
    static uint32 const walkBudgetMs = sConfigMgr->GetIntDefault("Playerbots.ClassicTalents.WalkBudgetMs", 250, true);
    uint32 validations = 0;
    bool cutShort = false;
    auto const overBudget = [&]
    {
        if (!cutShort && getMSTimeDiff(walkStartMs, getMSTime()) > walkBudgetMs)
            cutShort = true;
        return cutShort;
    };
    auto const validate = [&]
    {
        ++validations;
        TraitMgr::LearnResult const result = TraitMgr::ValidateConfig(cfg, bot);
        // The rows as a player's talent frame opens them, which the game's check does not hold Forever talents to.
        if (result == TraitMgr::LearnResult::Ok && !TalentRowsOpen(bot, cfg.Entries))
            return TraitMgr::LearnResult::NotEnoughTalentsInPrimaryTree;
        return result;
    };

    // A talent below another is refused until a parent node is filled to its maximum (ValidateConfig), and a build that takes a
    // parent only partway (3 of 5) loses everything under it. Buys the parents of a node, and theirs, up to their maximum ranks as
    // far as ValidateConfig allows; true when at least one rank was bought.
    // The class trees have edges both ways between two nodes (70532: ten pairs, 104708 <-> 104709 among them). Following one
    // back to a node still being filled (`path`) found that node's entry before it held a rank and erased it, under the
    // indexes the callers hold: the walk then wrote ranks past the end of the vector and erased from beyond it, which corrupted
    // the heap and spun the world thread in memmove until the freeze detector ended it (234, crashes 00:49 and 00:55).
    std::vector<int32> path;
    std::vector<TraitMgr::NodeEntryInfo> const configNodes = TraitMgr::GetNodeEntriesForConfig(cfg);
    std::function<bool(int32, int32)> fillParents = [&](int32 nodeId, int32 depth) -> bool
    {
        bool bought = false;
        if (depth > 4 || overBudget() || std::ranges::find(path, nodeId) != path.end())
            return false;
        // Filled already with the config as it stands: the same ranks would be refused again.
        if (auto [tried, first] = parentsTriedAt.try_emplace(nodeId, cfgVersion); !first)
        {
            if (tried->second == cfgVersion)
                return false;
            tried->second = cfgVersion;
        }
        path.push_back(nodeId);
        for (TraitEdgeEntry const* edge : TraitEdgesInto(nodeId))
        {
            if (overBudget())
                break;
            if (std::ranges::find(path, int32(edge->LeftTraitNodeID)) != path.end())
                continue;
            auto info = std::ranges::find(configNodes, edge->LeftTraitNodeID, &TraitMgr::NodeEntryInfo::TraitNodeID);
            if (info == configNodes.end())
                continue;

            auto itr = std::ranges::find_if(cfg.Entries, [&](WorldPackets::Traits::TraitEntry const& e)
            {
                return e.TraitNodeID == info->TraitNodeID && e.TraitNodeEntryID == info->TraitNodeEntryID;
            });
            bool const added = itr == cfg.Entries.end();
            if (added)
            {
                WorldPackets::Traits::TraitEntry& entry = cfg.Entries.emplace_back();
                entry.TraitNodeID = info->TraitNodeID;
                entry.TraitNodeEntryID = info->TraitNodeEntryID;
                itr = std::prev(cfg.Entries.end());
            }
            size_t const index = size_t(std::distance(cfg.Entries.begin(), itr));

            // the parent's own parents first, or its ranks are refused as well
            if (cfg.Entries[index].Rank + cfg.Entries[index].GrantedRanks < info->MaxRanks && fillParents(info->TraitNodeID, depth + 1))
                bought = true;
            // that call can add entries, but only after this one
            while (cfg.Entries[index].Rank + cfg.Entries[index].GrantedRanks < info->MaxRanks && !overBudget())
            {
                ++cfg.Entries[index].Rank;
                if (validate() != TraitMgr::LearnResult::Ok)
                {
                    --cfg.Entries[index].Rank;
                    break;
                }
                ++cfgVersion;
                bought = true;
            }
            // only the entry this call added: one that was there before belongs to a caller
            if (added && !cfg.Entries[index].Rank && !cfg.Entries[index].GrantedRanks)
                cfg.Entries.erase(cfg.Entries.begin() + index);
        }
        path.pop_back();
        return bought;
    };

    // Buys the pick's ranks as far as ValidateConfig allows; true when at least one rank was bought. `firstVisit` records
    // the pick's name and tree once (the walk below passes over every pick more than once).
    auto buyPick = [&](::Playerbot::Classic::TalentPick const& pick, bool firstVisit) -> bool
    {
        ::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, pick.name);
        if (!node)
        {
            if (firstVisit)
            {
                if (!unresolved.empty())
                    unresolved += ", ";
                unresolved += pick.name;
            }
            return false;
        }
        if (firstVisit && std::ranges::find(buildTrees, treeOf(node->TraitNodeID)) == buildTrees.end())
            buildTrees.push_back(treeOf(node->TraitNodeID));
        if (overBudget())
            return false;
        // refused with the config as it stands (its parents tried as well): nothing has been bought since that would let a rank through
        if (auto tried = pickTriedAt.find(pick.name); tried != pickTriedAt.end() && tried->second == cfgVersion)
            return false;

        auto entryItr = std::ranges::find_if(cfg.Entries, [&](WorldPackets::Traits::TraitEntry const& e)
        {
            return e.TraitNodeID == node->TraitNodeID && e.TraitNodeEntryID == node->TraitNodeEntryID;
        });
        if (entryItr == cfg.Entries.end())
        {
            WorldPackets::Traits::TraitEntry& added = cfg.Entries.emplace_back();
            added.TraitNodeID = node->TraitNodeID;
            added.TraitNodeEntryID = node->TraitNodeEntryID;
            entryItr = std::prev(cfg.Entries.end());
        }
        size_t const entryIndex = size_t(std::distance(cfg.Entries.begin(), entryItr));

        int32 const before = cfg.Entries[entryIndex].Rank;
        int32 const target = std::min<int32>(pick.ranks, node->MaxRanks - cfg.Entries[entryIndex].GrantedRanks);
        for (bool parentsTried = false; ; parentsTried = true)
        {
            while (cfg.Entries[entryIndex].Rank < target && !overBudget())
            {
                ++cfg.Entries[entryIndex].Rank;
                if (TraitMgr::LearnResult const result = validate(); result != TraitMgr::LearnResult::Ok)
                {
                    --cfg.Entries[entryIndex].Rank;
                    refusedBy[pick.name] = int32(AsUnderlyingType(result));
                    break;
                }
                ++cfgVersion;
                refusedBy.erase(pick.name);
            }
            // refused: fill the parent nodes to their maximum once and try the remaining ranks again
            if (parentsTried || cfg.Entries[entryIndex].Rank >= target || !fillParents(node->TraitNodeID, 0))
                break;
        }
        bool const bought = cfg.Entries[entryIndex].Rank > before;
        if (cfg.Entries[entryIndex].Rank < target)
            pickTriedAt[pick.name] = cfgVersion;
        rankOf[pick.name] = cfg.Entries[entryIndex].Rank;
        if (!cfg.Entries[entryIndex].Rank && !cfg.Entries[entryIndex].GrantedRanks)
            cfg.Entries.erase(cfg.Entries.begin() + entryIndex);
        return bought;
    };

    // A talent in a deeper tier is refused until the tab holds enough points (ValidateConfig's tier gates), and the build lists
    // talents in levelling order, so a single walk lost every talent whose tier the earlier picks had not opened yet (the arms
    // bot ended with no Deep Wounds, Two-Handed Weapon Specialization, Impale or Sweeping Strikes: damage_parity 2026-10-21).
    // Walk the build again until a pass buys nothing; the fillers come last, with the points left.
    for (bool filler : { false, true })
    {
        for (bool progress = true, firstVisit = true; progress; firstVisit = false)
        {
            progress = false;
            for (::Playerbot::Classic::TalentPick const& pick : build)
                if (pick.filler == filler && buyPick(pick, firstVisit))
                    progress = true;
        }
    }
    // Points the build couldn't place (a name Forever's trees spell differently, a prerequisite it doesn't list) were
    // left unspent: a level 60 paladin sat on 13 of 51 (Celestera, 2026-10-05). Spend the rest down the trees, the one
    // the build names first (a protection paladin's leftovers go to Protection, then Holy), each tree top tier first,
    // like a player filling out a spec. Trees the build never names come last, the one with most points first.
    {
        std::map<TreeKey, int32> spent;
        for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
            spent[treeOf(e.TraitNodeID)] += e.Rank;
        std::vector<TraitMgr::NodeEntryInfo> nodes = TraitMgr::GetNodeEntriesForConfig(cfg);
        auto posY = [](int32 nodeId) { TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(nodeId); return node ? node->PosY : 0; };
        auto posX = [](int32 nodeId) { TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(nodeId); return node ? node->PosX : 0; };
        std::ranges::stable_sort(nodes, [&](TraitMgr::NodeEntryInfo const& a, TraitMgr::NodeEntryInfo const& b)
        {
            TreeKey const& ta = treeOf(a.TraitNodeID);
            TreeKey const& tb = treeOf(b.TraitNodeID);
            if (ta != tb)
            {
                size_t const ra = size_t(std::ranges::find(buildTrees, ta) - buildTrees.begin());
                size_t const rb = size_t(std::ranges::find(buildTrees, tb) - buildTrees.begin());
                if (ra != rb)
                    return ra < rb;
                return spent[ta] != spent[tb] ? spent[ta] > spent[tb] : ta < tb;
            }
            if (posY(a.TraitNodeID) != posY(b.TraitNodeID))
                return posY(a.TraitNodeID) < posY(b.TraitNodeID);
            return posX(a.TraitNodeID) < posX(b.TraitNodeID);
        });
        // One rank per talent per pass, repeated until a pass places nothing: the first tree takes every point it can
        // (its deeper tiers open as it fills) before a point goes to the next one.
        // A level L bot has L - 9 points to spend (51 at 60). Every point bought ends the walk below, and the pass that finds nothing
        // left to place validates every talent of the class once more, so a bot whose points are all spent does not go through it.
        int32 const pointsForLevel = std::max<int32>(0, int32(bot->GetLevel()) - 9);
        auto const ranksBought = [&cfg]
        {
            int32 total = 0;
            for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
                total += std::max<int32>(0, e.Rank);
            return total;
        };
        for (bool placed = true; placed && ranksBought() < pointsForLevel && !overBudget(); )
        {
            placed = false;
            for (TraitMgr::NodeEntryInfo const& info : nodes)
            {
                if (overBudget())
                    break;
                auto entryItr = std::ranges::find_if(cfg.Entries, [&](WorldPackets::Traits::TraitEntry const& e)
                {
                    return e.TraitNodeID == info.TraitNodeID && e.TraitNodeEntryID == info.TraitNodeEntryID;
                });
                bool const added = entryItr == cfg.Entries.end();
                if (added)
                {
                    WorldPackets::Traits::TraitEntry& entry = cfg.Entries.emplace_back();
                    entry.TraitNodeID = info.TraitNodeID;
                    entry.TraitNodeEntryID = info.TraitNodeEntryID;
                    entryItr = std::prev(cfg.Entries.end());
                }
                if (entryItr->Rank + entryItr->GrantedRanks < info.MaxRanks)
                {
                    ++entryItr->Rank;
                    if (validate() == TraitMgr::LearnResult::Ok)
                    {
                        ++cfgVersion;
                        placed = true;
                        // A build talent refused for want of points in its tab (Vengeance, 147) is tried again as each rank lands: the
                        // leftover points would otherwise take all of what it needs by the time its tier opens.
                        std::vector<std::string> retry;
                        for (auto const& [name, result] : refusedBy)
                            retry.push_back(name);
                        for (std::string const& name : retry)
                            for (::Playerbot::Classic::TalentPick const& pick : build)
                                if (name == pick.name)
                                    buyPick(pick, false);
                        break;   // restart from the top: the preferred tree's next rank may have just opened
                    }
                    --entryItr->Rank;
                }
                if (added && !entryItr->Rank && !entryItr->GrantedRanks)
                    cfg.Entries.erase(entryItr);
            }
        }
    }
    // Leave the config in its validated state (subtree caches).
    TraitMgr::ValidateConfig(cfg, bot);

    auto ranksOf = [](auto const& entries)
    {
        std::map<std::pair<int32, int32>, int32> ranks;
        for (auto const& e : entries)
            if (e.Rank > 0)
                ranks[{ e.TraitNodeID, e.TraitNodeEntryID }] = e.Rank;
        return ranks;
    };
    std::map<std::pair<int32, int32>, int32> current;
    for (UF::TraitEntry const& e : active->Entries)
        if (e.Rank > 0)
            current[{ e.TraitNodeID, e.TraitNodeEntryID }] = e.Rank;
    bool const already = ranksOf(cfg.Entries) == current;   // already on the build for this level

    // Ranks per tree, the build's trees first: shows whether the tabs were told apart.
    std::map<TreeKey, int32> perTree;
    for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
        if (e.Rank > 0)
            perTree[treeOf(e.TraitNodeID)] += e.Rank;
    std::string split;
    for (TreeKey const& tree : buildTrees)
        split += (split.empty() ? "" : "/") + std::to_string(perTree.contains(tree) ? perTree[tree] : 0);
    for (auto const& [tree, ranks] : perTree)
        if (std::ranges::find(buildTrees, tree) == buildTrees.end())
            split += (split.empty() ? "" : "/") + std::to_string(ranks);

    // Every talent the walk left ranks in, by name (read before UpdateTraitConfig moves the config): the build's picks and the points the leftover pass
    // spent. Parity 200 (fire mage 575 against 743) had the same auras up as 184 and nothing to say which ranks differed.
    std::string heldRanks;
    if (!already && bot->GetLevel() >= 60)
        for (std::string const& n : ::Playerbot::Classic::ListTalentNames(bot))
            if (::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, n))
                for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
                    if (e.Rank > 0 && e.TraitNodeID == node->TraitNodeID && e.TraitNodeEntryID == node->TraitNodeEntryID)
                        heldRanks += fmt::format("{}{}={}/{}", heldRanks.empty() ? "" : ", ", n, e.Rank, node->MaxRanks);

    if (!already)
        bot->UpdateTraitConfig(std::move(cfg), 0, /*withCastTime*/ false);
    if (!unresolved.empty() && bot->GetLevel() >= 60)
    {
        // once per class and process: the names the client data gives this class's talents, to correct the build's
        static std::mutex s_namesLock;
        static std::set<uint8> s_namesLogged;
        std::lock_guard lock(s_namesLock);
        if (s_namesLogged.insert(bot->GetClass()).second)
        {
            std::string all;
            // with each node's row, the ranks this bot holds and the nodes it hangs under, so a build can be mapped onto the tab
            for (std::string const& n : ::Playerbot::Classic::ListTalentNames(bot))
            {
                std::string info;
                if (::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, n))
                {
                    TraitNodeEntry const* tn = sTraitNodeStore.LookupEntry(node->TraitNodeID);
                    int32 held = 0;
                    for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
                        if (e.TraitNodeID == node->TraitNodeID && e.TraitNodeEntryID == node->TraitNodeEntryID)
                            held = e.Rank;
                    std::string parents;
                    for (TraitEdgeEntry const* edge : TraitEdgesInto(node->TraitNodeID))
                        parents += (parents.empty() ? "" : "+") + std::to_string(edge->LeftTraitNodeID) + "t" + std::to_string(edge->Type);
                    info = fmt::format("[{} y{} {}/{}{}{}]", node->TraitNodeID, tn ? tn->PosY : -1, held, node->MaxRanks, parents.empty() ? "" : " under ", parents);
                }
                all += (all.empty() ? "" : ", ") + n + info;
            }
            TC_LOG_INFO("playerbots", "[API::talents] class {} talent names in the client data: {}", uint32(bot->GetClass()), all);
        }
    }
    std::string refused;   // build talents left short of their ranks, with ValidateConfig's LearnResult
    for (auto const& [name, result] : refusedBy)
        refused += (refused.empty() ? "" : ", ") + name + "=" + std::to_string(result) + " (holds " + std::to_string(rankOf.contains(name) ? rankOf[name] : 0) + ")";
    // The detail walks the edge store per refused talent: once per class and spec, and only for a bot with every point (a levelling bot's
    // refusals are its level; build 171 hung the world thread for 60 s with the lines written for every bot of the roster).
    static std::mutex s_detailLock;
    static std::set<uint32> s_detailLogged;
    bool logDetail = false;
    if (!refusedBy.empty() && bot->GetLevel() >= 60)
    {
        std::lock_guard lock(s_detailLock);
        logDetail = s_detailLogged.insert(uint32(bot->GetClass()) << 16 | spec).second;
    }
    if (logDetail)
    {
        // Where each build pick sits and what it holds, to see which tab's points the refused one is waiting for: node id, the currencies
        // its tab spends (Forever's three tabs share one tree and differ by currency), the posY row, and the ranks held.
        std::string detail;
        for (::Playerbot::Classic::TalentPick const& pick : build)
            if (::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, pick.name))
            {
                std::string currencies;
                for (int32 c : std::get<2>(treeOf(node->TraitNodeID)))
                    currencies += (currencies.empty() ? "" : "+") + std::to_string(c);
                TraitNodeEntry const* tn = sTraitNodeStore.LookupEntry(node->TraitNodeID);
                int32 held = 0;
                for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
                    if (e.TraitNodeID == node->TraitNodeID && e.TraitNodeEntryID == node->TraitNodeEntryID)
                        held = e.Rank;
                detail += fmt::format("{}{}[node {} cur {} y {} {}/{}]", detail.empty() ? "" : ", ", pick.name, node->TraitNodeID, currencies, tn ? tn->PosY : -1, held, node->MaxRanks);
            }
        TC_LOG_INFO("playerbots", "[API::talents] refused detail: {}", detail);
        // The nodes a refused pick hangs under: ValidateConfig refuses it (LearnResult 2) until a RequiredForAvailability parent is full.
        for (auto const& [name, result] : refusedBy)
            if (::Playerbot::Classic::TalentNode const* node = ::Playerbot::Classic::FindTalent(bot, name))
            {
                std::string parents;
                for (TraitEdgeEntry const* edge : TraitEdgesInto(node->TraitNodeID))
                {
                    auto info = std::ranges::find(configNodes, edge->LeftTraitNodeID, &TraitMgr::NodeEntryInfo::TraitNodeID);
                    int32 held = 0;
                    for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
                        if (info != configNodes.end() && e.TraitNodeID == info->TraitNodeID && e.TraitNodeEntryID == info->TraitNodeEntryID)
                            held = e.Rank + e.GrantedRanks;
                    parents += fmt::format("{}node {} edge type {} {}/{}", parents.empty() ? "" : ", ", edge->LeftTraitNodeID, edge->Type, held,
                                           info != configNodes.end() ? info->MaxRanks : -1);
                }
                TC_LOG_INFO("playerbots", "[API::talents] refused {} (node {}) has parents: {}", name, node->TraitNodeID, parents.empty() ? "none" : parents);
            }
    }
    TC_LOG_INFO("playerbots", "[API::talents] {} classic build for spec {} {}: {} ranks, per tree {} (build talents not found: {}; refused: {})",
        bot->GetName(), spec, already ? "already in place" : "committed", CountActiveTraitRanks(bot), split, unresolved.empty() ? "none" : unresolved, refused.empty() ? "none" : refused);
    if (!heldRanks.empty())
        TC_LOG_INFO("playerbots", "[API::talents] {} spec {} holds: {}", bot->GetName(), spec, heldRanks);
    {
        static std::mutex s_cutLock;
        static std::set<uint32> s_cutLogged;
        std::lock_guard lock(s_cutLock);
        if (cutShort && s_cutLogged.insert(uint32(bot->GetClass()) << 16 | spec).second)
            TC_LOG_ERROR("playerbots", "[API::talents] {} classic build for spec {} cut short after {} ms and {} validations ({} ranks placed, budget "
                "Playerbots.ClassicTalents.WalkBudgetMs = {}): the class trees make the walk too long", bot->GetName(), spec,
                getMSTimeDiff(walkStartMs, getMSTime()), validations, CountActiveTraitRanks(bot), walkBudgetMs);
    }
    {
        uint32 const now = getMSTime();
        s_nextWalkMs.store(now + 3 * getMSTimeDiff(walkStartMs, now));
        std::lock_guard<std::mutex> lock(s_walkLock);
        if (s_walked.size() > 8192)
            s_walked.clear();
        s_walked[walkKey] = { uint8(bot->GetLevel()), spec, CountActiveTraitRanks(bot) };
    }
    return CountActiveTraitRanks(bot) > 0 ? TalentsApplyResult::Ok : TalentsApplyResult::Locked;
}

} // namespace Playerbot::Classic
