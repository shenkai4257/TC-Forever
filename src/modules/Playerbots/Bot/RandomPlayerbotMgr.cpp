/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RandomPlayerbotMgr.h"
#include "CharacterCache.h"
#include "FieldHelper.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "AiFactory.h"
#include "Cell.h"
#include "CellImpl.h"
#include "ChannelMgr.h"
#include "Common.h"
#include "Containers.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "DatabaseEnv.h"
#include "Define.h"
#include "FleeManager.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "JsonUtils.h"
#include "MapManager.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "ObjectGuid.h"
#include "PerfMonitor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "Position.h"
#include "Random.h"
#include "RandomPlayerbotFactory.h"
#include "ServerFacade.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TerrainMgr.h"
#include "TravelMgr.h"
#include "Unit.h"
#include "World.h"
#include "World.h"
#include <algorithm>
#include <boost/thread/thread.hpp>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <random>
#include <set>
#include <utility>

struct GuidClassRaceInfo
{
    ObjectGuid::LowType guid;
    uint32 rClass;
    uint32 rRace;
};

void PrintStatsThread() { sRandomPlayerbotMgr.PrintStats(); }

void activatePrintStatsThread()
{
    boost::thread t(PrintStatsThread);
    t.detach();
}

void CheckPlayersThread() { sRandomPlayerbotMgr.CheckPlayers(); }

void activateCheckPlayersThread()
{
    boost::thread t(CheckPlayersThread);
    t.detach();
}

class botPIDImpl
{
public:
    botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd);
    ~botPIDImpl();
    double calculate(double setpoint, double pv);
    void adjust(double Kp, double Ki, double Kd)
    {
        _Kp = Kp;
        _Ki = Ki;
        _Kd = Kd;
    }
    void reset() { _integral = 0; }

private:
    double _dt;
    double _max;
    double _min;
    double _Kp;
    double _Ki;
    double _Kd;
    double _pre_error;
    double _integral;
};

botPID::botPID(double dt, double max, double min, double Kp, double Ki, double Kd)
{
    pimpl = new botPIDImpl(dt, max, min, Kp, Ki, Kd);
}
void botPID::adjust(double Kp, double Ki, double Kd) { pimpl->adjust(Kp, Ki, Kd); }
void botPID::reset() { pimpl->reset(); }
double botPID::calculate(double setpoint, double pv) { return pimpl->calculate(setpoint, pv); }
botPID::~botPID() { delete pimpl; }

/**
 * Implementation
 */
botPIDImpl::botPIDImpl(double dt, double max, double min, double Kp, double Ki, double Kd)
    : _dt(dt), _max(max), _min(min), _Kp(Kp), _Ki(Ki), _Kd(Kd), _pre_error(0), _integral(0)
{
}

double botPIDImpl::calculate(double setpoint, double pv)
{
    // Calculate error
    double error = setpoint - pv;

    // Proportional term
    double Pout = _Kp * error;

    // Integral term
    _integral += error * _dt;
    double Iout = _Ki * _integral;

    // Derivative term
    double derivative = (error - _pre_error) / _dt;
    double Dout = _Kd * derivative;

    // Calculate total output
    double output = Pout + Iout + Dout;

    // Restrict to max/min
    if (output > _max)
    {
        output = _max;
        _integral -= error * _dt;  // Stop integral buildup at max
    }
    else if (output < _min)
    {
        output = _min;
        _integral -= error * _dt;  // Stop integral buildup at min
    }

    // Save error to previous error
    _pre_error = error;

    return output;
}

botPIDImpl::~botPIDImpl() {}

uint32 RandomPlayerbotMgr::GetMaxAllowedBotCount() { return GetEventValue(0, "bot_count"); }

void RandomPlayerbotMgr::LogPlayerLocation()
{
    activeBots = 0;

    try
    {
        sPlayerbotAIConfig.OpenLog("player_location.csv", "w");

        if (sPlayerbotAIConfig.RandomBotAutologin)
        {
            for (auto i : GetAllBots())
            {
                Player* bot = i.second;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "RND"
                    << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->GetRace()) << ",";
                out << std::to_string(bot->GetClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPct(bot->GetPowerType()) << ",";
                out << bot->GetMoney() << ",";

                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    out << std::to_string(uint8(botAI->GetGrouperType())) << ",";
                    out << std::to_string(uint8(botAI->GetGuilderType())) << ",";
                    out << (botAI->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (botAI->IsActive() ? "active" : "delay") << ",";
                    out << botAI->HandleRemoteCommand("state") << ",";

                    if (botAI->AllowActivity(ALL_ACTIVITY))
                        activeBots++;
                }
                else
                {
                    out << 0 << "," << 0 << ",err,err,err,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->isDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive");

                sPlayerbotAIConfig.LogToFile("player_location.csv", out.str().c_str());
            }

            for (auto i : GetPlayers())
            {
                Player* bot = i;
                if (!bot)
                    continue;

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << "PLR"
                    << ",";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);
                WorldPosition(bot).printWKT(out);
                out << bot->GetOrientation() << ",";
                out << std::to_string(bot->GetRace()) << ",";
                out << std::to_string(bot->GetClass()) << ",";
                out << bot->GetMapId() << ",";
                out << bot->GetLevel() << ",";
                out << bot->GetHealth() << ",";
                out << bot->GetPowerPct(bot->GetPowerType()) << ",";
                out << bot->GetMoney() << ",";

                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    out << std::to_string(uint8(botAI->GetGrouperType())) << ",";
                    out << std::to_string(uint8(botAI->GetGuilderType())) << ",";
                    out << (botAI->AllowActivity(ALL_ACTIVITY) ? "active" : "inactive") << ",";
                    out << (botAI->IsActive() ? "active" : "delay") << ",";
                    out << botAI->HandleRemoteCommand("state") << ",";

                    if (botAI->AllowActivity(ALL_ACTIVITY))
                        activeBots++;
                }
                else
                {
                    out << 0 << "," << 0 << ",player,player,player,";
                }

                out << (bot->IsInCombat() ? "combat" : "safe") << ",";
                out << (bot->isDead() ? (bot->GetCorpse() ? "ghost" : "dead") : "alive");

                sPlayerbotAIConfig.LogToFile("player_location.csv", out.str().c_str());
            }
        }
    }
    catch (...)
    {
        return;
        // This is to prevent some thread-unsafeness. Crashes would happen if bots get added or removed.
        // We really don't care here. Just skip a log. Making this thread-safe is not worth the effort.
    }
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 /*elapsed*/, bool /*minimal*/)
{
    if (totalPmo)
        totalPmo->finish();

    totalPmo = sPerfMonitor.start(PERF_MON_TOTAL, "RandomPlayerbotMgr::FullTick");

    if (!sPlayerbotAIConfig.RandomBotAutologin || !sPlayerbotAIConfig.Enabled)
        return;

    if (sPlayerbotAIConfig.RandomBotConcentrateInPlayerZone)
        UpdatePlayerZones();

    /*if (sPlayerbotAIConfig.enablePrototypePerformanceDiff)
    {
        TC_LOG_INFO("playerbots", "---------------------------------------");
        TC_LOG_INFO("playerbots",
                 "PROTOTYPE: Playerbot performance enhancements are active. Issues and instability may occur.");
        TC_LOG_INFO("playerbots", "---------------------------------------");
        ScaleBotActivity();
    }*/

    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");

    // Trackers reset is needed to enforce count mode changes done by a live config reload.
    if (sPlayerbotAIConfig.RandomBotCountMode != _rolledCountMode)
    {
        _rolledCountMode = sPlayerbotAIConfig.RandomBotCountMode;
        _botCountRolled = false;
        _nextScheduledCountUpdate = 0;
    }

    // A live config reload can move Min/MaxRandomBots past the held count, which forces a re-roll.
    bool countOutOfRange =
        maxAllowedBotCount < sPlayerbotAIConfig.MinRandomBots || maxAllowedBotCount > sPlayerbotAIConfig.MaxRandomBots;

    switch (sPlayerbotAIConfig.RandomBotCountMode)
    {
        case 1:
            // Mode 1 (variable): roll a count at server start and re-roll it every RandomBotCountChangeMin/MaxInterval.
            // The start roll is forced, in case there's a stale count from another mode.
            if (!_botCountRolled || !maxAllowedBotCount || countOutOfRange)
            {
                maxAllowedBotCount = urand(sPlayerbotAIConfig.MinRandomBots, sPlayerbotAIConfig.MaxRandomBots);
                SetEventValue(0, "bot_count", maxAllowedBotCount,
                              urand(sPlayerbotAIConfig.RandomBotCountChangeMinInterval,
                                    sPlayerbotAIConfig.RandomBotCountChangeMaxInterval));
                _botCountRolled = true;
            }
            break;
        case 2:
        {
            // Mode 2 (scheduled): population count is set according to a schedule from GetScheduledBotCount, that
            // changes on a fixed timing, every RandomBotCountChangeMinInterval.
            time_t now = GameTime::GetGameTime();
            if (now >= _nextScheduledCountUpdate)
            {
                maxAllowedBotCount = GetScheduledBotCount();
                SetEventValue(0, "bot_count", maxAllowedBotCount, DAY);
                _nextScheduledCountUpdate = now + sPlayerbotAIConfig.RandomBotCountChangeMinInterval;
            }
            break;
        }
        default:
            // Mode 0 (static): roll a count once at server start and hold it for the whole run.
            if (!_botCountRolled || countOutOfRange)
            {
                maxAllowedBotCount = urand(sPlayerbotAIConfig.MinRandomBots, sPlayerbotAIConfig.MaxRandomBots);
                SetEventValue(0, "bot_count", maxAllowedBotCount, YEAR);
                _botCountRolled = true;
            }
            break;
    }

    GetBots();
    // Copied deliberately: ProcessBot() erases from currentBots while this is
    // being iterated below, so the loops must run over a snapshot.
    std::unordered_set<uint32> availableBots = currentBots;
    uint32 availableBotCount = availableBots.size();
    uint32 onlineBotCount = playerBots.size();

    uint32 onlineBotFocus = 75;
    if (onlineBotCount < (uint32)(sPlayerbotAIConfig.MinRandomBots * 90 / 100))
        onlineBotFocus = 25;

    // only keep updating till initializing time has completed,
    // which prevents unneeded expensive GameTime calls.
    if (_isBotInitializing)
    {
        _isBotInitializing = GameTime::GetUptime() < sPlayerbotAIConfig.MaxRandomBots * (0.11 + 0.4);
    }

    uint32 updateIntervalTurboBoost = _isBotInitializing ? 1 : sPlayerbotAIConfig.RandomBotUpdateInterval;
    SetNextCheckDelay(updateIntervalTurboBoost * (onlineBotFocus + 25) * 10);

    PerfMonitorOperation* pmo = sPerfMonitor.start(
        PERF_MON_TOTAL,
        onlineBotCount < maxAllowedBotCount ? "RandomPlayerbotMgr::Login" : "RandomPlayerbotMgr::UpdateAIInternal");

    bool realPlayerIsLogged = false;
    if (sPlayerbotAIConfig.DisabledWithoutRealPlayer)
    {
        if (sWorld->GetActiveAndQueuedSessionCount() > 0)
        {
            RealPlayerLastTimeSeen = time(nullptr);
            realPlayerIsLogged = true;

            if (DelayLoginBotsTimer == 0)
            {
                DelayLoginBotsTimer = time(nullptr) + sPlayerbotAIConfig.DisabledWithoutRealPlayerLoginDelay;
            }
        }
        else
        {
            if (DelayLoginBotsTimer)
            {
                DelayLoginBotsTimer = 0;
            }

            if (RealPlayerLastTimeSeen != 0 && onlineBotCount > 0 &&
                time(nullptr) > RealPlayerLastTimeSeen + sPlayerbotAIConfig.DisabledWithoutRealPlayerLogoutDelay)
            {
                LogoutAllBots();
                TC_LOG_INFO("playerbots", "Logout all bots due no real player session.");
            }
        }

        if (availableBotCount < maxAllowedBotCount &&
            (sPlayerbotAIConfig.DisabledWithoutRealPlayer == false ||
             (realPlayerIsLogged && DelayLoginBotsTimer != 0 && time(nullptr) >= DelayLoginBotsTimer)))
        {
            AddRandomBots();
        }
    }
    else if (availableBotCount < maxAllowedBotCount)
    {
        AddRandomBots();
    }

    // If the randombot population requires a trim this cycle, it is not executed when DisabledWithoutRealPlayer is
    // enabled and there are no real players logged-in. In that case, DisabledWithoutRealPlayer handles logouts.
    if (availableBotCount > maxAllowedBotCount &&
        (!sPlayerbotAIConfig.DisabledWithoutRealPlayer || realPlayerIsLogged))
        RemoveRandomBots();

    if (sPlayerbotAIConfig.SyncLevelWithPlayers && !players.empty())
    {
        if (time(nullptr) > (PlayersCheckTimer + 60))
            sRandomPlayerbotMgr.CheckPlayers();
    }

    if (sPlayerbotAIConfig.RandomBotAutologin && sPlayerbotAIConfig.RandomBotPrintStatsInterval &&
        time(nullptr) > (printStatsTimer + sPlayerbotAIConfig.RandomBotPrintStatsInterval))
    {
        if (!printStatsTimer)
        {
            printStatsTimer = time(nullptr);
        }
        else
        {
            sRandomPlayerbotMgr.PrintStats();
            // activatePrintStatsThread();
        }
    }
    uint32 updateBots = sPlayerbotAIConfig.RandomBotsPerInterval * onlineBotFocus / 100;
    uint32 maxNewBots =
        onlineBotCount < maxAllowedBotCount &&
                (sPlayerbotAIConfig.DisabledWithoutRealPlayer == false ||
                 (realPlayerIsLogged && DelayLoginBotsTimer != 0 && time(nullptr) >= DelayLoginBotsTimer))
            ? maxAllowedBotCount - onlineBotCount
            : 0;
    uint32 loginBots = std::min(sPlayerbotAIConfig.RandomBotsPerInterval - updateBots, maxNewBots);

    if (!availableBots.empty())
    {
        // Update bots
        for (auto bot : availableBots)
        {
            if (!GetPlayerBot(bot))
                continue;

            if (ProcessBot(bot))
            {
                updateBots--;
            }

            if (!updateBots)
                break;
        }

        if (loginBots && botLoading.empty())
        {
            loginBots += updateBots;
            loginBots = std::min(loginBots, maxNewBots);

            TC_LOG_DEBUG("playerbots", "{} new bots prepared to login", loginBots);

            // Log in bots
            for (auto bot : availableBots)
            {
                if (GetPlayerBot(bot))
                    continue;

                if (ProcessBot(bot))
                {
                    loginBots--;
                }

                if (!loginBots)
                    break;
            }

            DelayLoginBotsTimer = 0;
        }
    }

    if (pmo)
        pmo->finish();

    if (sPlayerbotAIConfig.HasLog("player_location.csv"))
    {
        LogPlayerLocation();
    }
}

