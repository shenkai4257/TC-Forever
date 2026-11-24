/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotAIConfig.h"
#include "BisListMgr.h"
#include "Config.h"
#include "NewRpgInfo.h"
#include "PlayerbotDungeonRepository.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "Playerbots.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "TravelMgr.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <utility>

template <class T>
void LoadList(std::string const value, T& list)
{
    std::vector<std::string> ids = split(value, ',');
    for (std::vector<std::string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        uint32 id = atoi((*i).c_str());
        // if (!id)
        //     continue;
        list.push_back(id);
    }
}

template <class T>
void LoadSet(std::string const value, T& set)
{
    std::vector<std::string> ids = split(value, ',');
    for (std::vector<std::string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        uint32 id = atoi((*i).c_str());
        // if (!id)
        //     continue;
        set.insert(id);
    }
}

template <class T>
void LoadListString(std::string const value, T& list)
{
    std::vector<std::string> strings = split(value, ',');
    for (std::vector<std::string>::iterator i = strings.begin(); i != strings.end(); i++)
    {
        std::string const string = *i;
        if (string.empty())
            continue;

        list.push_back(string);
    }
}

// Parses a comma-separated, whitespace-tolerant bot name list (as used by both
// Playerbots.LevelBrackets.ExcludeNames and Playerbots.ResetBotLevel.ExcludeNames) into out.
static void ParseLevelMgrExcludeNames(std::string const& csv, std::vector<std::string>& out)
{
    out.clear();
    std::istringstream f(csv);
    std::string s;
    while (getline(f, s, ','))
    {
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
        if (!s.empty())
            out.push_back(s);
    }
}