// void RandomPlayerbotMgr::ScaleBotActivity()
//{
//     float activityPercentage = getActivityPercentage();
//
//     // if (activityPercentage >= 100.0f || activityPercentage <= 0.0f) pid.reset(); //Stop integer buildup during
//     // max/min activity
//
//     //    % increase/decrease                   wanted diff                                         , avg diff
//     float activityPercentageMod = pid.calculate(
//         sRandomPlayerbotMgr.GetPlayers().empty() ? sPlayerbotAIConfig.diffEmpty :
//         sPlayerbotAIConfig.diffWithPlayer, sWorldUpdateTime.GetAverageUpdateTime());
//
//     activityPercentage = activityPercentageMod + 50;
//
//     // Cap the percentage between 0 and 100.
//     activityPercentage = std::max(0.0f, std::min(100.0f, activityPercentage));
//
//     setActivityPercentage(activityPercentage);
// }

// Assigns accounts as RNDbot accounts (type 1) based on MaxRandomBots, and assigns accounts as AddClass
// accounts (type 2) based AddClassAccountPoolSize. Type 1 and 2 assignments are permenant, unless MaxRandomBots
// or AddClassAccountPoolSize are set to 0. If so, their associated accounts will be unassigned (type 0)
void RandomPlayerbotMgr::AssignAccountTypes()
{
    TC_LOG_INFO("playerbots", "Assigning account types for random bot accounts...");

    // Clear existing filtered lists
    rndBotTypeAccounts.clear();
    addClassTypeAccounts.clear();

    // First, get ALL randombot accounts from the database
    std::vector<uint32> allRandomBotAccounts;
    QueryResult allAccounts = LoginDatabase.PQuery(
        "SELECT id FROM account WHERE username LIKE '{}%%' ORDER BY id",
        sPlayerbotAIConfig.RandomBotAccountPrefix.c_str());

    if (allAccounts)
    {
        do
        {
            Field* fields = allAccounts->Fetch();
            uint32 accountId = PlayerbotFieldGet<uint32>(fields[0]);
            allRandomBotAccounts.push_back(accountId);
        } while (allAccounts->NextRow());
    }

    TC_LOG_INFO("playerbots", "Found {} total randombot accounts in database", allRandomBotAccounts.size());

    // Check existing assignments
    QueryResult existingAssignments = CharacterDatabase.Query(
        "SELECT account_id, account_type FROM playerbots_account_type");
    std::map<uint32, uint8> currentAssignments;

    if (existingAssignments)
    {
        do
        {
            Field* fields = existingAssignments->Fetch();
            uint32 accountId = PlayerbotFieldGet<uint32>(fields[0]);
            uint8 accountType = PlayerbotFieldGet<uint8>(fields[1]);
            currentAssignments[accountId] = accountType;
        } while (existingAssignments->NextRow());
    }

    // Mark ALL randombot accounts as unassigned if not already assigned
    for (uint32 accountId : allRandomBotAccounts)
    {
        if (currentAssignments.find(accountId) == currentAssignments.end())
        {
            CharacterDatabase.DirectPExecute(
                "INSERT INTO playerbots_account_type (account_id, account_type) VALUES ({}, {}) ON DUPLICATE KEY UPDATE account_type = account_type",
                accountId, uint8(0));
            currentAssignments[accountId] = 0;
        }
    }

    // Calculate needed RNDbot accounts
    uint32 neededRndBotAccounts = 0;
    if (sPlayerbotAIConfig.MaxRandomBots > 0)
    {
        int divisor = RandomPlayerbotFactory::CalculateAvailableCharsPerAccount();
        int maxBots = sPlayerbotAIConfig.MaxRandomBots;

        // Calculate base accounts needed for RNDbots, ensuring round up for maxBots not cleanly divisible by the divisor
        neededRndBotAccounts = (maxBots + divisor - 1) / divisor;
    }

    // Count existing assigned accounts
    uint32 existingRndBotAccounts = 0;
    uint32 existingAddClassAccounts = 0;

    for (auto const& [accountId, accountType] : currentAssignments)
    {
        if (accountType == 1)
            existingRndBotAccounts++;
        else if (accountType == 2)
            existingAddClassAccounts++;
    }

    // Assign RNDbot accounts from lowest position if needed
    if (existingRndBotAccounts < neededRndBotAccounts)
    {
        uint32 toAssign = neededRndBotAccounts - existingRndBotAccounts;
        uint32 assigned = 0;

        for (uint32 i = 0; i < allRandomBotAccounts.size() && assigned < toAssign; i++)
        {
            uint32 accountId = allRandomBotAccounts[i];
            if (currentAssignments[accountId] == 0) // Unassigned
            {
                CharacterDatabase.DirectPExecute(
                    "UPDATE playerbots_account_type SET account_type = {}, assignment_date = NOW() WHERE account_id = {}",
                    uint8(1), accountId);
                currentAssignments[accountId] = 1;
                assigned++;
            }
        }

        if (assigned < toAssign)
            TC_LOG_ERROR("playerbots", "Not enough unassigned accounts to fulfill RNDbot requirements. Need {} more accounts.", toAssign - assigned);
    }

    // Assign AddClass accounts from highest position if needed
    uint32 neededAddClassAccounts = sPlayerbotAIConfig.AddClassAccountPoolSize;

    if (existingAddClassAccounts < neededAddClassAccounts)
    {
        uint32 toAssign = neededAddClassAccounts - existingAddClassAccounts;
        uint32 assigned = 0;

        for (size_t idx = allRandomBotAccounts.size(); idx-- > 0 && assigned < toAssign;)
        {
            uint32 accountId = allRandomBotAccounts[idx];
            if (currentAssignments[accountId] == 0) // Unassigned
            {
                CharacterDatabase.DirectPExecute(
                    "UPDATE playerbots_account_type SET account_type = {}, assignment_date = NOW() WHERE account_id = {}",
                    uint8(2), accountId);
                currentAssignments[accountId] = 2;
                assigned++;
            }
        }

        if (assigned < toAssign)
            TC_LOG_ERROR("playerbots", "Not enough unassigned accounts to fulfill AddClass requirements. Need {} more accounts.", toAssign - assigned);
    }

    // Populate filtered account lists with ALL accounts of each type
    for (auto const& [accountId, accountType] : currentAssignments)
    {
        if (accountType == 1)
            rndBotTypeAccounts.push_back(accountId);
        else if (accountType == 2)
            addClassTypeAccounts.push_back(accountId);
    }

    TC_LOG_INFO("playerbots", "Account type assignment complete: {} RNDbot accounts, {} AddClass accounts, {} unassigned",
             rndBotTypeAccounts.size(), addClassTypeAccounts.size(),
             currentAssignments.size() - rndBotTypeAccounts.size() - addClassTypeAccounts.size());
}

bool RandomPlayerbotMgr::IsAccountType(uint32 accountId, uint8 accountType)
{
    return CharacterDatabase.PQuery(
               "SELECT 1 FROM playerbots_account_type WHERE account_id = {} AND account_type = {}",
               accountId, accountType) != nullptr;
}

bool RandomPlayerbotMgr::IsAddClassAccount(uint32 accountId) const
{
    return std::find(addClassTypeAccounts.begin(), addClassTypeAccounts.end(), accountId) !=
           addClassTypeAccounts.end();
}

// Logs-in bots in 4 phases. Phase 1 logs Alliance bots up to how much is expected according to the faction ratio,
// and Phase 2 logs-in the remainder Horde bots to reach the total maxAllowedBotCount. If maxAllowedBotCount is not
// reached after Phase 2, the function goes back to log-in Alliance bots and reach maxAllowedBotCount. This is done
// because not every account is guaranteed 5A/5H bots, so the true ratio might be skewed by a few percentages.
// Finally, Phase 4 is reached if and only if the value of RandomBotAccountCount is lower than it should.
uint32 RandomPlayerbotMgr::AddRandomBots()
{
    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");
    static time_t missingBotsTimer = 0;

    if (currentBots.size() < maxAllowedBotCount)
    {
        // Calculate how many bots to add
        maxAllowedBotCount -= currentBots.size();
        maxAllowedBotCount = std::min(sPlayerbotAIConfig.RandomBotsPerInterval, maxAllowedBotCount);

        // Single RNG instance for all shuffling
        std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

        // Only need to track the Alliance count, as it's in Phase 1
        uint32 totalRatio = sPlayerbotAIConfig.RandomBotAllianceRatio + sPlayerbotAIConfig.RandomBotHordeRatio;
        uint32 allowedAllianceCount = maxAllowedBotCount * (sPlayerbotAIConfig.RandomBotAllianceRatio) / totalRatio;

        uint32 remainder = maxAllowedBotCount * (sPlayerbotAIConfig.RandomBotAllianceRatio) % totalRatio;

        // Fix #1082: Randomly add one based on reminder
        if (remainder && urand(1, totalRatio) <= remainder)
            allowedAllianceCount++;

        // Pre-map all characters from every RNDbot account.
        struct CharacterInfo
        {
            uint32 guid;
            uint8 rClass;
            uint8 rRace;
            uint32 accountId;
        };
        std::vector<CharacterInfo> allCharacters;

        for (uint32 accountId : rndBotTypeAccounts)
        {
            // 12.x's CHAR_SEL_CHARS_BY_ACCOUNT_ID selects guid only (3.3.5 returned
            // guid/name/race/class/...), so race and class need an explicit query.
            QueryResult result = CharacterDatabase.PQuery(
                "SELECT guid, race, class FROM characters WHERE account = {}", accountId);
            if (!result)
                continue;

            do
            {
                Field* fields = result->Fetch();
                CharacterInfo info;
                info.guid = PlayerbotFieldGet<uint32>(fields[0]);
                info.rRace = PlayerbotFieldGet<uint8>(fields[1]);
                info.rClass = PlayerbotFieldGet<uint8>(fields[2]);
                info.accountId = accountId;
                allCharacters.push_back(info);
            } while (result->NextRow());
        }

        // Shuffle for class balance
        std::shuffle(allCharacters.begin(), allCharacters.end(), rng);

        // Separate characters by faction for phased login
        std::vector<CharacterInfo> allianceChars;
        std::vector<CharacterInfo> hordeChars;

        for (auto const& charInfo : allCharacters)
        {
            if (IsAlliance(charInfo.rRace))
                allianceChars.push_back(charInfo);

            else
                hordeChars.push_back(charInfo);
        }

        // Lambda to handle bot login logic
        auto tryLoginBot = [&](CharacterInfo const& charInfo) -> bool
        {
            if (GetEventValue(charInfo.guid, "add") ||
                GetPlayerBot(charInfo.guid) ||
                currentBots.contains(charInfo.guid))
                return false;

            SetEventValue(charInfo.guid, "add", 1, YEAR);
            currentBots.insert(charInfo.guid);

            return true;
        };

        // PHASE 1: Log-in Alliance bots up to allowedAllianceCount
        for (auto const& charInfo : allianceChars)
        {
            if (!allowedAllianceCount)
                break;

            if (tryLoginBot(charInfo))
            {
                maxAllowedBotCount--;
                allowedAllianceCount--;
            }
        }

        // PHASE 2: Log-in Horde bots up to maxAllowedBotCount
        for (auto const& charInfo : hordeChars)
        {
            if (!maxAllowedBotCount)
                break;

            if (tryLoginBot(charInfo))
                maxAllowedBotCount--;
        }

        // PHASE 3: If maxAllowedBotCount wasn't reached, log-in more Alliance bots
        for (auto const& charInfo : allianceChars)
        {
            if (!maxAllowedBotCount)
                break;

            if (tryLoginBot(charInfo))
                maxAllowedBotCount--;
        }

        // PHASE 4: An error is given if maxAllowedBotCount is still not reached
        if (maxAllowedBotCount)
        {
            if (missingBotsTimer == 0)
                missingBotsTimer = time(nullptr);

            if (time(nullptr) - missingBotsTimer >= 10)
            {
                int divisor = RandomPlayerbotFactory::CalculateAvailableCharsPerAccount();
                uint32 moreAccountsNeeded = (maxAllowedBotCount + divisor - 1) / divisor;
                TC_LOG_ERROR("playerbots",
                          "Can't log-in all the requested bots. Try increasing RandomBotAccountCount in your conf file.\n"
                          "{} more accounts needed.", moreAccountsNeeded);
                missingBotsTimer = 0;   // Reset timer so error is not spammed every tick
            }
        }
        else
            missingBotsTimer = 0;       // Reset timer if logins for this interval were successful
    }
    else
        missingBotsTimer = 0;           // Reset timer if there's enough bots

    return currentBots.size();
}

// Whether a randombot can be safely logged out right now for a population downsize. Ineligible Bots are skipped
// and simply get trimmed on a later tick, if they become free and there's still unfulfilled demand for logouts.
bool RandomPlayerbotMgr::IsRemovableBot(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !IsRandomBot(bot))
        return false;

    if (bot->IsInCombat())
        return false;

    // In-flight or mid-teleport.
    if (bot->HasUnitState(UNIT_STATE_IN_FLIGHT) || bot->IsBeingTeleported())
        return false;

    // In a battleground, battlefield (Wintergrasp), arena or any of their queues.
    if (bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
        return false;

    // In a dungeon or raid (IsDungeon() covers both).
    if (bot->GetMap()->IsDungeon())
        return false;

    // Controlled by a real player. IsRandomBot is not enough if a real player is logged into a randombot.
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        if (botAI->HasGameClientMaster())
            return false;
    }

    // Grouped with a real player (including a selfbot).
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference const& ref : group->GetMembers())
        {
            Player* member = ref.GetSource();
            if (IsRealPlayer(member) || IsSelfBot(member))
                return false;
        }
    }

    return true;
}

// Logs out randombots when the online population is above the target count for this cycle.
uint32 RandomPlayerbotMgr::RemoveRandomBots()
{
    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");

    if (currentBots.size() <= maxAllowedBotCount)
        return 0;

    // Per-tick cap keeps the downsize gradual, matching how AddRandomBots throttles logins.
    uint32 toRemove = std::min(sPlayerbotAIConfig.RandomBotsPerInterval,
                               (uint32)(currentBots.size() - maxAllowedBotCount));

    // Ineligible bots still count toward their faction, as they keep occupying its share.
    std::vector<ObjectGuid> allianceBots;
    std::vector<ObjectGuid> hordeBots;
    uint32 onlineAlliance = 0;
    uint32 onlineHorde = 0;
    for (auto const& [guid, bot] : playerBots)
    {
        if (!IsRandomBot(bot))
            continue;

        bool isAlliance = IsAlliance(bot->GetRace());
        if (isAlliance)
            onlineAlliance++;
        else
            onlineHorde++;

        if (!IsRemovableBot(bot))
            continue;

        if (isAlliance)
            allianceBots.push_back(guid);
        else
            hordeBots.push_back(guid);
    }

    // Each faction is trimmed to its share of the target, so one at or below its share is left alone.
    uint32 totalRatio = sPlayerbotAIConfig.RandomBotAllianceRatio + sPlayerbotAIConfig.RandomBotHordeRatio;
    uint32 keepAlliance = maxAllowedBotCount * sPlayerbotAIConfig.RandomBotAllianceRatio / totalRatio;
    uint32 remainder = maxAllowedBotCount * sPlayerbotAIConfig.RandomBotAllianceRatio % totalRatio;
    if (remainder && urand(1, totalRatio) <= remainder)
        keepAlliance++;
    uint32 keepHorde = maxAllowedBotCount - keepAlliance;

    uint32 removeAlliance = onlineAlliance > keepAlliance ? onlineAlliance - keepAlliance : 0;
    uint32 removeHorde = onlineHorde > keepHorde ? onlineHorde - keepHorde : 0;

    // Never remove more than are actually available in each faction this tick.
    removeAlliance = std::min(removeAlliance, (uint32)allianceBots.size());
    removeHorde = std::min(removeHorde, (uint32)hordeBots.size());

    // Proportional split, so a budget-limited tick does not drain one faction first.
    if (removeAlliance + removeHorde > toRemove)
    {
        uint32 budgetAlliance = toRemove * removeAlliance / (removeAlliance + removeHorde);
        removeAlliance = std::min(removeAlliance, budgetAlliance);
        removeHorde = std::min(removeHorde, toRemove - budgetAlliance);
    }

    // playerBots is ordered by GUID, so without a shuffle the lowest GUIDs would always be the ones logged out.
    Trinity::Containers::RandomShuffle(allianceBots);
    Trinity::Containers::RandomShuffle(hordeBots);

    std::vector<ObjectGuid> toLogout;
    toLogout.insert(toLogout.end(), allianceBots.begin(), allianceBots.begin() + removeAlliance);
    toLogout.insert(toLogout.end(), hordeBots.begin(), hordeBots.begin() + removeHorde);

    // Reuse the same teardown as the 'add' event expiry path from ProcessBot.
    for (ObjectGuid guid : toLogout)
    {
        uint32 bot = guid.GetCounter();
        SetEventValue(bot, "add", 0, 0);
        currentBots.erase(bot);
        LogoutPlayerBot(guid);
    }

    if (!toLogout.empty())
        TC_LOG_DEBUG("playerbots", "Logged-out {} randombots to match target count of {}", toLogout.size(),
                  maxAllowedBotCount);

    return toLogout.size();
}

// Population target for RandomBotCountMode 2: a 24 hour cycle ramping from MinRandomBots at RandomBotCountMinTime
// up to MaxRandomBots at RandomBotCountMaxTime, then back down. The target population count is calculated from the
// clock at each interval, so the count is never desynced by a server shutdown/restart.
uint32 RandomPlayerbotMgr::GetScheduledBotCount()
{
    uint32 minCount = sPlayerbotAIConfig.MinRandomBots;
    uint32 maxCount = sPlayerbotAIConfig.MaxRandomBots;

    // No variation if either of the min/max values are equal to each other.
    if (sPlayerbotAIConfig.RandomBotCountMinTime == sPlayerbotAIConfig.RandomBotCountMaxTime || minCount == maxCount)
        return maxCount;

    // Timings are tracked by the second for accurate ramping.
    std::tm localTime = TimeBreakdown(GameTime::GetGameTime());
    uint32 localTimeOfDay = localTime.tm_hour * HOUR + localTime.tm_min * MINUTE + localTime.tm_sec;
    uint32 minCountTime = sPlayerbotAIConfig.RandomBotCountMinTime * HOUR;
    uint32 maxCountTime = sPlayerbotAIConfig.RandomBotCountMaxTime * HOUR;

    // Offsets run forward from minCountTime and wrap, so a maxCountTime earlier in the day needs no separate case.
    uint32 sinceMinCount = (localTimeOfDay + DAY - minCountTime) % DAY;
    uint32 riseDuration = (maxCountTime + DAY - minCountTime) % DAY;
    uint32 fallDuration = DAY - riseDuration;

    uint64 range = maxCount - minCount; // uint64, for a cursed server with more than 49710 bots.

    if (sinceMinCount < riseDuration)
        return static_cast<uint32>(minCount + range * sinceMinCount / riseDuration);

    return static_cast<uint32>(maxCount - range * (sinceMinCount - riseDuration) / fallDuration);
}

void RandomPlayerbotMgr::CheckPlayers()
{
    if (!PlayersCheckTimer || time(nullptr) > (PlayersCheckTimer + 60))
        PlayersCheckTimer = time(nullptr);

    TC_LOG_INFO("playerbots", "Checking Players...");

    if (!playersLevel)
        playersLevel = sPlayerbotAIConfig.RandomBotStartingLevel;

    for (std::vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* player = *i;

        if (player->IsGameMaster())
            continue;

        // if (player->GetSession()->GetSecurity() > SEC_PLAYER)
        //     continue;

        if (player->GetLevel() > playersLevel)
            playersLevel = player->GetLevel() + 3;
    }

    TC_LOG_INFO("playerbots", "Max player level is {}, max bot level set to {}", playersLevel - 3, playersLevel);
}

void RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time) { SetEventValue(bot, "randomize", 1, time); }

void RandomPlayerbotMgr::ScheduleTeleport(uint32 bot, uint32 time)
{
    if (!time)
        time = 60 + urand(sPlayerbotAIConfig.RandomBotUpdateInterval, sPlayerbotAIConfig.RandomBotUpdateInterval * 3);

    SetEventValue(bot, "teleport", 1, time);
}