bool PlayerbotAIConfig::Initialize()
{
    TC_LOG_INFO("server.loading", "Initializing mod-playerbots, based on AI Playerbots by ike3 and the original Playerbots by blueboy");

    Enabled = sConfigMgr->GetBoolDefault("Playerbots.Enabled", true);
    if (!Enabled)
    {
        TC_LOG_INFO("server.loading", "Playerbots Module is disabled in playerbots.conf");
        return false;
    }

    GlobalCoolDown = sConfigMgr->GetIntDefault("Playerbots.GlobalCooldown", 500);
    MaxWaitForMove = sConfigMgr->GetIntDefault("Playerbots.MaxWaitForMove", 5000);
    DisableMoveSplinePath = sConfigMgr->GetIntDefault("Playerbots.DisableMoveSplinePath", 0);
    MaxMovementSearchTime = sConfigMgr->GetIntDefault("Playerbots.MaxMovementSearchTime", 3);
    ExpireActionTime = sConfigMgr->GetIntDefault("Playerbots.ExpireActionTime", 5000);
    DispelAuraDuration = sConfigMgr->GetIntDefault("Playerbots.DispelAuraDuration", 700);
    ReactDelay = sConfigMgr->GetIntDefault("Playerbots.ReactDelay", 100);
    DynamicReactDelay = sConfigMgr->GetBoolDefault("Playerbots.DynamicReactDelay", true);
    PassiveDelay = sConfigMgr->GetIntDefault("Playerbots.PassiveDelay", 10000);
    RepeatDelay = sConfigMgr->GetIntDefault("Playerbots.RepeatDelay", 2000);
    ErrorDelay = sConfigMgr->GetIntDefault("Playerbots.ErrorDelay", 100);
    RpgDelay = sConfigMgr->GetIntDefault("Playerbots.RpgDelay", 10000);
    SitDelay = sConfigMgr->GetIntDefault("Playerbots.SitDelay", 20000);
    ReturnDelay = sConfigMgr->GetIntDefault("Playerbots.ReturnDelay", 2000);
    LootDelay = sConfigMgr->GetIntDefault("Playerbots.LootDelay", 1000);
    DisabledWithoutRealPlayerLoginDelay = sConfigMgr->GetIntDefault("Playerbots.DisabledWithoutRealPlayerLoginDelay", 30);
    DisabledWithoutRealPlayerLogoutDelay = sConfigMgr->GetIntDefault("Playerbots.DisabledWithoutRealPlayerLogoutDelay", 300);

    FarDistance = sConfigMgr->GetFloatDefault("Playerbots.FarDistance", 20.0f);
    SightDistance = sConfigMgr->GetFloatDefault("Playerbots.SightDistance", 100.0f);
    SpellDistance = sConfigMgr->GetFloatDefault("Playerbots.SpellDistance", 28.5f);
    ShootDistance = sConfigMgr->GetFloatDefault("Playerbots.ShootDistance", 5.0f);
    HealDistance = sConfigMgr->GetFloatDefault("Playerbots.HealDistance", 38.5f);
    LootDistance = sConfigMgr->GetFloatDefault("Playerbots.LootDistance", 15.0f);
    FleeDistance = sConfigMgr->GetFloatDefault("Playerbots.FleeDistance", 5.0f);
    AggroDistance = sConfigMgr->GetFloatDefault("Playerbots.AggroDistance", 22.0f);
    TooCloseDistance = sConfigMgr->GetFloatDefault("Playerbots.TooCloseDistance", 5.0f);
    MeleeDistance = sConfigMgr->GetFloatDefault("Playerbots.MeleeDistance", 0.75f);
    FollowDistance = sConfigMgr->GetFloatDefault("Playerbots.FollowDistance", 1.5f);
    WhisperDistance = sConfigMgr->GetFloatDefault("Playerbots.WhisperDistance", 6000.0f);
    ContactDistance = sConfigMgr->GetFloatDefault("Playerbots.ContactDistance", 0.45f);
    AoeRadius = sConfigMgr->GetFloatDefault("Playerbots.AoeRadius", 10.0f);
    RpgDistance = sConfigMgr->GetFloatDefault("Playerbots.RpgDistance", 200.0f);
    GrindDistance = sConfigMgr->GetFloatDefault("Playerbots.GrindDistance", 75.0f);
    ReactDistance = sConfigMgr->GetFloatDefault("Playerbots.ReactDistance", 150.0f);

    CriticalHealth = sConfigMgr->GetIntDefault("Playerbots.CriticalHealth", 25);
    LowHealth = sConfigMgr->GetIntDefault("Playerbots.LowHealth", 45);
    MediumHealth = sConfigMgr->GetIntDefault("Playerbots.MediumHealth", 65);
    AlmostFullHealth = sConfigMgr->GetIntDefault("Playerbots.AlmostFullHealth", 85);
    LowMana = sConfigMgr->GetIntDefault("Playerbots.LowMana", 15);
    MediumMana = sConfigMgr->GetIntDefault("Playerbots.MediumMana", 40);
    HighMana = sConfigMgr->GetIntDefault("Playerbots.HighMana", 65);
    AutoSaveMana = sConfigMgr->GetBoolDefault("Playerbots.AutoSaveMana", true);
    SaveManaThreshold = sConfigMgr->GetIntDefault("Playerbots.SaveManaThreshold", 60);
    switch (uint32(sConfigMgr->GetIntDefault("Playerbots.AutoGreaterBlessings", 2)))
    {
        case 0:
            AutoGreaterBlessings = AutoPartyBuffMode::DISABLED;
            break;
        case 2:
            AutoGreaterBlessings = AutoPartyBuffMode::GROUP_OR_RAID;
            break;
        case 1:
        default:
            AutoGreaterBlessings = AutoPartyBuffMode::RAID_ONLY;
            break;
    }
    BlessingRefreshThresholdSeconds =
        uint32(sConfigMgr->GetIntDefault("Playerbots.BlessingRefreshThresholdSeconds", 60));
    switch (uint32(sConfigMgr->GetIntDefault("Playerbots.AutoPartyBuffs", 2)))
    {
        case 0:
            AutoPartyBuffs = AutoPartyBuffMode::DISABLED;
            break;
        case 1:
            AutoPartyBuffs = AutoPartyBuffMode::RAID_ONLY;
            break;
        case 2:
        default:
            AutoPartyBuffs = AutoPartyBuffMode::GROUP_OR_RAID;
            break;
    }
    TellWhenMissingBuffReagents = sConfigMgr->GetBoolDefault("Playerbots.TellWhenMissingBuffReagents", true);
    MissingBuffReagentMessageCooldown = uint32(sConfigMgr->GetIntDefault(
        "Playerbots.MissingBuffReagentMessageCooldown", 300));
    ForceRebuffOnReadyCheck = sConfigMgr->GetBoolDefault("Playerbots.ForceRebuffOnReadyCheck", false);
    ForceRebuffMarginSecs = std::min(uint32(sConfigMgr->GetIntDefault("Playerbots.ForceRebuffMarginSecs", 60)), 3600u);
    AutoAvoidAoe = sConfigMgr->GetBoolDefault("Playerbots.AutoAvoidAoe", true);
    MaxAoeAvoidRadius = sConfigMgr->GetFloatDefault("Playerbots.MaxAoeAvoidRadius", 15.0f);
    LoadSet<std::set<uint32>>(sConfigMgr->GetStringDefault("Playerbots.AoeAvoidSpellWhitelist", "50759,57491,13810,29946"),
                              AoeAvoidSpellWhitelist);
    TellWhenAvoidAoe = sConfigMgr->GetBoolDefault("Playerbots.TellWhenAvoidAoe", false);

    RandomGearLoweringChance = sConfigMgr->GetFloatDefault("Playerbots.RandomGearLoweringChance", 0.0f);
    RandomGearQualityLimit = sConfigMgr->GetIntDefault("Playerbots.RandomGearQualityLimit", 3);
    RandomGearScoreLimit = sConfigMgr->GetIntDefault("Playerbots.RandomGearScoreLimit", 0);
    PreferClassArmorType  = sConfigMgr->GetBoolDefault("Playerbots.PreferClassArmorType", false);
    PreferredSpecWeapons  = sConfigMgr->GetBoolDefault("Playerbots.PreferredSpecWeapons", false);

    RandomBotMinLevelChance = sConfigMgr->GetFloatDefault("Playerbots.RandomBotMinLevelChance", 0.1f);
    RandomBotMaxLevelChance = sConfigMgr->GetFloatDefault("Playerbots.RandomBotMaxLevelChance", 0.1f);
    RandomBotRpgChance = sConfigMgr->GetFloatDefault("Playerbots.RandomBotRpgChance", 0.20f);

    IterationsPerTick = sConfigMgr->GetIntDefault("Playerbots.IterationsPerTick", 10);

    AllowAccountBots = sConfigMgr->GetBoolDefault("Playerbots.AllowAccountBots", true);
    AllowGuildBots = sConfigMgr->GetBoolDefault("Playerbots.AllowGuildBots", true);
    AllowTrustedAccountBots = sConfigMgr->GetBoolDefault("Playerbots.AllowTrustedAccountBots", true);
    DisabledWithoutRealPlayer = sConfigMgr->GetBoolDefault("Playerbots.DisabledWithoutRealPlayer", false);
    RandomBotGuildNearby = sConfigMgr->GetBoolDefault("Playerbots.RandomBotGuildNearby", false);
    RandomBotInvitePlayer = sConfigMgr->GetBoolDefault("Playerbots.RandomBotInvitePlayer", false);
    InviteChat = sConfigMgr->GetBoolDefault("Playerbots.InviteChat", false);

    RandomBotMapsAsString = sConfigMgr->GetStringDefault("Playerbots.RandomBotMaps", "0,1,530,571");
    LoadList<std::vector<uint32>>(RandomBotMapsAsString, RandomBotMaps);
    ProbTeleToBankers = sConfigMgr->GetFloatDefault("Playerbots.ProbTeleToBankers", 0.25f);
    ProbTeleToQuestGivers = sConfigMgr->GetFloatDefault("Playerbots.ProbTeleToQuestGivers", 0.0f);

    EnableWeightTeleToCityBankers = sConfigMgr->GetBoolDefault("Playerbots.EnableWeightTeleToCityBankers", false);
    WeightTeleToStormwind = sConfigMgr->GetIntDefault("Playerbots.TeleToStormwindWeight", 2);
    WeightTeleToIronforge = sConfigMgr->GetIntDefault("Playerbots.TeleToIronforgeWeight", 1);
    WeightTeleToDarnassus = sConfigMgr->GetIntDefault("Playerbots.TeleToDarnassusWeight", 1);
    WeightTeleToExodar = sConfigMgr->GetIntDefault("Playerbots.TeleToExodarWeight", 1);
    WeightTeleToOrgrimmar = sConfigMgr->GetIntDefault("Playerbots.TeleToOrgrimmarWeight", 2);
    WeightTeleToUndercity = sConfigMgr->GetIntDefault("Playerbots.TeleToUndercityWeight", 1);
    WeightTeleToThunderBluff = sConfigMgr->GetIntDefault("Playerbots.TeleToThunderBluffWeight", 1);
    WeightTeleToSilvermoonCity = sConfigMgr->GetIntDefault("Playerbots.TeleToSilvermoonCityWeight", 1);
    WeightTeleToShattrathCity = sConfigMgr->GetIntDefault("Playerbots.TeleToShattrathCityWeight", 1);
    WeightTeleToDalaran = sConfigMgr->GetIntDefault("Playerbots.TeleToDalaranWeight", 1);
    LoadList<std::vector<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.RandomBotQuestItems",
                                           "5175,5176,5177,5178,6948,11000,12382,13704,16309"),
        RandomBotQuestItems);
    LoadList<std::vector<uint32>>(sConfigMgr->GetStringDefault("Playerbots.RandomBotSpellIds", ""),
                                  RandomBotSpellIds);
    LoadList<std::vector<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.PvpProhibitedZoneIds",
                                           "2255,656,2361,2362,2363,976,35,2268,3425,392,541,1446,3828,3712,3738,3565,"
                                           "3539,3623,4152,3988,4658,4284,4418,4436,4275,4323,4395,3703,4298,3951"),
        PvpProhibitedZoneIds);
    LoadList<std::vector<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.PvpProhibitedAreaIds",
                                           "976,35,392,2268,4161,4010,4317,4312,3649,3887,3958,3724,4080,3938,3754,3786,"
                                           "3973,4085,4086,4087,4088,251"),
        PvpProhibitedAreaIds);
    FastReactInBG = sConfigMgr->GetBoolDefault("Playerbots.FastReactInBG", true);
    LoadList<std::vector<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.RandomBotQuestIds", "3802,5505,6502,7761,7848,10277,10285,11492,"
                                           "13188,13189,24499,24511,24710,24712"),
        RandomBotQuestIds);

    LoadSet<std::set<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.DisallowedGameObjects",
                                            "2560,2656,2857,3658,3705,3706,3719,17155,19020,74448,75293,105579,123329,"
                                            "128308,128403,141596,160836,160845,165738,165739,175245,175970,176112,"
                                            "176213,176224,176325,176327,179490,179516,180523,181085"),
        DisallowedGameObjects);
    LoadSet<std::set<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.AttunementQuests", "10279,10277,10282,10283,10284,10285,10296,"
                                           "10297,10298,11481,11482,11488,11490,11492,10901,10888,10445,10985"),
        AttunementQuests);

    LoadSet<std::set<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.UnobtainableItems", "12468,44869,44870,46978"),
        UnobtainableItems);

    BotAutologin = sConfigMgr->GetBoolDefault("Playerbots.BotAutologin", false);
    RandomBotAutologin = sConfigMgr->GetBoolDefault("Playerbots.RandomBotAutologin", true);
    MinRandomBots = sConfigMgr->GetIntDefault("Playerbots.MinRandomBots", 500);
    MaxRandomBots = sConfigMgr->GetIntDefault("Playerbots.MaxRandomBots", 500);
    // An inverted range aborts the worldserver on the first population tick, since urand asserts on it.
    if (MinRandomBots > MaxRandomBots)
    {
        TC_LOG_ERROR("server.loading",
                  "Playerbots.MinRandomBots ({}) is higher than Playerbots.MaxRandomBots ({}). Swapping them.",
                  MinRandomBots, MaxRandomBots);
        std::swap(MinRandomBots, MaxRandomBots);
    }
    RandomBotCountMode = uint32(sConfigMgr->GetIntDefault("Playerbots.RandomBotCountMode", 0));
    if (RandomBotCountMode > 2)
    {
        TC_LOG_ERROR("server.loading", "Playerbots.RandomBotCountMode ({}) is not 0, 1 or 2. Using 0 (static).",
                  RandomBotCountMode);
        RandomBotCountMode = 0;
    }
    RandomBotUpdateInterval = sConfigMgr->GetIntDefault("Playerbots.RandomBotUpdateInterval", 20);
    RandomBotCountChangeMinInterval =
        sConfigMgr->GetIntDefault("Playerbots.RandomBotCountChangeMinInterval", 30 * MINUTE);
    RandomBotCountChangeMaxInterval =
        sConfigMgr->GetIntDefault("Playerbots.RandomBotCountChangeMaxInterval", 2 * HOUR);
    RandomBotCountMinTime =
        std::min<uint32>(uint32(sConfigMgr->GetIntDefault("Playerbots.RandomBotCountMinTime", 5)), 23);
    RandomBotCountMaxTime =
        std::min<uint32>(uint32(sConfigMgr->GetIntDefault("Playerbots.RandomBotCountMaxTime", 20)), 23);
    MinRandomBotRandomizeTime = sConfigMgr->GetIntDefault("Playerbots.MinRandomBotRandomizeTime", 2 * HOUR);
    MaxRandomBotRandomizeTime = sConfigMgr->GetIntDefault("Playerbots.MaxRandomBotRandomizeTime", 14 * 24 * HOUR);
    MinRandomBotChangeStrategyTime =
        sConfigMgr->GetIntDefault("Playerbots.MinRandomBotChangeStrategyTime", 30 * MINUTE);
    MaxRandomBotChangeStrategyTime =
        sConfigMgr->GetIntDefault("Playerbots.MaxRandomBotChangeStrategyTime", 2 * HOUR);
    MinRandomBotReviveTime = sConfigMgr->GetIntDefault("Playerbots.MinRandomBotReviveTime", MINUTE);
    MaxRandomBotReviveTime = sConfigMgr->GetIntDefault("Playerbots.MaxRandomBotReviveTime", 5 * MINUTE);
    MinRandomBotTeleportInterval = sConfigMgr->GetIntDefault("Playerbots.MinRandomBotTeleportInterval", 1 * HOUR);
    MaxRandomBotTeleportInterval = sConfigMgr->GetIntDefault("Playerbots.MaxRandomBotTeleportInterval", 5 * HOUR);
    RandomBotTeleportDistance = sConfigMgr->GetIntDefault("Playerbots.RandomBotTeleportDistance", 100);
    RandomBotsPerInterval = sConfigMgr->GetIntDefault("Playerbots.RandomBotsPerInterval", 60);
    RandomBotPrintStatsInterval = sConfigMgr->GetIntDefault("Playerbots.RandomBotPrintStatsInterval", 300);
    MinRandomBotsPriceChangeInterval =
        sConfigMgr->GetIntDefault("Playerbots.MinRandomBotsPriceChangeInterval", 2 * HOUR);
    MaxRandomBotsPriceChangeInterval =
        sConfigMgr->GetIntDefault("Playerbots.MaxRandomBotsPriceChangeInterval", 48 * HOUR);
    RandomBotJoinLfg = sConfigMgr->GetBoolDefault("Playerbots.RandomBotJoinLfg", true);
    RandomBotJoinLfgOutleveledExpansion =
        sConfigMgr->GetBoolDefault("Playerbots.RandomBotJoinLfgOutleveledExpansion", true);

    RestrictHealerDPS = sConfigMgr->GetBoolDefault("Playerbots.HealerDPSMapRestriction", false);
    LoadList<std::vector<uint32>>(
        sConfigMgr->GetStringDefault("Playerbots.RestrictedHealerDPSMaps",
                                             "33,34,36,43,47,48,70,90,109,129,209,229,230,329,349,389,429,1001,1004,"
                                             "1007,269,540,542,543,545,546,547,552,553,554,555,556,557,558,560,585,574,"
                                             "575,576,578,595,599,600,601,602,604,608,619,632,650,658,668,409,469,509,"
                                             "531,532,534,544,548,550,564,565,580,249,533,603,615,616,624,631,649,724"),
        RestrictedHealerDPSMaps);

    //////////////////////////// ICC

    EnableICCBuffs = sConfigMgr->GetBoolDefault("Playerbots.EnableICCBuffs", true);

    //////////////////////////// Professions
    ClassMatchingProfessionChance =
        std::min<uint32>(100, uint32(sConfigMgr->GetIntDefault("Playerbots.ClassMatchingProfessionChance", 30)));
    FishingDistanceFromMaster = sConfigMgr->GetFloatDefault("Playerbots.FishingDistanceFromMaster", 10.0f);
    EndFishingWithMaster = sConfigMgr->GetFloatDefault("Playerbots.EndFishingWithMaster", 30.0f);
    FishingDistance = sConfigMgr->GetFloatDefault("Playerbots.FishingDistance", 40.0f);
    EnableFishingWithMaster = sConfigMgr->GetBoolDefault("Playerbots.EnableFishingWithMaster", true);
    //////////////////////////// CHAT
    EnableBroadcasts = sConfigMgr->GetBoolDefault("Playerbots.EnableBroadcasts", true);
    RandomBotTalk = sConfigMgr->GetBoolDefault("Playerbots.RandomBotTalk", false);
    RandomBotEmote = sConfigMgr->GetBoolDefault("Playerbots.RandomBotEmote", false);
    RandomBotSuggestDungeons = sConfigMgr->GetBoolDefault("Playerbots.RandomBotSuggestDungeons", true);
    RandomBotSayWithoutMaster = sConfigMgr->GetBoolDefault("Playerbots.RandomBotSayWithoutMaster", false);
    AnnounceConsumableUse = sConfigMgr->GetBoolDefault("Playerbots.AnnounceConsumableUse", true);

    // broadcastChanceMaxValue is used in urand(1, broadcastChanceMaxValue) for broadcasts,
    // lowering it will increase the chance, setting it to 0 will disable broadcasts
    // for internal use, not intended to be change by the user
    BroadcastChanceMaxValue = EnableBroadcasts ? 30000 : 0;

    // all broadcast chances should be in range 1-broadcastChanceMaxValue, value of 0 will disable this particular
    // broadcast setting value to max does not guarantee the broadcast, as there are some internal randoms as well
    BroadcastToGuildGlobalChance = sConfigMgr->GetIntDefault("Playerbots.BroadcastToGuildGlobalChance", 30000);
    BroadcastToWorldGlobalChance = sConfigMgr->GetIntDefault("Playerbots.BroadcastToWorldGlobalChance", 30000);
    BroadcastToGeneralGlobalChance = sConfigMgr->GetIntDefault("Playerbots.BroadcastToGeneralGlobalChance", 30000);
    BroadcastToTradeGlobalChance = sConfigMgr->GetIntDefault("Playerbots.BroadcastToTradeGlobalChance", 30000);
    BroadcastToLFGGlobalChance = sConfigMgr->GetIntDefault("Playerbots.BroadcastToLFGGlobalChance", 30000);
    BroadcastToLocalDefenseGlobalChance =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastToLocalDefenseGlobalChance", 30000);
    BroadcastToWorldDefenseGlobalChance =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastToWorldDefenseGlobalChance", 30000);
    BroadcastToGuildRecruitmentGlobalChance =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastToGuildRecruitmentGlobalChance", 30000);

    BroadcastChanceLootingItemPoor = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemPoor", 30);
    BroadcastChanceLootingItemNormal =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemNormal", 300);
    BroadcastChanceLootingItemUncommon =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemUncommon", 10000);
    BroadcastChanceLootingItemRare = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemRare", 20000);
    BroadcastChanceLootingItemEpic = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemEpic", 30000);
    BroadcastChanceLootingItemLegendary =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemLegendary", 30000);
    BroadcastChanceLootingItemArtifact =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLootingItemArtifact", 30000);

    BroadcastChanceQuestAccepted = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestAccepted", 6000);
    BroadcastChanceQuestUpdateObjectiveCompleted =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestUpdateObjectiveCompleted", 300);
    BroadcastChanceQuestUpdateObjectiveProgress =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestUpdateObjectiveProgress", 300);
    BroadcastChanceQuestUpdateFailedTimer =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestUpdateFailedTimer", 300);
    BroadcastChanceQuestUpdateComplete =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestUpdateComplete", 1000);
    BroadcastChanceQuestTurnedIn = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceQuestTurnedIn", 10000);

    BroadcastChanceKillNormal = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillNormal", 30);
    BroadcastChanceKillElite = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillElite", 300);
    BroadcastChanceKillRareelite = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillRareelite", 3000);
    BroadcastChanceKillWorldboss = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillWorldboss", 20000);
    BroadcastChanceKillRare = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillRare", 10000);
    BroadcastChanceKillUnknown = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillUnknown", 100);
    BroadcastChanceKillPet = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillPet", 10);
    BroadcastChanceKillPlayer = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceKillPlayer", 30);

    BroadcastChanceLevelupGeneric = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLevelupGeneric", 20000);
    BroadcastChanceLevelupTenX = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLevelupTenX", 30000);
    BroadcastChanceLevelupMaxLevel = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceLevelupMaxLevel", 30000);

    BroadcastChanceSuggestInstance = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestInstance", 5000);
    BroadcastChanceSuggestQuest = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestQuest", 10000);
    BroadcastChanceSuggestGrindMaterials =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestGrindMaterials", 5000);
    BroadcastChanceSuggestGrindReputation =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestGrindReputation", 5000);
    BroadcastChanceSuggestSell = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestSell", 300);
    BroadcastChanceSuggestSomething =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestSomething", 30000);

    BroadcastChanceSuggestSomethingToxic =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestSomethingToxic", 0);

    BroadcastChanceSuggestToxicLinks = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestToxicLinks", 0);
    ToxicLinksPrefix = sConfigMgr->GetStringDefault("Playerbots.ToxicLinksPrefix", "gnomes");

    BroadcastChanceSuggestThunderfury =
        sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceSuggestThunderfury", 1);

    // does not depend on global chance
    BroadcastChanceGuildManagement = sConfigMgr->GetIntDefault("Playerbots.BroadcastChanceGuildManagement", 30000);

    ToxicLinksRepliesChance = sConfigMgr->GetIntDefault("Playerbots.ToxicLinksRepliesChance", 30);    // 0-100
    ThunderfuryRepliesChance = sConfigMgr->GetIntDefault("Playerbots.ThunderfuryRepliesChance", 40);  // 0-100
    GuildRepliesRate = sConfigMgr->GetIntDefault("Playerbots.GuildRepliesRate", 100);                 // 0-100

    RandomBotJoinBG = sConfigMgr->GetBoolDefault("Playerbots.RandomBotJoinBG", true);
    RandomBotAutoJoinBG = sConfigMgr->GetBoolDefault("Playerbots.RandomBotAutoJoinBG", false);

    RandomBotAutoJoinArenaBracket = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinArenaBracket", 14);

    RandomBotAutoJoinWSBrackets = sConfigMgr->GetStringDefault("Playerbots.RandomBotAutoJoinWSBrackets", "7");
    RandomBotAutoJoinABBrackets = sConfigMgr->GetStringDefault("Playerbots.RandomBotAutoJoinABBrackets", "6");
    RandomBotAutoJoinAVBrackets = sConfigMgr->GetStringDefault("Playerbots.RandomBotAutoJoinAVBrackets", "3");
    RandomBotAutoJoinEYBrackets = sConfigMgr->GetStringDefault("Playerbots.RandomBotAutoJoinEYBrackets", "2");
    RandomBotAutoJoinICBrackets = sConfigMgr->GetStringDefault("Playerbots.RandomBotAutoJoinICBrackets", "1");

    RandomBotAutoJoinBGWSCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGWSCount", 1);
    RandomBotAutoJoinBGABCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGABCount", 1);
    RandomBotAutoJoinBGAVCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGAVCount", 0);
    RandomBotAutoJoinBGEYCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGEYCount", 1);
    RandomBotAutoJoinBGICCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGICCount", 0);

    RandomBotAutoJoinBGRatedArena2v2Count =
        sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGRatedArena2v2Count", 0);
    RandomBotAutoJoinBGRatedArena3v3Count =
        sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGRatedArena3v3Count", 0);
    RandomBotAutoJoinBGRatedArena5v5Count =
        sConfigMgr->GetIntDefault("Playerbots.RandomBotAutoJoinBGRatedArena5v5Count", 0);
    LogInGroupOnly = sConfigMgr->GetBoolDefault("Playerbots.LogInGroupOnly", true);
    LogValuesPerTick = sConfigMgr->GetBoolDefault("Playerbots.LogValuesPerTick", false);
    SummonAtInnkeepersEnabled = sConfigMgr->GetBoolDefault("Playerbots.SummonAtInnkeepersEnabled", true);
    RandomBotMinLevel = sConfigMgr->GetIntDefault("Playerbots.RandomBotMinLevel", 1);
    RandomBotMaxLevel = sConfigMgr->GetIntDefault("Playerbots.RandomBotMaxLevel", 80);
    if (RandomBotMaxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        RandomBotMaxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    // Bracket defaults (below) derive from randomBotMaxLevel, so this must run after it is read.
    LoadRandomBotLevelConfig();

    RandomBotTeleLowerLevel = sConfigMgr->GetIntDefault("Playerbots.RandomBotTeleLowerLevel", 1);
    RandomBotTeleHigherLevel = sConfigMgr->GetIntDefault("Playerbots.RandomBotTeleHigherLevel", 3);
    OpenGoSpell = sConfigMgr->GetIntDefault("Playerbots.OpenGoSpell", 6477);

    // Zones for NewRpgStrategy teleportation brackets
    std::vector<uint32> zoneIds = {
        // Classic WoW - Low-level zones
        1, 12, 14, 85, 141, 215, 3430, 3524,
        // Classic WoW - Mid-level zones
        17, 38, 40, 130, 148, 3433, 3525,
        // Classic WoW - High-level zones
        10, 11, 44, 267, 331, 400, 406,
        // Classic WoW - Higher-level zones
        3, 8, 15, 16, 33, 45, 47, 51, 357, 405, 440,
        // Classic WoW - Top-level zones
        4, 28, 46, 139, 361, 490, 618, 1377,
        // The Burning Crusade - Zones
        3483, 3518, 3519, 3520, 3521, 3522, 3523, 4080,
        // Wrath of the Lich King - Zones
        65, 66, 67, 210, 394, 495, 2817, 3537, 3711, 4197
    };

    for (uint32 zoneId : zoneIds)
    {
        std::string setting = "Playerbots.ZoneBracket." + std::to_string(zoneId);
        std::string value = sConfigMgr->GetStringDefault(setting, "");

        if (!value.empty())
        {
            size_t commaPos = value.find(',');
            if (commaPos != std::string::npos)
            {
                uint32 minLevel = atoi(value.substr(0, commaPos).c_str());
                uint32 maxLevel = atoi(value.substr(commaPos + 1).c_str());
                ZoneBrackets[zoneId] = std::make_pair(minLevel, maxLevel);
            }
        }
    }

    RandomChangeMultiplier = sConfigMgr->GetFloatDefault("Playerbots.RandomChangeMultiplier", 1.0);

    RandomBotCombatStrategies = sConfigMgr->GetStringDefault("Playerbots.RandomBotCombatStrategies", "");
    RandomBotNonCombatStrategies = sConfigMgr->GetStringDefault("Playerbots.RandomBotNonCombatStrategies", "");
    CombatStrategies = sConfigMgr->GetStringDefault("Playerbots.CombatStrategies", "");
    NonCombatStrategies = sConfigMgr->GetStringDefault("Playerbots.NonCombatStrategies", "");
    ReactStrategies = sConfigMgr->GetStringDefault("Playerbots.ReactStrategies", "");
    RandomBotReactStrategies = sConfigMgr->GetStringDefault("Playerbots.RandomBotReactStrategies", "");
    ApplyInstanceStrategies = sConfigMgr->GetBoolDefault("Playerbots.ApplyInstanceStrategies", true);

    CommandPrefix = sConfigMgr->GetStringDefault("Playerbots.CommandPrefix", "");
    CommandSeparator = sConfigMgr->GetStringDefault("Playerbots.CommandSeparator", "\\\\");

    CommandServerPort = sConfigMgr->GetIntDefault("Playerbots.CommandServerPort", 8888);
    PerfMonEnabled = sConfigMgr->GetBoolDefault("Playerbots.PerfMonEnabled", false);

    // stagger bot flightpath takeoff
    BotTaxiDelayMin = uint32(sConfigMgr->GetIntDefault("Playerbots.BotTaxiDelayMinMs", 350));
    BotTaxiDelayMax = uint32(sConfigMgr->GetIntDefault("Playerbots.BotTaxiDelayMaxMs", 5000));
    BotTaxiGapMs = uint32(sConfigMgr->GetIntDefault("Playerbots.BotTaxiGapMs", 200));
    BotTaxiGapJitterMs = uint32(sConfigMgr->GetIntDefault("Playerbots.BotTaxiGapJitterMs", 100));

    BotCheats.clear();
    LoadListString<std::vector<std::string>>(sConfigMgr->GetStringDefault("Playerbots.BotCheats", "food,taxi,raid"),
                                             BotCheats);

    BotCheatMask = 0;

    if (std::find(BotCheats.begin(), BotCheats.end(), "food") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::food;
    if (std::find(BotCheats.begin(), BotCheats.end(), "taxi") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::taxi;
    if (std::find(BotCheats.begin(), BotCheats.end(), "gold") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::gold;
    if (std::find(BotCheats.begin(), BotCheats.end(), "health") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::health;
    if (std::find(BotCheats.begin(), BotCheats.end(), "mana") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::mana;
    if (std::find(BotCheats.begin(), BotCheats.end(), "power") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::power;
    if (std::find(BotCheats.begin(), BotCheats.end(), "raid") != BotCheats.end())
        BotCheatMask |= (uint32)BotCheatMask::raid;

    LoadListString<std::vector<std::string>>(sConfigMgr->GetStringDefault("Playerbots.AllowedLogFiles", ""),
                                             AllowedLogFiles);
    EnableAutoTradeOnItemMention = sConfigMgr->GetBoolDefault("Playerbots.EnableAutoTradeOnItemMention", true);
    LoadListString<std::vector<std::string>>(sConfigMgr->GetStringDefault("Playerbots.TradeActionExcludedPrefixes", ""),
                                             TradeActionExcludedPrefixes);

    WorldBuffs.clear();
    LoadWorldBuff();
    TC_LOG_INFO("playerbots", "Loading World Buff Feature...");

    RandomBotAccountPrefix = sConfigMgr->GetStringDefault("Playerbots.RandomBotAccountPrefix", "rndbot");
    RandomBotAccountCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotAccountCount", 0);
    DeleteRandomBotAccounts = sConfigMgr->GetBoolDefault("Playerbots.DeleteRandomBotAccounts", false);
    RandomBotGuildCount = sConfigMgr->GetIntDefault("Playerbots.RandomBotGuildCount", 20);
    RandomBotGuildSizeMax = sConfigMgr->GetIntDefault("Playerbots.RandomBotGuildSizeMax", 15);
    DeleteRandomBotGuilds = sConfigMgr->GetBoolDefault("Playerbots.DeleteRandomBotGuilds", false);

    BotSendMailEnabled = sConfigMgr->GetBoolDefault("Playerbots.BotSendMailEnabled", true);

    GuildTaskEnabled = sConfigMgr->GetBoolDefault("Playerbots.EnableGuildTasks", false);
    MinGuildTaskChangeTime = sConfigMgr->GetIntDefault("Playerbots.MinGuildTaskChangeTime", 3 * 24 * 3600);
    MaxGuildTaskChangeTime = sConfigMgr->GetIntDefault("Playerbots.MaxGuildTaskChangeTime", 4 * 24 * 3600);
    MinGuildTaskAdvertisementTime = sConfigMgr->GetIntDefault("Playerbots.MinGuildTaskAdvertisementTime", 300);
    MaxGuildTaskAdvertisementTime = sConfigMgr->GetIntDefault("Playerbots.MaxGuildTaskAdvertisementTime", 12 * 3600);
    MinGuildTaskRewardTime = sConfigMgr->GetIntDefault("Playerbots.MinGuildTaskRewardTime", 300);
    MaxGuildTaskRewardTime = sConfigMgr->GetIntDefault("Playerbots.MaxGuildTaskRewardTime", 3600);
    GuildTaskAdvertCleanupTime = sConfigMgr->GetIntDefault("Playerbots.GuildTaskAdvertCleanupTime", 300);
    GuildTaskKillTaskDistance = sConfigMgr->GetIntDefault("Playerbots.GuildTaskKillTaskDistance", 2000);
    TargetPosRecalcDistance = sConfigMgr->GetFloatDefault("Playerbots.TargetPosRecalcDistance", 0.1f);

    //cosmetics
    switch (sConfigMgr->GetIntDefault("Playerbots.RandomBotShowHelmet", 1))
    {
        case 0:
            RandomBotShowHelmet = ShowHideCosmetic::ALWAYS_HIDE;
            break;
        case 2:
            RandomBotShowHelmet = ShowHideCosmetic::RANDOMIZE;
            break;
        case 1:
        default:
            RandomBotShowHelmet = ShowHideCosmetic::ALWAYS_SHOW;
            break;
    }
    switch (sConfigMgr->GetIntDefault("Playerbots.RandomBotShowCloak", 1))
    {
        case 0:
            RandomBotShowCloak = ShowHideCosmetic::ALWAYS_HIDE;
            break;
        case 2:
            RandomBotShowCloak = ShowHideCosmetic::RANDOMIZE;
            break;
        case 1:
        default:
            RandomBotShowCloak = ShowHideCosmetic::ALWAYS_SHOW;
            break;
    }

    // SPP switches
    EnableGreet = sConfigMgr->GetBoolDefault("Playerbots.EnableGreet", true);
    SummonWhenGroup = sConfigMgr->GetBoolDefault("Playerbots.SummonWhenGroup", true);
    RandomBotFixedLevel = sConfigMgr->GetBoolDefault("Playerbots.RandomBotFixedLevel", false);
    DisableRandomLevels = sConfigMgr->GetBoolDefault("Playerbots.DisableRandomLevels", false);
    RandomBotRandomPassword = sConfigMgr->GetBoolDefault("Playerbots.RandomBotRandomPassword", true);
    DowngradeMaxLevelBot = sConfigMgr->GetBoolDefault("Playerbots.DowngradeMaxLevelBot", true);
    EquipAndSpecPersistence = sConfigMgr->GetBoolDefault("Playerbots.EquipAndSpecPersistence", true);
    EquipAndSpecPersistenceLevel = sConfigMgr->GetIntDefault("Playerbots.EquipAndSpecPersistenceLevel", 1);
    GroupInvitationPermission = sConfigMgr->GetIntDefault("Playerbots.GroupInvitationPermission", 1);
    AllowBattlegroundCommands = sConfigMgr->GetBoolDefault("Playerbots.AllowBattlegroundCommands", false);
    KeepAltsInGroup = sConfigMgr->GetBoolDefault("Playerbots.KeepAltsInGroup", false);
    AllowSummonInCombat = sConfigMgr->GetBoolDefault("Playerbots.AllowSummonInCombat", true);
    AllowSummonWhenMasterIsDead = sConfigMgr->GetBoolDefault("Playerbots.AllowSummonWhenMasterIsDead", true);
    AllowSummonWhenBotIsDead = sConfigMgr->GetBoolDefault("Playerbots.AllowSummonWhenBotIsDead", true);
    ReviveBotWhenSummoned = sConfigMgr->GetIntDefault("Playerbots.ReviveBotWhenSummoned", 1);
    BotRepairWhenSummon = sConfigMgr->GetBoolDefault("Playerbots.BotRepairWhenSummon", true);
    AutoInitOnly = sConfigMgr->GetBoolDefault("Playerbots.AutoInitOnly", false);
    ResetInstanceIdForAltBots = sConfigMgr->GetBoolDefault("Playerbots.ResetInstanceIdForAltBots", false);
    AutoInitEquipLevelLimitRatio = sConfigMgr->GetFloatDefault("Playerbots.AutoInitEquipLevelLimitRatio", 1.0);

    MaxAddedBots = sConfigMgr->GetIntDefault("Playerbots.MaxAddedBots", 40);
    AddClassCommand = sConfigMgr->GetIntDefault("Playerbots.AddClassCommand", 1);
    AddClassAccountPoolSize = sConfigMgr->GetIntDefault("Playerbots.AddClassAccountPoolSize", 50);
    AddClassRandomCharacter = sConfigMgr->GetBoolDefault("Playerbots.AddClassRandomCharacter", false);
    MaintenanceCommand = sConfigMgr->GetIntDefault("Playerbots.MaintenanceCommand", 1);

    AltMaintenanceAttunementQs = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceAttunementQuests", true);
    AltMaintenanceBags = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceBags", true);
    AltMaintenanceAmmo = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceAmmo", true);
    AltMaintenanceFood = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceFood", true);
    AltMaintenanceReagents = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceReagents", true);
    AltMaintenanceConsumables = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceConsumables", true);
    AltMaintenancePotions = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenancePotions", true);
    AltMaintenanceTalentTree = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceTalentTree", true);
    AltMaintenancePet = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenancePet", true);
    AltMaintenancePetTalents = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenancePetTalents", true);
    AltMaintenanceClassSpells = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceClassSpells", true);
    AltMaintenanceAvailableSpells = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceAvailableSpells", true);
    AltMaintenanceSkills = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceSkills", true);
    AltMaintenanceReputation = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceReputation", true);
    AltMaintenanceSpecialSpells = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceSpecialSpells", true);
    AltMaintenanceMounts = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceMounts", true);
    AltMaintenanceGlyphs = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceGlyphs", true);
    AltMaintenanceKeyring = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceKeyring", true);
    AltMaintenanceGemsEnchants = sConfigMgr->GetBoolDefault("Playerbots.AltMaintenanceGemsEnchants", true);

    AutoGearCommand = sConfigMgr->GetIntDefault("Playerbots.AutoGearCommand", 1);
    AutoGearCommandAltBots = sConfigMgr->GetIntDefault("Playerbots.AutoGearCommandAltBots", 1);
    AutoGearBisCommand = sConfigMgr->GetIntDefault("Playerbots.AutoGearBisCommand", 0);
    AutoGearQualityLimit = sConfigMgr->GetIntDefault("Playerbots.AutoGearQualityLimit", 3);
    AutoGearScoreLimit = sConfigMgr->GetIntDefault("Playerbots.AutoGearScoreLimit", 0);

    RandomBotXPRate = sConfigMgr->GetFloatDefault("Playerbots.RandomBotXPRate", 1.0);
    RandomBotAllianceRatio = sConfigMgr->GetIntDefault("Playerbots.RandomBotAllianceRatio", 50);
    RandomBotHordeRatio = sConfigMgr->GetIntDefault("Playerbots.RandomBotHordeRatio", 50);
    if (!RandomBotAllianceRatio && !RandomBotHordeRatio)    // AddRandomBots() divides by 0 if both ratio = 0
    {
        TC_LOG_ERROR("server.loading",
                  "Playerbots.RandomBotAllianceRatio and Playerbots.RandomBotHordeRatio are both 0. Using 50/50.");
        RandomBotAllianceRatio = 50;
        RandomBotHordeRatio = 50;
    }
    DisableDeathKnightLogin = sConfigMgr->GetBoolDefault("Playerbots.DisableDeathKnightLogin", 0);
    BotActiveAlone = sConfigMgr->GetIntDefault("Playerbots.BotActiveAlone", 10);
    BotActiveAloneDurationSeconds = sConfigMgr->GetIntDefault("Playerbots.BotActiveAloneDurationSeconds", 30);
    BotActiveAloneForceWhenInRadius = uint32(sConfigMgr->GetIntDefault("Playerbots.BotActiveAloneForceWhenInRadius", 150));
    BotActiveAloneForceWhenInZone = sConfigMgr->GetBoolDefault("Playerbots.BotActiveAloneForceWhenInZone", 1);
    BotActiveAloneForceWhenInMap = sConfigMgr->GetBoolDefault("Playerbots.BotActiveAloneForceWhenInMap", 0);
    BotActiveAloneForceWhenIsFriend = sConfigMgr->GetBoolDefault("Playerbots.BotActiveAloneForceWhenIsFriend", 0);
    BotActiveAloneForceWhenInGuild = sConfigMgr->GetBoolDefault("Playerbots.BotActiveAloneForceWhenInGuild", 1);
    BotActiveAloneSmartScale = sConfigMgr->GetBoolDefault("Playerbots.botActiveAloneSmartScale", 1);
    BotActiveAloneSmartScaleDiffLimitFloor = uint32(sConfigMgr->GetIntDefault("Playerbots.botActiveAloneSmartScaleDiffLimitfloor", 50));
    BotActiveAloneSmartScaleDiffLimitCeiling = uint32(sConfigMgr->GetIntDefault("Playerbots.botActiveAloneSmartScaleDiffLimitCeiling", 200));
    BotActiveAloneSmartScaleWhenMinLevel = uint32(sConfigMgr->GetIntDefault("Playerbots.botActiveAloneSmartScaleWhenMinLevel", 1));
    BotActiveAloneSmartScaleWhenMaxLevel = uint32(sConfigMgr->GetIntDefault("Playerbots.botActiveAloneSmartScaleWhenMaxLevel", 80));

    RandomBotsWalkingRPG = sConfigMgr->GetBoolDefault("Playerbots.RandombotsWalkingRPG", false);
    RandomBotsWalkingRPGInDoors = sConfigMgr->GetBoolDefault("Playerbots.RandombotsWalkingRPG.InDoors", false);
    MinEnchantingBotLevel = sConfigMgr->GetIntDefault("Playerbots.MinEnchantingBotLevel", 60);
    LimitEnchantExpansion = sConfigMgr->GetIntDefault("Playerbots.LimitEnchantExpansion", 1);
    LimitGearExpansion = sConfigMgr->GetIntDefault("Playerbots.LimitGearExpansion", 1);
    RandomBotStartingLevel = sConfigMgr->GetIntDefault("Playerbots.RandombotStartingLevel", 1);
    EnableRandomBotTrading = sConfigMgr->GetIntDefault("Playerbots.EnableRandomBotTrading", 1);
    GearScoreCheck = sConfigMgr->GetBoolDefault("Playerbots.GearScoreCheck", false);
    RandomBotPreQuests = sConfigMgr->GetBoolDefault("Playerbots.PreQuests", false);

    // SPP automation
    FreeMethodLoot = sConfigMgr->GetBoolDefault("Playerbots.FreeMethodLoot", false);
    LootNeedRollLevel = sConfigMgr->GetIntDefault("Playerbots.LootNeedRollLevel", 1);
    LootRollRecipe = sConfigMgr->GetBoolDefault("Playerbots.LootRollRecipe", false);
    LootRollDisenchant = sConfigMgr->GetBoolDefault("Playerbots.LootRollDisenchant", false);
    LootGreedRollLevel = sConfigMgr->GetBoolDefault("Playerbots.LootGreedRollLevel", false);
    AutoPickReward = sConfigMgr->GetStringDefault("Playerbots.AutoPickReward", "yes");
    AutoEquipUpgradeLoot = sConfigMgr->GetBoolDefault("Playerbots.AutoEquipUpgradeLoot", true);
    EquipUpgradeThreshold = sConfigMgr->GetFloatDefault("Playerbots.EquipUpgradeThreshold", 1.1f);
    TwoRoundsGearInit = sConfigMgr->GetBoolDefault("Playerbots.TwoRoundsGearInit", false);
    SyncQuestWithPlayer = sConfigMgr->GetBoolDefault("Playerbots.SyncQuestWithPlayer", true);
    SyncQuestForPlayer = sConfigMgr->GetBoolDefault("Playerbots.SyncQuestForPlayer", false);
    DropObsoleteQuests = sConfigMgr->GetBoolDefault("Playerbots.DropObsoleteQuests", true);
    AllowLearnTrainerSpells = sConfigMgr->GetBoolDefault("Playerbots.AllowLearnTrainerSpells", true);
    AutoPickTalents = sConfigMgr->GetBoolDefault("Playerbots.AutoPickTalents", true);
    AutoPickTalentsForPlayerControlledBots = sConfigMgr->GetBoolDefault("Playerbots.AutoPickTalentsForPlayerControlledBots", false);
    AutoUpgradeEquip = sConfigMgr->GetBoolDefault("Playerbots.AutoUpgradeEquip", true);
    HunterWolfPet = sConfigMgr->GetIntDefault("Playerbots.HunterWolfPet", 0);
    DefaultPetStance = sConfigMgr->GetIntDefault("Playerbots.DefaultPetStance", 1);
    PetChatCommandDebug = sConfigMgr->GetBoolDefault("Playerbots.PetChatCommandDebug", 0);
    AutoLearnTrainerSpells = sConfigMgr->GetBoolDefault("Playerbots.AutoLearnTrainerSpells", true);
    AutoLearnQuestSpells = sConfigMgr->GetBoolDefault("Playerbots.AutoLearnQuestSpells", true);
    AutoTeleportForLevel = sConfigMgr->GetBoolDefault("Playerbots.AutoTeleportForLevel", false);
    AutoDoQuests = sConfigMgr->GetBoolDefault("Playerbots.AutoDoQuests", true);
    EnableNewRpgStrategy = sConfigMgr->GetBoolDefault("Playerbots.EnableNewRpgStrategy", true);
    RpgTargetSearchDistance = sConfigMgr->GetFloatDefault("Playerbots.RpgTargetSearchDistance", 150.0f);
    CollectDetailedQuestStats = sConfigMgr->GetBoolDefault("Playerbots.CollectDetailedQuestStats", false);

    RpgStatusProbWeight[RPG_WANDER_RANDOM] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.WanderRandom", 15);
    RpgStatusProbWeight[RPG_WANDER_NPC] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.WanderNpc", 20);
    RpgStatusProbWeight[RPG_GO_GRIND] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.GoGrind", 15);
    RpgStatusProbWeight[RPG_GO_CAMP] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.GoCamp", 10);
    RpgStatusProbWeight[RPG_DO_QUEST] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.DoQuest", 60);
    RpgStatusProbWeight[RPG_TRAVEL_FLIGHT] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.TravelFlight", 15);
    RpgStatusProbWeight[RPG_REST] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.Rest", 5);
    RpgStatusProbWeight[RPG_OUTDOOR_PVP] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.OutdoorPvp", 10);
    RpgStatusProbWeight[RPG_DO_GATHER] = sConfigMgr->GetIntDefault("Playerbots.RpgStatusProbWeight.DoGather", 15);

    SyncLevelWithPlayers = sConfigMgr->GetBoolDefault("Playerbots.SyncLevelWithPlayers", false);
    RandomBotConcentrateInPlayerZone =
        sConfigMgr->GetBoolDefault("Playerbots.RandomBotConcentrateInPlayerZone", false);
    RandomBotGroupNearby = sConfigMgr->GetBoolDefault("Playerbots.RandomBotGroupNearby", false);

    // arena
    RandomBotArenaTeam2v2Count = sConfigMgr->GetIntDefault("Playerbots.RandomBotArenaTeam2v2Count", 10);
    RandomBotArenaTeam3v3Count = sConfigMgr->GetIntDefault("Playerbots.RandomBotArenaTeam3v3Count", 10);
    RandomBotArenaTeam5v5Count = sConfigMgr->GetIntDefault("Playerbots.RandomBotArenaTeam5v5Count", 5);
    DeleteRandomBotArenaTeams = sConfigMgr->GetBoolDefault("Playerbots.DeleteRandomBotArenaTeams", false);
    RandomBotArenaTeamMaxRating = sConfigMgr->GetIntDefault("Playerbots.RandomBotArenaTeamMaxRating", 2000);
    RandomBotArenaTeamMinRating = sConfigMgr->GetIntDefault("Playerbots.RandomBotArenaTeamMinRating", 1000);

    SelfBotLevel = sConfigMgr->GetIntDefault("Playerbots.SelfBotLevel", 1);

    RandomPlayerbotFactory::LoadLocalizedNameColumns();
    RandomPlayerbotFactory::CreateRandomBots();
    if (World::IsStopped())
    {
        return true;
    }

    // Assign account types after accounts are created
    sRandomPlayerbotMgr.AssignAccountTypes();

    if (sPlayerbotAIConfig.Enabled)
    {
        sRandomPlayerbotMgr.Init();
    }

    PlayerbotGuildMgr::instance().Init();
    sRandomPlayerbotMgr.InitArenaTeams();
    sRandomItemMgr.Init();
    sRandomItemMgr.InitAfterAhBot();
    sBisListMgr->LoadAll();
    PlayerbotTextMgr::instance().LoadBotTexts();
    PlayerbotTextMgr::instance().LoadBotTextChance();
    PlayerbotFactory::Init();

    AiObjectContext::BuildAllSharedContexts();

    if (sPlayerbotAIConfig.RandomBotSuggestDungeons)
    {
        PlayerbotDungeonRepository::instance().LoadDungeonSuggestions();
    }
    sTravelMgr.Init();

    ExcludedHunterPetFamilies.clear();
    LoadList<std::vector<uint32>>(sConfigMgr->GetStringDefault("Playerbots.ExcludedHunterPetFamilies", ""), ExcludedHunterPetFamilies);

    TC_LOG_INFO("server.loading", "---------------------------------------");
    TC_LOG_INFO("server.loading", "       mod-playerbots initialized      ");
    TC_LOG_INFO("server.loading", "---------------------------------------");

    return true;
}

// Loads Playerbots.LevelBrackets.* and Playerbots.ResetBotLevel.* (see RandomBotLevelMgr). Also
// re-run on ".reload config" via RandomBotLevelWorldScript::OnAfterConfigLoad. Bracket
// bounds/percentages are only the as-configured values here; RandomBotLevelMgr::LoadConfig()
// copies them into its own working state, since dynamic distribution and clamp/rebalance mutate
// percentages at runtime.
void PlayerbotAIConfig::LoadRandomBotLevelConfig()
{
    // ---- Level brackets ----
    LevelBracketsEnabled = sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.Enabled", false);
    LevelBracketsIgnoreGuildWithRealPlayers =
        sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.IgnoreGuildBotsWithRealPlayers", true);
    LevelBracketsIgnoreArenaTeamBots =
        sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.IgnoreArenaTeamBots", true);

    LevelBracketsCheckFrequency = uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.CheckFrequency", 300));
    LevelBracketsFlaggedCheckFrequency =
        uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.CheckFlaggedFrequency", 15));
    LevelBracketsDynamicDistribution =
        sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.Dynamic.UseDynamicDistribution", false);
    LevelBracketsRealPlayerWeight =
        sConfigMgr->GetFloatDefault("Playerbots.LevelBrackets.Dynamic.RealPlayerWeight", 1.0f);
    LevelBracketsSyncFactions = sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.Dynamic.SyncFactions", false);
    LevelBracketsIgnoreFriendListed = sConfigMgr->GetBoolDefault("Playerbots.LevelBrackets.IgnoreFriendListed", true);
    LevelBracketsFlaggedProcessLimit =
        uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.FlaggedProcessLimit", 5));

    ParseLevelMgrExcludeNames(sConfigMgr->GetStringDefault("Playerbots.LevelBrackets.ExcludeNames", ""),
        LevelBracketsExcludeNames);

    LevelBracketsNumRanges =
        static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.NumRanges", 9)));
    LevelBracketsAlliance.resize(LevelBracketsNumRanges);
    LevelBracketsHorde.resize(LevelBracketsNumRanges);

    for (uint8 i = 0; i < LevelBracketsNumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        uint32 defaultLower = (i == 0 ? 1 : i * 10);
        uint32 defaultUpper = (i < LevelBracketsNumRanges - 1 ? i * 10 + 9 : RandomBotMaxLevel);
        LevelBracketsAlliance[i].Lower = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Alliance.Range" + idx + ".Lower", defaultLower)));
        LevelBracketsAlliance[i].Upper = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Alliance.Range" + idx + ".Upper", defaultUpper)));
        LevelBracketsAlliance[i].Pct = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Alliance.Range" + idx + ".Pct", 11)));
    }

    for (uint8 i = 0; i < LevelBracketsNumRanges; ++i)
    {
        std::string idx = std::to_string(i + 1);
        uint32 defaultLower = (i == 0 ? 1 : i * 10);
        uint32 defaultUpper = (i < LevelBracketsNumRanges - 1 ? i * 10 + 9 : RandomBotMaxLevel);
        LevelBracketsHorde[i].Lower = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Horde.Range" + idx + ".Lower", defaultLower)));
        LevelBracketsHorde[i].Upper = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Horde.Range" + idx + ".Upper", defaultUpper)));
        LevelBracketsHorde[i].Pct = static_cast<uint8>(
            uint32(sConfigMgr->GetIntDefault("Playerbots.LevelBrackets.Horde.Range" + idx + ".Pct", 11)));
    }

    // A mismatch forcibly disables SyncFactions and logs an error; it never brings the server down.
    if (LevelBracketsSyncFactions)
    {
        for (uint8 i = 0; i < LevelBracketsNumRanges; ++i)
        {
            if (LevelBracketsAlliance[i].Lower != LevelBracketsHorde[i].Lower ||
                LevelBracketsAlliance[i].Upper != LevelBracketsHorde[i].Upper)
            {
                TC_LOG_ERROR("server.loading",
                    "[RandomBotLevelMgr] Bracket mismatch detected between factions at index {}. Alliance: {}-{}, "
                    "Horde: {}-{}. SyncFactions requires both bracket count and min/max levels to match exactly; "
                    "forcibly disabling SyncFactions for this session. Check your configuration.",
                    i, LevelBracketsAlliance[i].Lower, LevelBracketsAlliance[i].Upper,
                    LevelBracketsHorde[i].Lower, LevelBracketsHorde[i].Upper);
                LevelBracketsSyncFactions = false;
                break;
            }
        }
    }

    // ---- Level reset ----
    ResetBotLevelEnabled = sConfigMgr->GetBoolDefault("Playerbots.ResetBotLevel.Enabled", false);

    ResetBotLevelMaxLevel =
        static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.MaxLevel", 80)));
    if ((ResetBotLevelMaxLevel < 2 || ResetBotLevelMaxLevel > 80) && ResetBotLevelMaxLevel != 0)
    {
        TC_LOG_ERROR("server.loading",
            "[RandomBotLevelMgr] Invalid Playerbots.ResetBotLevel.MaxLevel value: {}. Using default value 80.",
            ResetBotLevelMaxLevel);
        ResetBotLevelMaxLevel = 80;
    }

    ResetBotLevelResetTo =
        static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.ResetToLevel", 1)));
    if (ResetBotLevelResetTo < 1 || (ResetBotLevelMaxLevel > 0 && ResetBotLevelResetTo >= ResetBotLevelMaxLevel))
    {
        TC_LOG_ERROR("server.loading",
            "[RandomBotLevelMgr] Invalid Playerbots.ResetBotLevel.ResetToLevel value: {}. Using default value 1.",
            ResetBotLevelResetTo);
        ResetBotLevelResetTo = 1;
    }

    ResetBotLevelSkipFrom =
        static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.SkipFromLevel", 0)));
    if (ResetBotLevelSkipFrom > 80 || (ResetBotLevelMaxLevel > 0 && ResetBotLevelSkipFrom >= ResetBotLevelMaxLevel))
    {
        TC_LOG_ERROR("server.loading",
            "[RandomBotLevelMgr] Invalid Playerbots.ResetBotLevel.SkipFromLevel value: {}. Using default value 0 "
            "(disabled).",
            ResetBotLevelSkipFrom);
        ResetBotLevelSkipFrom = 0;
    }

    ResetBotLevelSkipTo = static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.SkipToLevel", 1)));
    if (ResetBotLevelSkipTo < 1 || ResetBotLevelSkipTo > 80 ||
        (ResetBotLevelMaxLevel > 0 && ResetBotLevelSkipTo > ResetBotLevelMaxLevel))
    {
        TC_LOG_ERROR("server.loading",
            "[RandomBotLevelMgr] Invalid Playerbots.ResetBotLevel.SkipToLevel value: {}. Using default value 1.",
            ResetBotLevelSkipTo);
        ResetBotLevelSkipTo = 1;
    }

    ResetBotLevelChance =
        static_cast<uint8>(uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.ResetChance", 100)));
    if (ResetBotLevelChance > 100)
    {
        TC_LOG_ERROR("server.loading",
            "[RandomBotLevelMgr] Invalid Playerbots.ResetBotLevel.ResetChance value: {}. Using default value 100.",
            ResetBotLevelChance);
        ResetBotLevelChance = 100;
    }

    ResetBotLevelScaledChance = sConfigMgr->GetBoolDefault("Playerbots.ResetBotLevel.ScaledChance", false);

    ResetBotLevelRestrictTimePlayed =
        sConfigMgr->GetBoolDefault("Playerbots.ResetBotLevel.RestrictTimePlayed", false);
    ResetBotLevelMinTimePlayed = uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.MinTimePlayed", 86400));
    ResetBotLevelPlayedTimeCheckFrequency =
        uint32(sConfigMgr->GetIntDefault("Playerbots.ResetBotLevel.PlayedTimeCheckFrequency", 864));

    ResetBotLevelIgnoreGuildWithRealPlayers =
        sConfigMgr->GetBoolDefault("Playerbots.ResetBotLevel.IgnoreGuildBotsWithRealPlayers", false);

    ParseLevelMgrExcludeNames(sConfigMgr->GetStringDefault("Playerbots.ResetBotLevel.ExcludeNames", ""),
        ResetBotLevelExcludeNames);
}

bool PlayerbotAIConfig::IsInRandomAccountList(uint32 id)
{
    return find(RandomBotAccounts.begin(), RandomBotAccounts.end(), id) != RandomBotAccounts.end();
}

bool PlayerbotAIConfig::IsInRandomQuestItemList(uint32 id)
{
    return find(RandomBotQuestItems.begin(), RandomBotQuestItems.end(), id) != RandomBotQuestItems.end();
}

bool PlayerbotAIConfig::IsPvpProhibited(uint32 zoneId, uint32 areaId)
{
    return IsInPvpProhibitedZone(zoneId) || IsInPvpProhibitedArea(areaId) || IsInPvpProhibitedZone(areaId);
}

bool PlayerbotAIConfig::IsInPvpProhibitedZone(uint32 id)
{
    return find(PvpProhibitedZoneIds.begin(), PvpProhibitedZoneIds.end(), id) != PvpProhibitedZoneIds.end();
}

bool PlayerbotAIConfig::IsInPvpProhibitedArea(uint32 id)
{
    return find(PvpProhibitedAreaIds.begin(), PvpProhibitedAreaIds.end(), id) != PvpProhibitedAreaIds.end();
}

bool PlayerbotAIConfig::IsRestrictedHealerDPSMap(uint32 mapId) const
{
    return RestrictHealerDPS &&
            std::find(RestrictedHealerDPSMaps.begin(), RestrictedHealerDPSMaps.end(), mapId) != RestrictedHealerDPSMaps.end();
}

std::string const PlayerbotAIConfig::GetTimestampStr()
{
    time_t t = time(nullptr);
    tm* aTm = localtime(&t);
    //       YYYY   year
    //       MM     month (2 digits 01-12)
    //       DD     day (2 digits 01-31)
    //       HH     hour (2 digits 00-23)
    //       MM     minutes (2 digits 00-59)
    //       SS     seconds (2 digits 00-59)
    // Sized for the widest output snprintf can produce for these int conversions, so the
    // result is never truncated.
    char buf[128];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d-%02d-%02d", aTm->tm_year + 1900, aTm->tm_mon + 1, aTm->tm_mday, aTm->tm_hour,
             aTm->tm_min, aTm->tm_sec);
    return std::string(buf);
}

bool PlayerbotAIConfig::OpenLog(std::string const fileName, char const* mode)
{
    if (!HasLog(fileName))
        return false;

    auto logFileIt = LogFiles.find(fileName);
    if (logFileIt == LogFiles.end())
    {
        LogFiles.insert(std::make_pair(fileName, std::make_pair(nullptr, false)));
        logFileIt = LogFiles.find(fileName);
    }

    FILE* file = logFileIt->second.first;
    bool fileOpen = logFileIt->second.second;

    if (fileOpen)  // close log file
        fclose(file);

    std::string m_logsDir = sConfigMgr->GetStringDefault("LogsDir", "", false);
    if (!m_logsDir.empty())
    {
        if ((m_logsDir.at(m_logsDir.length() - 1) != '/') && (m_logsDir.at(m_logsDir.length() - 1) != '\\'))
            m_logsDir.append("/");
    }

    file = fopen((m_logsDir + fileName).c_str(), mode);
    fileOpen = true;

    logFileIt->second.first = file;
    logFileIt->second.second = fileOpen;

    return true;
}

void PlayerbotAIConfig::LogToFile(std::string const fileName, char const* str, ...)
{
    if (!str)
        return;

    std::lock_guard<std::mutex> guard(LogMutex);

    if (!IsLogOpen(fileName) && !OpenLog(fileName, "a"))
        return;

    FILE* file = LogFiles.find(fileName)->second.first;

    va_list ap;
    va_start(ap, str);
    vfprintf(file, str, ap);
    fprintf(file, "\n");
    va_end(ap);
    fflush(file);

    fflush(stdout);
}

void PlayerbotAIConfig::LoadWorldBuff()
{
    std::string matrix = sConfigMgr->GetStringDefault("Playerbots.WorldBuffMatrix", "", true);
    if (matrix.empty())
        return;

    std::istringstream entryStream(matrix);
    std::string entry;

    while (std::getline(entryStream, entry, ';'))
    {

        entry.erase(0, entry.find_first_not_of(" \t\r\n"));
        entry.erase(entry.find_last_not_of(" \t\r\n") + 1);

        size_t firstColon = entry.find(':');
        size_t secondColon = entry.find(':', firstColon + 1);

        if (firstColon == std::string::npos || secondColon == std::string::npos)
        {
            TC_LOG_ERROR("playerbots", "Malformed entry: [{}]", entry);
            continue;
        }

        std::string metaPart = entry.substr(firstColon + 1, secondColon - firstColon - 1);
        std::string spellPart = entry.substr(secondColon + 1);

        std::vector<uint32> ids;
        std::istringstream metaStream(metaPart);
        std::string token;
        while (std::getline(metaStream, token, ','))
        {
            try {
                ids.push_back(static_cast<uint32>(std::stoi(token)));
            } catch (...) {
                TC_LOG_ERROR("playerbots", "Invalid meta token in [{}]", entry);
                break;
            }
        }

        if (ids.size() != 5)
        {
            TC_LOG_ERROR("playerbots", "Entry [{}] has incomplete meta block", entry);
            continue;
        }

        std::istringstream spellStream(spellPart);
        while (std::getline(spellStream, token, ','))
        {
            try {
                uint32 spellId = static_cast<uint32>(std::stoi(token));
                WorldBuff wb = { spellId, ids[0], ids[1], ids[2], ids[3], ids[4] };
                WorldBuffs.push_back(wb);
            } catch (...) {
                TC_LOG_ERROR("playerbots", "Invalid spell ID in [{}]", entry);
            }
        }
    }
}

static std::vector<std::string> Split(std::string const& str, std::string const& pattern)
{
    std::vector<std::string> res;
    if (str == "")
        return res;
    // Also add separators to string connections to facilitate intercepting the last paragraph.
    std::string strs = str + pattern;
    size_t pos = strs.find(pattern);

    while (pos != strs.npos)
    {
        std::string temp = strs.substr(0, pos);
        res.push_back(temp);
        // Remove the split string and split the remaining string
        strs = strs.substr(pos + 1, strs.size());
        pos = strs.find(pattern);
    }

    return res;
}