void RandomPlayerbotMgr::ScheduleChangeStrategy(uint32 bot, uint32 time)
{
    if (!time)
        time = urand(sPlayerbotAIConfig.MinRandomBotChangeStrategyTime,
                     sPlayerbotAIConfig.MaxRandomBotChangeStrategyTime);

    SetEventValue(bot, "change_strategy", 1, time);
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    ObjectGuid botGUID = ObjectGuid::Create<HighGuid::Player>(bot);
    Player* player = GetPlayerBot(botGUID);
    PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;

    uint32 isValid = GetEventValue(bot, "add");
    if (!isValid)
    {
        if (!player || !player->GetGroup())
        {
            if (player)
                TC_LOG_DEBUG("playerbots", "Bot #{} {}:{} <{}>: log out", bot, IsAlliance(player->GetRace()) ? "A" : "H",
                          player->GetLevel(), player->GetName().c_str());
            else
                TC_LOG_DEBUG("playerbots", "Bot #{}: log out", bot);

            SetEventValue(bot, "add", 0, 0);
            currentBots.erase(bot);

            if (player)
                LogoutPlayerBot(botGUID);
        }

        return false;
    }

    uint32 randomTime;
    if (!player)
    {
        AddPlayerBot(botGUID, 0);
        randomTime = urand(1, 2);

        uint32 randomBotUpdateInterval = _isBotInitializing ? 1 : sPlayerbotAIConfig.RandomBotUpdateInterval;
        randomTime = urand(std::max(5, static_cast<int>(randomBotUpdateInterval * 0.5)),
                           std::max(12, static_cast<int>(randomBotUpdateInterval * 2)));
        SetEventValue(bot, "update", 1, randomTime);

        // do not randomize or teleport immediately after server start (prevent lagging)
        if (!GetEventValue(bot, "randomize"))
        {
            randomTime = urand(3, std::max(4, static_cast<int>(randomBotUpdateInterval * 0.4)));
            ScheduleRandomize(bot, randomTime);
        }
        if (!GetEventValue(bot, "teleport"))
        {
            randomTime = urand(std::max(7, static_cast<int>(randomBotUpdateInterval * 0.7)),
                               std::max(14, static_cast<int>(randomBotUpdateInterval * 1.4)));
            ScheduleTeleport(bot, randomTime);
        }

        return true;
    }

    if (!player->IsInWorld())
        return false;

    if (player->GetGroup() || player->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return false;

    uint32 update = GetEventValue(bot, "update");
    if (!update)
    {
        if (botAI)
            botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Set(true);

        bool update = true;
        if (botAI)
        {
            // botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Set(true);
            if (!sRandomPlayerbotMgr.IsRandomBot(player))
                update = false;

            if (player->GetGroup() && botAI->GetGroupLeader())
            {
                PlayerbotAI* groupLeaderBotAI = GET_PLAYERBOT_AI(botAI->GetGroupLeader());
                if (!groupLeaderBotAI || IsSelfBot(botAI->GetGroupLeader()))
                    update = false;
            }

            // if (botAI->HasPlayerNearby(sPlayerbotAIConfig.GrindDistance))
            //     update = false;
        }

        if (update)
            ProcessBot(player);

        randomTime = urand(sPlayerbotAIConfig.MinRandomBotReviveTime, sPlayerbotAIConfig.MaxRandomBotReviveTime);
        SetEventValue(bot, "update", 1, randomTime);

        return true;
    }

    return false;
}

bool RandomPlayerbotMgr::ProcessBot(Player* bot)
{

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    if (bot->InBattleground())
        return false;

    if (bot->InBattlegroundQueue())
        return false;

     uint32 botId = bot->GetGUID().GetCounter();

    // if death revive
    if (bot->isDead())
    {
        if (!GetEventValue(botId, "dead"))
        {
            uint32 randomTime =
                urand(sPlayerbotAIConfig.MinRandomBotReviveTime, sPlayerbotAIConfig.MaxRandomBotReviveTime);
            TC_LOG_DEBUG("playerbots", "Mark bot {} as dead, will be revived in {}s.", bot->GetName().c_str(),
                      randomTime);
            SetEventValue(botId, "dead", 1, DAY);
            SetEventValue(botId, "revive", 1, randomTime);
            return false;
        }

        if (!GetEventValue(botId, "revive"))
        {
            Revive(bot);
            return true;
        }

        return false;
    }

    // leave group if leader is rndbot
    Group* group = bot->GetGroup();
    if (group && !group->isLFGGroup() && IsRandomBot(group->GetLeaderGUID().GetCounter()))
    {
        botAI->LeaveOrDisbandGroup();
        TC_LOG_INFO("playerbots", "Bot {} remove from group since leader is random bot.", bot->GetName().c_str());
    }

    // only randomize and teleport idle bots
    bool idleBot = false;
    if (TravelTarget* target = botAI->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get())
    {
        if (target->getTravelState() == TravelState::TRAVEL_STATE_IDLE)
        {
            idleBot = true;
        }
    }
    else
    {
        idleBot = true;
    }

    if (idleBot)
    {
        // randomize
        uint32 randomize = GetEventValue(botId, "randomize");
        if (!randomize)
        {
            // bool randomiser = true;
            // if (player->GetGuildId())
            // {
            //     if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
            //     {
            //         if (guild->GetLeaderGUID() == player->GetGUID())
            //         {
            //             for (std::vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
            //                 GuildTaskMgr::instance().Update(*i, player);
            //         }

            //         uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guild->GetLeaderGUID());
            //         if (!sPlayerbotAIConfig.IsInRandomAccountList(accountId))
            //         {
            //             uint8 rank = player->GetRank();
            //             randomiser = rank < 4 ? false : true;
            //         }
            //     }
            // }
            // if (randomiser)
            // {
            Randomize(bot);
            TC_LOG_DEBUG("playerbots", "Bot #{} {}:{} <{}>: randomized", botId,
                      bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
            uint32 randomTime =
                urand(sPlayerbotAIConfig.MinRandomBotRandomizeTime, sPlayerbotAIConfig.MaxRandomBotRandomizeTime);
            ScheduleRandomize(botId, randomTime);
            return true;
        }

        // uint32 changeStrategy = GetEventValue(bot, "change_strategy");
        // if (!changeStrategy)
        // {
        //     TC_LOG_INFO("playerbots", "Changing strategy for bot  #{} <{}>", bot, player->GetName().c_str());
        //     ChangeStrategy(player);
        //     return true;
        // }

        uint32 teleport = GetEventValue(botId, "teleport");
        if (!teleport)
        {
            TC_LOG_DEBUG("playerbots", "Bot #{} <{}>: teleport for level and refresh", botId, bot->GetName());
            Refresh(bot);
            RandomTeleportForLevel(bot);
            uint32 time = urand(sPlayerbotAIConfig.MinRandomBotTeleportInterval,
                                sPlayerbotAIConfig.MaxRandomBotTeleportInterval);
            ScheduleTeleport(botId, time);
            return true;
        }
    }

    return false;
}

void RandomPlayerbotMgr::Revive(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    // TC_LOG_INFO("playerbots", "Bot {} revived", player->GetName().c_str());
    SetEventValue(bot, "dead", 0, 0);
    SetEventValue(bot, "revive", 0, 0);

    Refresh(player);
    RandomTeleportGrindForLevel(player);
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, std::vector<WorldLocation>& locs, bool hearth)
{
    // ignore when alrdy teleported or not in the world yet.
    if (bot->IsBeingTeleported() || !bot->IsInWorld())
        return;

    // no teleport / movement update when rooted.
    if (bot->HasUnitState(UNIT_STATE_ROOT))
        return;

    // ignore when in queue for battle grounds.
    if (bot->InBattlegroundQueue())
        return;

    // ignore when in battle grounds or arena.
    if (bot->InBattleground() || bot->InArena())
        return;

    // ignore when in group (e.g. world, dungeons, raids) and leader is not a player.
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (botAI)
    {
        // ignore when in when taxi with boat/zeppelin and has players nearby
        if ((bot->GetTransport() != nullptr) && bot->HasUnitState(UNIT_STATE_IGNORE_PATHFINDING) &&
            botAI->HasPlayerNearby())
            return;
    }

    // if (sPlayerbotAIConfig.RandomBotRpgChance < 0)
    //     return;

    if (locs.empty())
    {
        TC_LOG_DEBUG("playerbots", "Cannot teleport bot {} - no locations available", bot->GetName().c_str());
        return;
    }

    std::vector<WorldPosition> tlocs;
    for (auto& loc : locs)
        tlocs.push_back(WorldPosition(loc));
    // Do not teleport to maps disabled in config
    tlocs.erase(std::remove_if(tlocs.begin(), tlocs.end(),
                               [](WorldPosition l)
                               {
                                   std::vector<uint32>::iterator i =
                                       find(sPlayerbotAIConfig.RandomBotMaps.begin(),
                                            sPlayerbotAIConfig.RandomBotMaps.end(), l.GetMapId());
                                   return i == sPlayerbotAIConfig.RandomBotMaps.end();
                               }),
                tlocs.end());
    if (tlocs.empty())
    {
        TC_LOG_DEBUG("playerbots", "Cannot teleport bot {} - all locations removed by filter", bot->GetName().c_str());
        return;
    }

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomTeleportByLocations");

    Trinity::Containers::RandomShuffle(tlocs);
    for (uint32 i = 0; i < tlocs.size(); i++)
    {
        WorldLocation loc = tlocs[i];

        float x = loc.GetPositionX();  // + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.GrindDistance) -
                                       // sPlayerbotAIConfig.GrindDistance / 2 : 0);
        float y = loc.GetPositionY();  // + (attemtps > 0 ? urand(0, sPlayerbotAIConfig.GrindDistance) -
                                       // sPlayerbotAIConfig.GrindDistance / 2 : 0);
        float z = loc.GetPositionZ();

        Map* map = sMapMgr->FindMap(loc.GetMapId(), 0);
        if (!map)
            continue;

        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(map->GetZoneId(bot->GetPhaseShift(), x, y, z));
        if (!zone)
            continue;

        AreaTableEntry const* area = sAreaTableStore.LookupEntry(map->GetAreaId(bot->GetPhaseShift(), x, y, z));
        if (!area)
            continue;

        // Do not teleport to enemy zones if level is low
        if ((zone->FactionGroupMask & FACTION_MASK_ALLIANCE) && bot->GetTeamId() == TEAM_ALLIANCE)
            continue;

        if ((zone->FactionGroupMask & FACTION_MASK_HORDE) && bot->GetTeamId() == TEAM_HORDE)
            continue;

        if (map->IsInWater(bot->GetPhaseShift(), x, y, z))
            continue;

        float ground = map->GetHeight(bot->GetPhaseShift(), x, y, z + 0.5f);
        if (ground <= INVALID_HEIGHT)
            continue;

        z = 0.05f + ground;

        if (!botAI->StarterLevelDistanceCheck(bot, loc, true))
            continue;

        LocaleConstant const& locale = sWorld->GetDefaultDbcLocale();
        TC_LOG_DEBUG("playerbots",
                  "Random teleporting bot {} (level {}) to Map: {} ({}) Zone: {} ({}) Area: {} ({}) ZoneLevel: {} "
                  "AreaLevel: {} {},{},{} ({}/{} "
                  "locations)",
                  bot->GetName().c_str(), bot->GetLevel(), map->GetId(), map->GetMapName(), zone->ID,
                  zone->AreaName[locale], area->ID, area->AreaName[locale], 0, 0, x, y,
                  z, i + 1, tlocs.size());

        if (hearth)
        {
            bot->SetHomebind(loc, zone->ID);
        }

        // Prevent blink to be detected by visible real players
        if (botAI->HasPlayerNearby(150.0f))
        {
            break;
        }

        bot->GetMotionMaster()->Clear();
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->Reset(true);
        bot->RemoveAurasWithInterruptFlags(SpellAuraInterruptFlags::LeaveWorld | SpellAuraInterruptFlags::EnterWorld);
        bot->TeleportTo(loc.GetMapId(), x, y, z, 0);

        if (pmo)
            pmo->finish();

        return;
    }

    if (pmo)
        pmo->finish();

    // TC_LOG_ERROR("playerbots", "Cannot teleport bot {} - no locations available ({} locations)", bot->GetName().c_str(),
    //           tlocs.size());
}

void RandomPlayerbotMgr::PrepareAddclassCache()
{
    // Using accounts marked as type 2 (AddClass)
    int32 collected = 0;

    for (uint32 accountId : addClassTypeAccounts)
    {
        for (uint8 claz = CLASS_WARRIOR; claz <= CLASS_DRUID; claz++)
        {
            // 1.60 has nine classes; the other class ids are empty slots.
            if (!PlayerbotCompat::IsClassicClass(claz))
                continue;

            QueryResult results = CharacterDatabase.PQuery(
                "SELECT guid, race FROM characters "
                "WHERE account = {} AND class = '{}' AND online = 0",
                accountId, claz);

            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(PlayerbotFieldGet<uint32>(fields[0]));
                    uint32 race = PlayerbotFieldGet<uint32>(fields[1]);
                    bool isAlliance = race == 1 || race == 3 || race == 4 || race == 7 || race == 11;
                    addclassCache[GetTeamClassIdx(isAlliance, claz)].insert(guid);
                    collected++;
                } while (results->NextRow());
            }
        }
    }

    TC_LOG_INFO("playerbots", ">> {} characters collected for addclass command from {} AddClass accounts.", collected, addClassTypeAccounts.size());
}

void RandomPlayerbotMgr::Init()
{
    if (sPlayerbotAIConfig.AddClassCommand)
        sRandomPlayerbotMgr.PrepareAddclassCache();

    CharacterDatabase.DirectExecute("DELETE FROM playerbots_random_bots WHERE event = 'add'");
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot)
{
    if (bot->InBattleground())
        return;

    // One weighted roll across the three destination classes, so each config value is an exact
    // share of the roll (they are peers, not nested gates):
    //   quest giver   : ProbTeleToQuestGivers
    //   city/banker   : ProbTeleToBankers (level >= 10 only)
    //   innkeeper/hub : the remainder (1 - quest giver - banker); absorbs any branch that had
    //                   no destination (no valid giver, below level 10, empty city cache)
    uint32 roll = urand(0, 99);

    if (roll < sPlayerbotAIConfig.ProbTeleToQuestGivers * 100)
    {
        std::vector<WorldLocation> qlocs = sTravelMgr.GetValidQuestGiverLocations(bot);

        // Concentrated servers keep bots near players: only givers in a player zone count, and
        // otherwise the roll falls through to the hub path.
        if (!qlocs.empty() && sPlayerbotAIConfig.RandomBotConcentrateInPlayerZone)
        {
            std::vector<WorldLocation> playerZoneLocs = GetPlayerZoneTeleportLocations(qlocs, bot);
            qlocs = std::move(playerZoneLocs);
        }

        if (!qlocs.empty())
        {
            TC_LOG_DEBUG("playerbots", "Random teleporting bot {} to a quest giver ({} candidates)",
                      bot->GetName().c_str(), qlocs.size());
            RandomTeleport(bot, qlocs, /*hearth=*/false);
            return;
        }
    }
    else if (bot->GetLevel() >= 10
             && roll < (sPlayerbotAIConfig.ProbTeleToQuestGivers + sPlayerbotAIConfig.ProbTeleToBankers) * 100)
    {
        std::vector<WorldLocation> locs = sTravelMgr.GetCityLocations(bot);
        if (!locs.empty())
        {
            RandomTeleport(bot, locs, /*hearth=*/true);
            return;
        }
    }

    std::vector<WorldLocation> locs = sTravelMgr.GetTeleportLocations(bot);

    if (sPlayerbotAIConfig.RandomBotConcentrateInPlayerZone)
    {
        std::vector<WorldLocation> playerZoneLocs = GetPlayerZoneHubLocations(locs, bot);
        if (!playerZoneLocs.empty())
            locs = std::move(playerZoneLocs);
    }

    if (!locs.empty())
    {
        RandomTeleport(bot, locs, false);
        return;
    }
}

// Returns the subset of teleport locations that lie in a zone currently occupied by a real
// (non-GM) player, so random bots can be gathered where players actually are. Returns an empty
// vector when no player is online or none of the locations match, letting the caller fall back
// to the default world-wide behaviour.
std::vector<WorldLocation> RandomPlayerbotMgr::GetPlayerZoneTeleportLocations(std::vector<WorldLocation> const& locs,
                                                                              Player* bot)
{
    return FilterPlayerZoneTeleportLocations(locs, bot, GetPlayerZones());
}

// Same filter for the bot's level hubs, falling back to the hubs of nearby levels.
std::vector<WorldLocation> RandomPlayerbotMgr::GetPlayerZoneHubLocations(std::vector<WorldLocation> const& hubs,
                                                                         Player* bot)
{
    std::set<std::pair<uint32, uint32>> const playerMapZones = GetPlayerZones();
    std::vector<WorldLocation> filtered = FilterPlayerZoneTeleportLocations(hubs, bot, playerMapZones);

    // Hubs are indexed only for their zone's level range; the legacy cache already includes the margin.
    if (!filtered.empty() || playerMapZones.empty() || !sPlayerbotAIConfig.EnableNewRpgStrategy)
        return filtered;

    uint32 botLevel = bot->GetLevel();
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 higher = sPlayerbotAIConfig.RandomBotTeleHigherLevel;
    uint32 lower = sPlayerbotAIConfig.RandomBotTeleLowerLevel;
    uint32 maxOffset = std::min(std::max(higher, lower), maxLevel);
    for (uint32 offset = 1; offset <= maxOffset; ++offset)
    {
        if (offset <= higher && botLevel > offset)
            filtered = FilterPlayerZoneTeleportLocations(sTravelMgr.GetTeleportLocations(bot, botLevel - offset), bot,
                                                         playerMapZones);

        if (filtered.empty() && offset <= lower && botLevel + offset <= maxLevel)
            filtered = FilterPlayerZoneTeleportLocations(sTravelMgr.GetTeleportLocations(bot, botLevel + offset), bot,
                                                         playerMapZones);

        if (!filtered.empty())
            break;
    }

    return filtered;
}

std::set<std::pair<uint32, uint32>> RandomPlayerbotMgr::GetPlayerZones()
{
    std::lock_guard<std::mutex> lock(_playerZonesLock);
    return _playerZones;
}

std::vector<WorldLocation> RandomPlayerbotMgr::FilterPlayerZoneTeleportLocations(
    std::vector<WorldLocation> const& locs, Player* bot, std::set<std::pair<uint32, uint32>> const& playerMapZones)
{
    std::vector<WorldLocation> filtered;
    if (playerMapZones.empty())
        return filtered;

    std::set<uint32> playerMaps;
    for (auto const& mapZone : playerMapZones)
        playerMaps.insert(mapZone.first);

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    for (WorldLocation const& loc : locs)
    {
        if (playerMaps.find(loc.GetMapId()) == playerMaps.end())
            continue;

        uint32 zoneId = sTerrainMgr.GetZoneId(PhaseShift(), loc);
        if (playerMapZones.find(std::make_pair(loc.GetMapId(), zoneId)) == playerMapZones.end())
            continue;

        // Skip enemy-faction zones, matching the check in RandomTeleport. This has to be done here:
        // the caller only falls back to normal teleporting when the filtered set is empty, so keeping
        // a hostile location would let the downstream team check drain the set and strand the bot.
        if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(zoneId))
        {
            if ((zone->FactionGroupMask & FACTION_MASK_ALLIANCE) && bot->GetTeamId() == TEAM_ALLIANCE)
                continue;

            if ((zone->FactionGroupMask & FACTION_MASK_HORDE) && bot->GetTeamId() == TEAM_HORDE)
                continue;
        }

        if (std::find(sPlayerbotAIConfig.RandomBotMaps.begin(), sPlayerbotAIConfig.RandomBotMaps.end(),
                      loc.GetMapId()) == sPlayerbotAIConfig.RandomBotMaps.end())
            continue;

        if (botAI && !botAI->StarterLevelDistanceCheck(bot, loc, true))
            continue;

        filtered.push_back(loc);
    }

    return filtered;
}

// World thread only.
void RandomPlayerbotMgr::UpdatePlayerZones()
{
    std::set<std::pair<uint32, uint32>> playerMapZones;

    for (Player* player : players)
    {
        if (!player || !player->IsInWorld() || player->IsGameMaster())
            continue;

        if (!IsRealPlayer(player) && !IsSelfBot(player))
            continue;

        Map* map = player->GetMap();
        if (!map)
            continue;

        // WorldLocation has no instance id.
        if (map->Instanceable())
            continue;

        uint32 zoneId =
            map->GetZoneId(PhaseShift(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        playerMapZones.insert(std::make_pair(map->GetId(), zoneId));
    }

    std::lock_guard<std::mutex> lock(_playerZonesLock);
    _playerZones = std::move(playerMapZones);
}

void RandomPlayerbotMgr::RandomTeleportGrindForLevel(Player* bot)
{
    if (bot->InBattleground())
        return;

    std::vector<WorldLocation> locs = sTravelMgr.GetTeleportLocations(bot);
    TC_LOG_DEBUG("playerbots", "Random teleporting bot {} for level {} ({} locations available)", bot->GetName().c_str(),
              bot->GetLevel(), locs.size());

    RandomTeleport(bot, locs);
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot)
{
    if (bot->InBattleground())
        return;

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomTeleport");
    std::vector<WorldLocation> locs;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.RandomBotTeleportDistance;
    Trinity::AnyUnitInObjectRangeCheck u_check(bot, range);
    Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitAllObjects(bot, searcher, range);

    if (!targets.empty())
    {
        for (Unit* unit : targets)
        {
            bot->UpdatePosition(*unit);
            FleeManager manager(bot, sPlayerbotAIConfig.SightDistance, 0, true);
            float rx, ry, rz;
            if (manager.CalculateDestination(&rx, &ry, &rz))
            {
                WorldLocation loc(bot->GetMapId(), rx, ry, rz);
                locs.push_back(loc);
            }
        }
    }
    else
    {
        RandomTeleportForLevel(bot);
    }

    if (pmo)
        pmo->finish();

    Refresh(bot);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (bot->InBattleground())
        return;

    if (bot->GetLevel() < 3)
    {
        RandomizeFirst(bot);
    }
    else if (bot->GetLevel() < sPlayerbotAIConfig.RandomBotMaxLevel || !sPlayerbotAIConfig.DowngradeMaxLevelBot)
    {
        uint8 level = bot->GetLevel();
        PlayerbotFactory factory(bot, level);
        factory.Randomize(true);
        // IncreaseLevel(bot);
    }
    else
    {
        RandomizeFirst(bot);
    }
}

void RandomPlayerbotMgr::IncreaseLevel(Player* bot)
{
    uint32 maxLevel = sPlayerbotAIConfig.RandomBotMaxLevel;
    if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "IncreaseLevel");
    uint32 lastLevel = GetValue(bot, "level");
    uint8 level = bot->GetLevel() + 1;
    if (level > maxLevel)
    {
        level = maxLevel;
    }
    if (lastLevel != level)
    {
        PlayerbotFactory factory(bot, level);
        factory.Randomize(true);
    }

    if (pmo)
        pmo->finish();
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    uint32 maxLevel = sPlayerbotAIConfig.RandomBotMaxLevel;
    if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    // if lvl sync is enabled, max level is limited by online players lvl
    if (sPlayerbotAIConfig.SyncLevelWithPlayers)
        maxLevel = std::max(sPlayerbotAIConfig.RandomBotMinLevel,
                            std::min(playersLevel, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)));

    uint32 minLevel = sPlayerbotAIConfig.RandomBotMinLevel;
    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomizeFirst");

    uint32 level;

    if (sPlayerbotAIConfig.DowngradeMaxLevelBot && bot->GetLevel() >= sPlayerbotAIConfig.RandomBotMaxLevel)
    {
        level = sPlayerbotAIConfig.RandomBotMinLevel;
    }
    else
    {
        // Roll for the top or the bottom of the level range, otherwise land anywhere in between.
        float maxLevelChance = sPlayerbotAIConfig.RandomBotMaxLevelChance;
        float minLevelChance = sPlayerbotAIConfig.RandomBotMinLevelChance;
        uint32 roll = urand(1, 100);

        if (roll <= 100 * maxLevelChance)
            level = maxLevel;
        else if (roll <= 100 * (maxLevelChance + minLevelChance))
            level = minLevel;
        else
            level = urand(minLevel, maxLevel);
    }

    if (sPlayerbotAIConfig.DisableRandomLevels)
    {
        level = sPlayerbotAIConfig.RandomBotStartingLevel;
    }

    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false);

    uint32 randomTime =
        urand(sPlayerbotAIConfig.MinRandomBotRandomizeTime, sPlayerbotAIConfig.MaxRandomBotRandomizeTime);

    CharacterDatabase.PExecute(
        "UPDATE playerbots_random_bots SET validIn = {} WHERE event = 'bot_delete' AND bot = {}",
        randomTime, bot->GetGUID().GetCounter());

    // teleport to a random inn for bot level
    botAI->Reset(true);

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();

    RandomTeleportForLevel(bot);
}

void RandomPlayerbotMgr::RandomizeMin(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "RandomizeMin");
    uint32 level = sPlayerbotAIConfig.RandomBotMinLevel;
    SetValue(bot, "level", level);
    PlayerbotFactory factory(bot, level);
    factory.Randomize(false);

    uint32 randomTime =
        urand(sPlayerbotAIConfig.MinRandomBotRandomizeTime, sPlayerbotAIConfig.MaxRandomBotRandomizeTime);

    CharacterDatabase.PExecute(
        "UPDATE playerbots_random_bots SET validIn = {} WHERE event = 'bot_delete' AND bot = {}",
        randomTime, bot->GetGUID().GetCounter());

    // teleport to a random inn for bot level
    botAI->Reset(true);

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();
}

void RandomPlayerbotMgr::Clear(Player* bot)
{
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.ClearEverything();
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float /*teleZ*/)
{
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    uint32 level = 0;
    QueryResult results = WorldDatabase.PQuery(
        "SELECT AVG(t.minlevel) minlevel, AVG(t.maxlevel) maxlevel FROM creature c "
        "INNER JOIN creature_template t ON c.id = t.entry WHERE map = {} AND minlevel > 1 AND ABS(position_x - {}) < "
        "{} AND ABS(position_y - {}) < {}",
        mapId, teleX, sPlayerbotAIConfig.RandomBotTeleportDistance / 2, teleY,
        sPlayerbotAIConfig.RandomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        uint8 minLevel = PlayerbotFieldGet<uint8>(fields[0]);
        uint8 maxLevel = PlayerbotFieldGet<uint8>(fields[1]);
        level = urand(minLevel, maxLevel);
        if (level > maxLevel)
            level = maxLevel;
    }
    else
    {
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    if (bot->isDead())
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        botAI->ResetStrategies(false);
    }

    // if (sPlayerbotAIConfig.DisableRandomLevels)
    //     return;

    if (bot->InBattleground())
        return;

    TC_LOG_DEBUG("playerbots", "Refreshing bot {} <{}>", bot->GetGUID().ToString().c_str(), bot->GetName().c_str());

    PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_RNDBOT, "Refresh");

    botAI->Reset();

    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SetFullHealth();
    bot->SetPvP(sWorld->IsPvPRealm());
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.Refresh();

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));

    uint32 money = bot->GetMoney();
    bot->SetMoney(money + 500 * sqrt(urand(1, bot->GetLevel() * 5)));

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    if (pmo)
        pmo->finish();
}

bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    if (bot && GET_PLAYERBOT_AI(bot))
    {
        if (IsSelfBot(bot))
            return false;
    }
    if (bot)
    {
        return IsRandomBot(bot->GetGUID().GetCounter());
    }

    return false;
}

bool RandomPlayerbotMgr::IsRandomBot(ObjectGuid::LowType bot)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(bot);
    if (!sPlayerbotAIConfig.IsInRandomAccountList(sCharacterCache->GetCharacterAccountIdByGuid(guid)))
        return false;

    return currentBots.contains(bot);
}

bool RandomPlayerbotMgr::IsAddclassBot(Player* bot)
{
    if (bot && GET_PLAYERBOT_AI(bot))
    {
        if (IsSelfBot(bot))
            return false;
    }
    if (bot)
    {
        return IsAddclassBot(bot->GetGUID().GetCounter());
    }

    return false;
}

bool RandomPlayerbotMgr::IsAddclassBot(ObjectGuid::LowType bot)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(bot);

    // Check the cache with faction considerations
    for (uint8 claz = CLASS_WARRIOR; claz <= CLASS_DRUID; claz++)
    {
        // 1.60 has nine classes; the other class ids are empty slots.
        if (!PlayerbotCompat::IsClassicClass(claz))
            continue;

        for (uint8 isAlliance = 0; isAlliance <= 1; isAlliance++)
        {
            if (addclassCache[GetTeamClassIdx(isAlliance, claz)].find(guid) !=
                addclassCache[GetTeamClassIdx(isAlliance, claz)].end())
            {
                return true;
            }
        }
    }

    // If not in cache, check the account type
    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (accountId && IsAccountType(accountId, 2)) // Type 2 = AddClass
    {
        return true;
    }

    return false;
}

void RandomPlayerbotMgr::GetBots()
{
    if (!currentBots.empty())
        return;

    uint32 maxAllowedBotCount = GetEventValue(0, "bot_count");
    if (QueryResult result = CharacterDatabase.Query(
            "SELECT bot FROM playerbots_random_bots WHERE owner = 0 AND event = 'add'"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 bot = PlayerbotFieldGet<uint32>(fields[0]);
            if (GetEventValue(bot, "add"))
                currentBots.insert(bot);

            if (currentBots.size() >= maxAllowedBotCount)
                break;
        } while (result->NextRow());
    }
}

CachedEvent* RandomPlayerbotMgr::FindEvent(uint32 bot, std::string const& event)
{
    BotEventCache& cache = eventCache[bot];

    // Load once
    if (!cache.loaded)
    {
        cache.events.clear();

        if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT `event`, `value`, `time`, validIn, `data` FROM playerbots_random_bots WHERE owner = 0 AND bot = {}",
                bot))
        {
            do
            {
                Field* fields = result->Fetch();

                CachedEvent e;
                e.value = PlayerbotFieldGet<uint32>(fields[1]);
                e.lastChangeTime = PlayerbotFieldGet<uint32>(fields[2]);
                e.validIn = PlayerbotFieldGet<uint32>(fields[3]);
                e.data = PlayerbotFieldGet<std::string>(fields[4]);

                cache.events.emplace(PlayerbotFieldGet<std::string>(fields[0]), std::move(e));
            } while (result->NextRow());
        }

        cache.loaded = true;
    }

    auto it = cache.events.find(event);
    if (it == cache.events.end())
        return nullptr;

    CachedEvent& e = it->second;

    // remove expired events
    if (e.validIn && (NowSeconds() - e.lastChangeTime) >= e.validIn && event != "specNo" && event != "specLink")
    {
        cache.events.erase(it);
        return nullptr;
    }

    return &e;
}

bool RandomPlayerbotMgr::IsSpecPvp(uint32 bot, uint8 cls)
{
    // The 1.60 built-in talent builds (Api/ClassicTalentBuilds) have no pvp variants, so no bot is
    // on a pvp spec by this definition.
    return false;
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, std::string const& event)
{
    if (CachedEvent* e = FindEvent(bot, event))
        return e->value;

    return 0;
}

std::string RandomPlayerbotMgr::GetEventData(uint32 bot, std::string const& event)
{
    if (CachedEvent* e = FindEvent(bot, event))
        return e->data;

    return "";
}

uint32 RandomPlayerbotMgr::SetEventValue(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                                         std::string const& data)
{
    std::string escapedEvent = event;
    std::string escapedData = data;
    CharacterDatabase.EscapeString(escapedEvent);
    CharacterDatabase.EscapeString(escapedData);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    trans->Append(Trinity::StringFormat(
                      "DELETE FROM playerbots_random_bots WHERE owner = 0 AND bot = {} AND event = '{}'",
                      bot, escapedEvent).c_str());

    if (value)
    {
        std::string const dataValue = data.empty() ? std::string("NULL") : ("'" + escapedData + "'");
        trans->Append(Trinity::StringFormat(
                          "INSERT INTO playerbots_random_bots (owner, bot, `time`, validIn, event, `value`, `data`) VALUES (0, {}, {}, {}, '{}', {}, {})",
                          bot, NowSeconds(), validIn, escapedEvent, value, dataValue).c_str());
    }

    CharacterDatabase.CommitTransaction(trans);

    // Update in-memory cache
    BotEventCache& cache = eventCache[bot];
    cache.loaded = true;

    if (!value)
    {
        cache.events.erase(event);
        return 0;
    }

    CachedEvent& e = cache.events[event];  // create-on-write is OK here
    e.value = value;
    e.lastChangeTime = NowSeconds();
    e.validIn = validIn;
    e.data = data;

    return value;
}

uint32 RandomPlayerbotMgr::GetValue(uint32 bot, std::string const& type) { return GetEventValue(bot, type); }

uint32 RandomPlayerbotMgr::GetValue(Player* bot, std::string const& type)
{
    return GetValue(bot->GetGUID().GetCounter(), type);
}

std::string RandomPlayerbotMgr::GetData(uint32 bot, std::string const& type) { return GetEventData(bot, type); }

void RandomPlayerbotMgr::SetValue(uint32 bot, std::string const& type, uint32 value, std::string const& data)
{
    SetEventValue(bot, type, value, DAY, data);
}

void RandomPlayerbotMgr::SetValue(Player* bot, std::string const& type, uint32 value, std::string const& data)
{
    SetValue(bot->GetGUID().GetCounter(), type, value, data);
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* /*handler*/, char const* args)
{
    if (!sPlayerbotAIConfig.Enabled)
    {
        TC_LOG_ERROR("playerbots", "Playerbots system is currently disabled!");
        return false;
    }

    if (!args || !*args)
    {
        TC_LOG_ERROR("playerbots", "Usage: rndbot stats/queststats/questdump/update/reset/init/refresh/add/remove");
        return false;
    }

    std::string const cmd = args;

    if (cmd == "reset")
    {
        CharacterDatabase.Execute("DELETE FROM playerbots_random_bots");
        sRandomPlayerbotMgr.eventCache.clear();
        TC_LOG_INFO("playerbots", "Random bots were reset for all players. Please restart the Server.");
        return true;
    }

    if (cmd == "stats")
    {
        sRandomPlayerbotMgr.PrintStats();
        // activatePrintStatsThread();
        return true;
    }

    if (cmd == "queststats")
    {
        sRandomPlayerbotMgr.PrintQuestStats();
        return true;
    }

    if (cmd == "questdump")
    {
        sRandomPlayerbotMgr.ExportQuestStatsToJson();
        return true;
    }

    if (cmd == "reload")
    {
        sPlayerbotAIConfig.Initialize();
        return true;
    }

    if (cmd == "update")
    {
        sRandomPlayerbotMgr.UpdateAIInternal(0);
        return true;
    }

    std::map<std::string, ConsoleCommandHandler> handlers;
    // handlers["initmin"] = &RandomPlayerbotMgr::RandomizeMin;
    handlers["init"] = &RandomPlayerbotMgr::RandomizeFirst;
    handlers["clear"] = &RandomPlayerbotMgr::Clear;
    handlers["levelup"] = handlers["level"] = &RandomPlayerbotMgr::IncreaseLevel;
    handlers["refresh"] = &RandomPlayerbotMgr::Refresh;
    handlers["teleport"] = &RandomPlayerbotMgr::RandomTeleportForLevel;
    // handlers["rpg"] = &RandomPlayerbotMgr::RandomTeleportForRpg;
    handlers["revive"] = &RandomPlayerbotMgr::Revive;
    handlers["grind"] = &RandomPlayerbotMgr::RandomTeleport;
    handlers["change_strategy"] = &RandomPlayerbotMgr::ChangeStrategy;

    for (std::map<std::string, ConsoleCommandHandler>::iterator j = handlers.begin(); j != handlers.end(); ++j)
    {
        std::string const prefix = j->first;
        if (cmd.find(prefix) != 0)
            continue;

        std::string const name = cmd.size() > prefix.size() + 1 ? cmd.substr(1 + prefix.size()) : "%";

        std::vector<uint32> botIds;
        for (std::vector<uint32>::iterator i = sPlayerbotAIConfig.RandomBotAccounts.begin();
             i != sPlayerbotAIConfig.RandomBotAccounts.end(); ++i)
        {
            uint32 account = *i;
            if (QueryResult results = CharacterDatabase.PQuery(
                    "SELECT guid FROM characters WHERE account = {} AND name like '{}'", account, name.c_str()))
            {
                do
                {
                    Field* fields = results->Fetch();

                    uint32 botId = PlayerbotFieldGet<uint32>(fields[0]);
                    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(botId);
                    if (!sRandomPlayerbotMgr.IsRandomBot(guid.GetCounter()))
                    {
                        continue;
                    }
                    Player* bot = ObjectAccessor::FindPlayer(guid);
                    if (!bot)
                        continue;

                    botIds.push_back(botId);
                } while (results->NextRow());
            }
        }

        if (botIds.empty())
        {
            TC_LOG_INFO("playerbots", "Nothing to do");
            return false;
        }

        uint32 processed = 0;
        for (std::vector<uint32>::iterator i = botIds.begin(); i != botIds.end(); ++i)
        {
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(*i);
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot)
                continue;

            TC_LOG_INFO("playerbots", "[{}/{}] Processing command {} for bot {}", processed++, botIds.size(), cmd.c_str(),
                     bot->GetName().c_str());

            ConsoleCommandHandler handler = j->second;
            (sRandomPlayerbotMgr.*handler)(bot);
        }

        return true;
    }

    // std::vector<std::string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args);
    // for (std::vector<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    // {
    //     TC_LOG_INFO("playerbots", "{}", i->c_str());
    // }
    return true;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, std::string const text, Player* fromPlayer, std::string channelName)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;

        if (!channelName.empty())
        {
            if (ChannelMgr* cMgr = ChannelMgr::ForTeam(bot->GetTeam()))
            {
                Channel* chn = ChannelMgr::GetChannelForPlayerByNamePart(channelName, bot);
                if (!chn)
                    continue;
            }
        }

        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
            botAI->HandleCommand(type, text, fromPlayer);
    }
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    DisablePlayerBot(player->GetGUID());

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && player == botAI->GetMaster())
        {
            botAI->SetMaster(nullptr);
            if (!bot->InBattleground())
            {
                botAI->ResetStrategies();
            }
        }
    }

    std::vector<Player*>::iterator i = std::find(players.begin(), players.end(), player);
    if (i != players.end())
        players.erase(i);
}

void RandomPlayerbotMgr::OnBotLoginInternal(Player* const bot)
{
    if (_isBotLogging)
    {
        TC_LOG_INFO("playerbots", "{}/{} Bot {} logged in", playerBots.size(),
                 sRandomPlayerbotMgr.GetMaxAllowedBotCount(), bot->GetName().c_str());

        if (playerBots.size() == sRandomPlayerbotMgr.GetMaxAllowedBotCount())
        {
            _isBotLogging = false;
        }
    }

    // Run guild recovery/assignment at login to handle empty guild tables after restart.
    if (sPlayerbotAIConfig.RandomBotGuildCount > 0)
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitGuild();
    }

    if (sPlayerbotAIConfig.RandomBotFixedLevel)
    {
        bot->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
    else
    {
        bot->RemovePlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    uint32 botsNearby = 0;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (player == bot /* || GET_PLAYERBOT_AI(player)*/)  // TEST
            continue;

        Cell playerCell(player->GetPositionX(), player->GetPositionY());
        Cell botCell(bot->GetPositionX(), bot->GetPositionY());

        // if (playerCell == botCell)
        // botsNearby++;

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        for (GroupReference const& gref : group->GetMembers())
        {
            Player* member = gref.GetSource();
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (botAI && member == player && (!botAI->GetMaster() || GET_PLAYERBOT_AI(botAI->GetMaster())))
            {
                if (!bot->InBattleground())
                {
                    botAI->SetMaster(player);
                    botAI->ResetStrategies();
                    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                        "hello", "Hello", {}));
                }

                break;
            }
        }
    }

    if (botsNearby > 100 && false)
    {
        WorldPosition botPos(player);

        // botPos.GetReachableRandomPointOnGround(player, sPlayerbotAIConfig.ReactDistance * 2, true);

        // player->TeleportTo(botPos);
        // player->Relocate(botPos.coord_x, botPos.coord_y, botPos.coord_z, botPos.orientation);

        if (!player->GetFactionTemplateEntry())
        {
            botPos.GetReachableRandomPointOnGround(player, sPlayerbotAIConfig.ReactDistance * 2, true);
        }
        else
        {
            std::vector<TravelDestination*> dests = TravelMgr::instance().getRpgTravelDestinations(player, true, true, 200000.0f);

            do
            {
                RpgTravelDestination* dest = (RpgTravelDestination*)dests[urand(0, dests.size() - 1)];
                CreatureTemplate const* cInfo = dest->GetCreatureTemplate();
                if (!cInfo)
                    continue;

                FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->faction);
                // 12.x compares reaction through the two faction templates.
                bool const factionFriendly = player->GetFactionTemplateEntry()->IsFriendlyTo(factionEntry);

                if (factionFriendly && dest->nearestPoint(&botPos)->GetMapId() == player->GetMapId())
                {
                    botPos = *dest->nearestPoint(&botPos);
                    break;
                }
            } while (true);
        }

        player->RemoveAurasWithInterruptFlags(SpellAuraInterruptFlags::LeaveWorld | SpellAuraInterruptFlags::EnterWorld);
        player->TeleportTo(botPos);

        // player->Relocate(botPos.getX(), botPos.getY(), botPos.getZ(), botPos.getO());
    }

    if (IsRandomBot(player))
    {
        // ObjectGuid::LowType guid = player->GetGUID().GetCounter(); //not used, conditional could be rewritten for
        // simplicity. line marked for removal.
        player->SetPvP(sWorld->IsPvPRealm());
    }
    else
    {
        players.push_back(player);
        TC_LOG_DEBUG("playerbots", "Including non-random bot player {} into random bot update", player->GetName().c_str());
    }
}

void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)
{
    SetEventValue(bot, "add", 0, 0);
    currentBots.erase(bot);
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return nullptr;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

void RandomPlayerbotMgr::PrintStats()
{
    printStatsTimer = time(nullptr);
    TC_LOG_INFO("playerbots", "Random Bots Stats: {} online", playerBots.size());

    std::map<uint8, uint32> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    std::map<uint8, uint32> perRace;
    std::map<uint8, uint32> perClass;

    std::map<uint8, uint32> lvlPerRace;
    std::map<uint8, uint32> lvlPerClass;
    for (uint8 race = RACE_HUMAN; race < sChrRacesStore.GetNumRows(); ++race)
    {
        perRace[race] = 0;
        lvlPerRace[race] = 0;
    }

    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
        lvlPerClass[cls] = 0;
    }

    uint32 dps = 0;
    uint32 heal = 0;
    uint32 tank = 0;
    uint32 active = 0;
/*    uint32 update = 0;
    uint32 randomize = 0;
    uint32 teleport = 0;
    uint32 changeStrategy = 0;*/
    uint32 dead = 0;
    uint32 combat = 0;
    // uint32 revive = 0; //not used, line marked for removal.
    uint32 inFlight = 0;
    uint32 moving = 0;
    uint32 mounted = 0;
    uint32 inBg = 0;
    uint32 rest = 0;
    uint32 engine_noncombat = 0;
    uint32 engine_combat = 0;
    uint32 engine_dead = 0;
    std::unordered_map<NewRpgStatus, int> rpgStatusCount;
    // static NewRpgStatistic rpgStasticTotal;
    std::unordered_map<uint32, int> zoneCount;
    uint8 maxBotLevel = 0;
    for (PlayerBotMap::iterator i = playerBots.begin(); i != playerBots.end(); ++i)
    {
        Player* bot = i->second;
        if (IsAlliance(bot->GetRace()))
            ++alliance[bot->GetLevel()];
        else
            ++horde[bot->GetLevel()];
        maxBotLevel = std::max(maxBotLevel, bot->GetLevel());

        ++perRace[bot->GetRace()];
        ++perClass[bot->GetClass()];

        lvlPerClass[bot->GetClass()] += bot->GetLevel();
        lvlPerRace[bot->GetRace()] += bot->GetLevel();

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
        {
            TC_LOG_ERROR("playerbots", "Player/Bot {} is registered in sRandomPlayerbotMgr playerBots and has no bot AI!", bot->GetName().c_str());
            continue;
        }

        if (botAI->IsActivityAllowedCached())
            ++active;
        /* TODO: Review statistics on rpg merge
        if (botAI->GetAiObjectContext()->GetValue<bool>("random bot update")->Get())
            ++update;

        uint32 botId = bot->GetGUID().GetCounter();
        if (!GetEventValue(botId, "randomize"))
            ++randomize;

        if (!GetEventValue(botId, "teleport"))
            ++teleport;

        if (!GetEventValue(botId, "change_strategy"))
            ++changeStrategy;
        */
        if (bot->isDead())
        {
            ++dead;
            // if (!GetEventValue(botId, "dead"))
            //++revive;
        }
        if (bot->IsInCombat())
            ++combat;

        if (bot->isMoving())
            ++moving;

        if (bot->IsInFlight())
            ++inFlight;

        if (bot->IsMounted())
            ++mounted;

        if (bot->InBattleground() || bot->InArena())
            ++inBg;

        if (bot->HasPlayerFlag(PLAYER_FLAGS_RESTING))
            ++rest;

        if (botAI->GetState() == BOT_STATE_NON_COMBAT)
            ++engine_noncombat;

        else if (botAI->GetState() == BOT_STATE_COMBAT)
            ++engine_combat;

        else
            ++engine_dead;

        if (PlayerbotAI::IsHeal(bot, false))
            ++heal;

        else if (PlayerbotAI::IsTank(bot, false))
            ++tank;

        else
            ++dps;

        zoneCount[bot->GetZoneId()]++;

        if (sPlayerbotAIConfig.EnableNewRpgStrategy)
        {
            rpgStatusCount[botAI->rpgInfo.GetStatus()]++;
            rpgStasticTotal += botAI->rpgStatistic;
            botAI->rpgStatistic = NewRpgStatistic();
        }
    }

    TC_LOG_INFO("playerbots", "Bots level:");
    // uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32_t currentAlliance = 0, currentHorde = 0;
    uint32_t step = std::max(1, static_cast<int>((maxBotLevel + 4) / 8));
    uint32_t from = 1;

    for (uint8 i = 1; i <= maxBotLevel; ++i)
    {
        currentAlliance += alliance[i];
        currentHorde += horde[i];

        if (((i + 1) % step == 0) || i == maxBotLevel)
        {
            if (currentAlliance || currentHorde)
                TC_LOG_INFO("playerbots", "    {}..{}: {} alliance, {} horde", from, i, currentAlliance, currentHorde);
            currentAlliance = 0;
            currentHorde = 0;
            from = i + 1;
        }
    }

    TC_LOG_INFO("playerbots", "Bots race:");
    for (uint8 race = RACE_HUMAN; race < sChrRacesStore.GetNumRows(); ++race)
    {
        if (perRace[race])
        {
            uint32 lvl = lvlPerRace[race] * 10 / perRace[race];
            float flvl = lvl / 10.0f;
            TC_LOG_INFO("playerbots", "    {}: {}, avg lvl: {}", ChatHelper::FormatRace(race).c_str(), perRace[race],
                     flvl);
        }
    }

    TC_LOG_INFO("playerbots", "Bots class:");
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
        {
            uint32 lvl = lvlPerClass[cls] * 10 / perClass[cls];
            float flvl = lvl / 10.0f;
            TC_LOG_INFO("playerbots", "    {}: {}, avg lvl: {}", ChatHelper::FormatClass(cls).c_str(), perClass[cls],
                     flvl);
        }
    }

    TC_LOG_INFO("playerbots", "Bots role:");
    TC_LOG_INFO("playerbots", "    tank: {}, heal: {}, dps: {}", tank, heal, dps);

    TC_LOG_INFO("playerbots", "Bots status:");
    TC_LOG_INFO("playerbots", "    Active: {}", active);
    TC_LOG_INFO("playerbots", "    Moving: {}", moving);

    // TC_LOG_INFO("playerbots", "Bots to:");
    // TC_LOG_INFO("playerbots", "    update: {}", update);
    // TC_LOG_INFO("playerbots", "    randomize: {}", randomize);
    // TC_LOG_INFO("playerbots", "    teleport: {}", teleport);
    // TC_LOG_INFO("playerbots", "    change_strategy: {}", changeStrategy);
    // TC_LOG_INFO("playerbots", "    revive: {}", revive);

    TC_LOG_INFO("playerbots", "    In flight: {}", inFlight);
    TC_LOG_INFO("playerbots", "    On mount: {}", mounted);
    TC_LOG_INFO("playerbots", "    In combat: {}", combat);
    TC_LOG_INFO("playerbots", "    In BG: {}", inBg);
    TC_LOG_INFO("playerbots", "    In Rest: {}", rest);
    TC_LOG_INFO("playerbots", "    Dead: {}", dead);

    if (sPlayerbotAIConfig.EnableNewRpgStrategy)
    {
        TC_LOG_INFO("playerbots", "Bots rpg status:");
        TC_LOG_INFO("playerbots",
                 "    Idle: {}, Rest: {}, GoGrind: {}, GoCamp: {}, MoveRandom: {}, MoveNpc: {}, DoQuest: {}, "
                 "TravelFlight: {}, OutdoorPvP: {}, DoGather: {}",
                 rpgStatusCount[RPG_IDLE], rpgStatusCount[RPG_REST], rpgStatusCount[RPG_GO_GRIND],
                 rpgStatusCount[RPG_GO_CAMP], rpgStatusCount[RPG_WANDER_RANDOM], rpgStatusCount[RPG_WANDER_NPC],
                 rpgStatusCount[RPG_DO_QUEST], rpgStatusCount[RPG_TRAVEL_FLIGHT], rpgStatusCount[RPG_OUTDOOR_PVP],
                 rpgStatusCount[RPG_DO_GATHER]);

        PrintQuestTotals(rpgStasticTotal);
    }

    TC_LOG_INFO("playerbots", "Bots engine:", dead);
    TC_LOG_INFO("playerbots", "    Non-combat: {}, Combat: {}, Dead: {}", engine_noncombat, engine_combat, engine_dead);
}

NewRpgStatistic RandomPlayerbotMgr::CollectQuestStats()
{
    // Merge each bot's pending statistics on top of the last aggregation, so the report is fresh
    // even when PrintStats has not run yet.
    NewRpgStatistic total = rpgStasticTotal;
    for (PlayerBotMap::iterator i = playerBots.begin(); i != playerBots.end(); ++i)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(i->second);
        if (!botAI)
            continue;

        total += botAI->rpgStatistic;
    }
    return total;
}

void RandomPlayerbotMgr::PrintQuestTotals(NewRpgStatistic const& stats)
{
    TC_LOG_INFO("playerbots", "Bots total quests:");
    TC_LOG_INFO("playerbots", "    Accepted: {}, Completed: {}, Rewarded: {}, Dropped: {}, Abandoned: {}",
             stats.questAccepted, stats.questCompleted, stats.questRewarded, stats.questDropped,
             stats.questAbandoned);

    if (stats.questAccepted > 0)
    {
        TC_LOG_INFO("playerbots", "    Completed: {:.2f}%, Rewarded: {:.2f}%, Dropped: {:.2f}%, Abandoned: {:.2f}%",
                 stats.questCompleted * 100.0f / stats.questAccepted,
                 stats.questRewarded * 100.0f / stats.questAccepted,
                 stats.questDropped * 100.0f / stats.questAccepted,
                 stats.questAbandoned * 100.0f / stats.questAccepted);
    }
}

void RandomPlayerbotMgr::PrintQuestStats()
{
    if (!sPlayerbotAIConfig.EnableNewRpgStrategy)
    {
        TC_LOG_INFO("playerbots", "New RPG strategy is not enabled. Quest stats not available.");
        return;
    }

    TC_LOG_INFO("playerbots", "=== QUEST STATISTICS REPORT ===");
    NewRpgStatistic const total = CollectQuestStats();
    PrintQuestTotals(total);
    TC_LOG_INFO("playerbots", "");

    if (!sPlayerbotAIConfig.CollectDetailedQuestStats)
    {
        TC_LOG_INFO("playerbots", "Detailed quest stats are disabled. Set Playerbots.CollectDetailedQuestStats = 1.");
        return;
    }

    constexpr size_t dropReasonCount = static_cast<size_t>(QuestDropReason::Count);
    constexpr size_t abandonReasonCount = static_cast<size_t>(QuestAbandonReason::Count);
    std::array<uint32, dropReasonCount> dropTotals{};
    std::array<uint32, abandonReasonCount> abandonTotals{};
    for (auto const& [questId, counters] : total.byQuest)
    {
        for (size_t i = 0; i < dropReasonCount; ++i)
            dropTotals[i] += counters.dropped[i];
        for (size_t i = 0; i < abandonReasonCount; ++i)
            abandonTotals[i] += counters.abandoned[i];
    }

    uint32 dropTotal = 0;
    for (uint32 count : dropTotals)
        dropTotal += count;
    if (dropTotal)
    {
        TC_LOG_INFO("playerbots", "Quest drop reasons:");
        for (size_t i = 0; i < dropReasonCount; ++i)
            if (dropTotals[i])
                TC_LOG_INFO("playerbots", "    {}: {}", QuestDropReasonName(static_cast<QuestDropReason>(i)),
                         dropTotals[i]);
        TC_LOG_INFO("playerbots", "");
    }

    uint32 abandonTotal = 0;
    for (uint32 count : abandonTotals)
        abandonTotal += count;
    if (abandonTotal)
    {
        TC_LOG_INFO("playerbots", "Quest abandon reasons:");
        for (size_t i = 0; i < abandonReasonCount; ++i)
            if (abandonTotals[i])
                TC_LOG_INFO("playerbots", "    {}: {}", QuestAbandonReasonName(static_cast<QuestAbandonReason>(i)),
                         abandonTotals[i]);
        TC_LOG_INFO("playerbots", "");
    }

    struct ProblemQuest
    {
        uint32 questId{0};
        uint32 problems{0};
        std::string reasons;
    };

    std::vector<ProblemQuest> problems;
    problems.reserve(total.byQuest.size());
    for (auto const& [questId, counters] : total.byQuest)
    {
        uint32 problemCount = 0;
        std::string reasons;
        for (size_t i = 0; i < dropReasonCount; ++i)
        {
            if (!counters.dropped[i])
                continue;
            problemCount += counters.dropped[i];
            if (!reasons.empty())
                reasons += ", ";
            reasons += std::string(QuestDropReasonName(static_cast<QuestDropReason>(i))) + " x" +
                       std::to_string(counters.dropped[i]);
        }
        for (size_t i = 0; i < abandonReasonCount; ++i)
        {
            if (!counters.abandoned[i])
                continue;
            problemCount += counters.abandoned[i];
            if (!reasons.empty())
                reasons += ", ";
            reasons += std::string(QuestAbandonReasonName(static_cast<QuestAbandonReason>(i))) + " x" +
                       std::to_string(counters.abandoned[i]);
        }

        if (problemCount)
            problems.push_back({questId, problemCount, std::move(reasons)});
    }

    if (!problems.empty())
    {
        std::sort(problems.begin(), problems.end(),
                  [](ProblemQuest const& a, ProblemQuest const& b) { return a.problems > b.problems; });

        TC_LOG_INFO("playerbots", "Top quest problems (dropped + abandoned):");
        for (size_t i = 0; i < problems.size() && i < 10; ++i)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(problems[i].questId);
            TC_LOG_INFO("playerbots", "    [{}] {} x{} ({})", problems[i].questId,
                     quest ? quest->GetLogTitle() : "Unknown Quest", problems[i].problems, problems[i].reasons);
        }
    }

    TC_LOG_INFO("playerbots", "=== END QUEST STATISTICS REPORT ===");
}

void RandomPlayerbotMgr::ExportQuestStatsToJson()
{
    if (!sPlayerbotAIConfig.EnableNewRpgStrategy)
    {
        TC_LOG_ERROR("playerbots", "New RPG strategy is not enabled. Quest stats not available.");
        return;
    }

    if (!sPlayerbotAIConfig.CollectDetailedQuestStats)
    {
        TC_LOG_ERROR("playerbots", "Detailed quest stats are disabled. Set Playerbots.CollectDetailedQuestStats = 1.");
        return;
    }

    NewRpgStatistic const total = CollectQuestStats();

    auto dropName = [](size_t i) { return QuestDropReasonName(static_cast<QuestDropReason>(i)); };
    auto abandonName = [](size_t i) { return QuestAbandonReasonName(static_cast<QuestAbandonReason>(i)); };

    auto reasonsToJson = [](auto const& counts, auto nameFor)
    {
        std::ostringstream out;
        out << "{";
        bool first = true;
        for (size_t i = 0; i < counts.size(); ++i)
        {
            if (!counts[i])
                continue;
            if (!first)
                out << ", ";
            out << "\"" << JsonEscape(nameFor(i)) << "\": " << counts[i];
            first = false;
        }
        out << "}";
        return out.str();
    };

    struct QuestRow
    {
        uint32 questId{0};
        QuestCounters counters;
        uint32 Total() const
        {
            uint32 count = counters.accepted + counters.completed + counters.rewarded;
            for (uint32 value : counters.dropped)
                count += value;
            for (uint32 value : counters.abandoned)
                count += value;
            return count;
        }
    };

    std::vector<QuestRow> rows;
    rows.reserve(total.byQuest.size());
    for (auto const& [questId, counters] : total.byQuest)
        rows.push_back({questId, counters});

    std::sort(rows.begin(), rows.end(), [](QuestRow const& a, QuestRow const& b) { return a.Total() > b.Total(); });

    constexpr size_t dropReasonCount = static_cast<size_t>(QuestDropReason::Count);
    constexpr size_t abandonReasonCount = static_cast<size_t>(QuestAbandonReason::Count);
    std::array<uint32, dropReasonCount> dropTotals{};
    std::array<uint32, abandonReasonCount> abandonTotals{};
    for (QuestRow const& row : rows)
    {
        for (size_t i = 0; i < dropReasonCount; ++i)
            dropTotals[i] += row.counters.dropped[i];
        for (size_t i = 0; i < abandonReasonCount; ++i)
            abandonTotals[i] += row.counters.abandoned[i];
    }

    char stamp[32] = "";
    std::time_t const now = std::time(nullptr);
    if (std::tm const* utc = std::gmtime(&now))
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", utc);

    std::ostringstream out;
    out << "{\n";
    out << "  \"generatedAt\": " << static_cast<uint64>(now) << ",\n";
    out << "  \"generatedAtUtc\": \"" << stamp << "\",\n";
    out << "  \"totals\": {\n";
    out << "    \"accepted\": " << total.questAccepted << ",\n";
    out << "    \"completed\": " << total.questCompleted << ",\n";
    out << "    \"rewarded\": " << total.questRewarded << ",\n";
    out << "    \"dropped\": " << total.questDropped << ",\n";
    out << "    \"abandoned\": " << total.questAbandoned << "\n";
    out << "  },\n";
    out << "  \"dropReasons\": " << reasonsToJson(dropTotals, dropName) << ",\n";
    out << "  \"abandonReasons\": " << reasonsToJson(abandonTotals, abandonName) << ",\n";
    out << "  \"quests\": [\n";

    for (size_t i = 0; i < rows.size(); ++i)
    {
        QuestRow const& row = rows[i];
        Quest const* quest = sObjectMgr->GetQuestTemplate(row.questId);
        uint32 dropped = 0;
        uint32 abandoned = 0;
        for (uint32 value : row.counters.dropped)
            dropped += value;
        for (uint32 value : row.counters.abandoned)
            abandoned += value;

        out << "    {\n";
        out << "      \"id\": " << row.questId << ",\n";
        out << "      \"name\": \"" << JsonEscape(quest ? quest->GetLogTitle() : "Unknown Quest") << "\",\n";
        out << "      \"accepted\": " << row.counters.accepted << ",\n";
        out << "      \"completed\": " << row.counters.completed << ",\n";
        out << "      \"rewarded\": " << row.counters.rewarded << ",\n";
        out << "      \"dropped\": " << dropped << ",\n";
        out << "      \"abandoned\": " << abandoned << ",\n";
        out << "      \"dropReasons\": " << reasonsToJson(row.counters.dropped, dropName) << ",\n";
        out << "      \"abandonReasons\": " << reasonsToJson(row.counters.abandoned, abandonName) << "\n";
        out << "    }" << (i + 1 < rows.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    WriteJsonFile(LogsJsonPath("playerbots_quest_stats.json"), out.str(), "Quest stats");
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "buymultiplier");
    if (!value)
    {
        value = urand(50, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.MinRandomBotsPriceChangeInterval,
                               sPlayerbotAIConfig.MaxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.MinRandomBotsPriceChangeInterval,
                               sPlayerbotAIConfig.MaxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

void RandomPlayerbotMgr::AddTradeDiscount(Player* bot, Player* master, int32 value)
{
    if (!master)
        return;

    uint32 discount = GetTradeDiscount(bot, master);
    int32 result = (int32)discount + value;
    discount = (result < 0 ? 0 : result);

    SetTradeDiscount(bot, master, discount);
}

void RandomPlayerbotMgr::SetTradeDiscount(Player* bot, Player* master, uint32 value)
{
    if (!master)
        return;

    uint32 botId = bot->GetGUID().GetCounter();
    uint32 masterId = master->GetGUID().GetCounter();

    std::ostringstream name;
    name << "trade_discount_" << masterId;
    SetEventValue(botId, name.str(), value, DAY);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot, Player* master)
{
    if (!master)
        return 0;

    uint32 botId = bot->GetGUID().GetCounter();
    uint32 masterId = master->GetGUID().GetCounter();

    std::ostringstream name;
    name << "trade_discount_" << masterId;
    return GetEventValue(botId, name.str());
}

std::string const RandomPlayerbotMgr::HandleRemoteCommand(std::string const request)
{
    std::string::const_iterator pos = std::find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        std::ostringstream out;
        out << "invalid request: " << request;
        return out.str();
    }

    std::string const command = std::string(request.begin(), pos);
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(atoi(std::string(pos + 1, request.end()).c_str()));
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return "invalid guid";

    return botAI->HandleRemoteCommand(command);
}

void RandomPlayerbotMgr::ChangeStrategy(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    if (frand(0.f, 100.f) > sPlayerbotAIConfig.RandomBotRpgChance)
    {
        TC_LOG_INFO("playerbots", "Bot #{} <{}>: sent to grind spot", bot, player->GetName().c_str());
        ScheduleTeleport(bot, 30);
    }
    else
    {
        TC_LOG_INFO("playerbots", "Changing strategy for bot #{} <{}> to RPG", bot, player->GetName().c_str());
        TC_LOG_INFO("playerbots", "Bot #{} <{}>: sent to inn", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
        SetEventValue(bot, "teleport", 1, sPlayerbotAIConfig.MaxRandomBotTeleportInterval);
    }

    ScheduleChangeStrategy(bot);
}

void RandomPlayerbotMgr::ChangeStrategyOnce(Player* player)
{
    uint32 bot = player->GetGUID().GetCounter();

    if (frand(0.f, 100.f) > sPlayerbotAIConfig.RandomBotRpgChance)  // select grind / pvp
    {
        TC_LOG_INFO("playerbots", "Bot #{} <{}>: sent to grind spot", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
        Refresh(player);
    }
    else
    {
        TC_LOG_INFO("playerbots", "Bot #{} <{}>: sent to inn", bot, player->GetName().c_str());
        RandomTeleportForLevel(player);
    }
}

void RandomPlayerbotMgr::RandomTeleportForRpg(Player* bot)
{
    uint32 race = bot->GetRace();
    uint32 level = bot->GetLevel();
    TC_LOG_DEBUG("playerbots", "Random teleporting bot {} for RPG ({} locations available)", bot->GetName().c_str(),
              rpgLocsCacheLevel[race].size());
    RandomTeleport(bot, rpgLocsCacheLevel[race][level], true);
}

void RandomPlayerbotMgr::Remove(Player* bot)
{
    ObjectGuid owner = bot->GetGUID();

    CharacterDatabase.PExecute(
        "DELETE FROM playerbots_random_bots WHERE owner = 0 AND bot = {}", owner.GetCounter());

    uint32 botId = owner.GetCounter();
    eventCache.erase(botId);

    LogoutPlayerBot(owner);
}

CreatureData const* RandomPlayerbotMgr::GetCreatureDataByEntry(uint32 entry)
{
    if (entry != 0)
    {
        for (auto const& itr : sObjectMgr->GetAllCreatureData())
            if (itr.second.id == entry)
                return &itr.second;
    }

    return nullptr;
}
