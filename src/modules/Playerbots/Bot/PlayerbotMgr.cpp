/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ClassicSpec.h"
#include "ClassicTalentsApply.h"
#include "ClassicTalentBuilds.h"
#include "PlayerbotMgr.h"
#include "AccountMgr.h"
#include "ChannelPackets.h"
#include "Channel.h"
#include "LoginQueryHolder.h"
#include "FieldHelper.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "BotTickEvent.h"
#include "BroadcastHelper.h"
#include "ChannelMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Common.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "Define.h"
#include "Group.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotOperations.h"
#include "PlayerbotRepository.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include "World.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <openssl/sha.h>
#include <string>
#include <vector>
#include <unordered_set>

class BotInitGuard
{
public:
    BotInitGuard(ObjectGuid guid) : guid(guid), active(false)
    {
        if (!botsBeingInitialized.contains(guid))
        {
            botsBeingInitialized.insert(guid);
            active = true;
        }
    }

    ~BotInitGuard()
    {
        if (active)
            botsBeingInitialized.erase(guid);
    }

    bool IsLocked() const { return !active; }

private:
    ObjectGuid guid;
    bool active;
    static std::unordered_set<ObjectGuid> botsBeingInitialized;
};

std::unordered_set<ObjectGuid> BotInitGuard::botsBeingInitialized;
std::unordered_map<ObjectGuid, PendingBotLogin> PlayerbotHolder::botLoading;

PlayerbotHolder::PlayerbotHolder() : PlayerbotAIBase(false) {}
class PlayerbotLoginQueryHolder : public LoginQueryHolder
{
private:
    uint32 masterAccountId;
public:
    PlayerbotLoginQueryHolder(uint32 masterAccount, uint32 accountId, ObjectGuid guid)
        : LoginQueryHolder(accountId, guid), masterAccountId(masterAccount)
    {
    }

    uint32 GetMasterAccountId() const { return masterAccountId; }
};

void PlayerbotHolder::AddPlayerBot(ObjectGuid playerGuid, uint32 masterAccountId)
{
    if (botLoading.find(playerGuid) != botLoading.end())
        return;

    // has bot already been added?
    Player* bot = ObjectAccessor::FindConnectedPlayer(playerGuid);
    if (bot && bot->IsInWorld())
        return;

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
        return;

    WorldSession* masterSession = masterAccountId ? sWorld->FindSession(masterAccountId) : nullptr;
    Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;

    bool isRndbot = !masterAccountId;
    bool sameAccount = sPlayerbotAIConfig.AllowAccountBots && accountId == masterAccountId;
    Guild* guild = masterPlayer ? sGuildMgr->GetGuildById(masterPlayer->GetGuildId()) : nullptr;
    bool sameGuild = sPlayerbotAIConfig.AllowGuildBots && guild && guild->GetMembers().find(playerGuid) != guild->GetMembers().end();
    bool addClassBot = sRandomPlayerbotMgr.IsAddclassBot(playerGuid.GetCounter());
    bool linkedAccount = sPlayerbotAIConfig.AllowTrustedAccountBots && IsAccountLinked(accountId, masterAccountId);

    bool allowed = true;
    std::ostringstream out;
    std::string botName;
    sCharacterCache->GetCharacterNameByGuid(playerGuid, botName);
    if (!isRndbot && !sameAccount && !sameGuild && !addClassBot && !linkedAccount)
    {
        allowed = false;
        out << "Failure: You are not allowed to control bot " << botName.c_str();
    }
    if (masterAccountId && masterPlayer)
    {
        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(masterPlayer);
        if (!mgr)
        {
            TC_LOG_DEBUG("playerbots", "PlayerbotMgr not found for master player with GUID: {}", masterPlayer->GetGUID().ToString());
            return;
        }
        uint32 loadingForMaster = 0;
        for (auto const& [guid, pending] : botLoading)
        {
            if (pending.masterAccountId == masterAccountId)
                ++loadingForMaster;
        }
        uint32 count = mgr->GetPlayerbotsCount() + loadingForMaster;
        if (count >= uint32(PlayerbotAIConfig::Instance().MaxAddedBots))
        {
            allowed = false;
            out << "Failure: You have added too many bots (more than " << sPlayerbotAIConfig.MaxAddedBots << ")";
        }
    }
    if (!allowed)
    {
        if (masterSession)
        {
            ChatHandler ch(masterSession);
            ch.SendSysMessage(out.str());
        }
        return;
    }
    std::shared_ptr<PlayerbotLoginQueryHolder> holder =
        std::make_shared<PlayerbotLoginQueryHolder>(masterAccountId, accountId, playerGuid);
    if (!holder->Initialize())
    {
        return;
    }

    // 12.x session constructor: bnet id/email, socket, security, expansion, mute time, os name,
    // timezone, build, client build variant, locale, recruiter flags and the headless bot flag.
    WorldSession* botSession = new WorldSession(accountId, std::string(), 0, std::string(), nullptr, SEC_PLAYER,
                                                uint8(EXPANSION_CLASSIC), time_t(0), std::string(), Minutes(0), 0,
                                                ClientBuild::VariantId{ClientBuild::Platform::Win_x64, ClientBuild::Arch::x64,
                                                                       ClientBuild::Type::Retail},
                                                sWorld->GetDefaultDbcLocale(), 0, false, true);

    botLoading.emplace(playerGuid, PendingBotLogin{masterAccountId, botSession, false});

    botSession->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder))
        .AfterComplete(
            [botSession](SQLQueryHolderBase const& queryHolder)
            {
                PlayerbotLoginQueryHolder const& holder = static_cast<PlayerbotLoginQueryHolder const&>(queryHolder);
                uint32 masterAccountId = holder.GetMasterAccountId();

                if (masterAccountId)
                {
                    // verify and find current world session of master
                    WorldSession* masterSession = sWorld->FindSession(masterAccountId);
                    Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;

                    if (masterPlayer)
                    {
                        if (PlayerbotHolder* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(masterPlayer))
                        {
                            mgr->HandlePlayerBotLoginCallback(holder, botSession);
                            return;
                        }

                        // Master lost its manager while the holder was in flight
                        AbandonPendingLogin(holder.GetGuid());
                        return;
                    }
                }

                RandomPlayerbotMgr::instance().HandlePlayerBotLoginCallback(holder, botSession);
            });
}

void PlayerbotHolder::AbandonPendingLogin(ObjectGuid guid)
{
    auto itr = botLoading.find(guid);
    if (itr != botLoading.end())
        itr->second.failed = true;
}

void PlayerbotHolder::UpdatePendingLogins()
{
    if (botLoading.empty())
        return;

    // Callbacks erase or flag their own entry, so iterate over a snapshot of the keys
    std::vector<ObjectGuid> guids;
    guids.reserve(botLoading.size());
    for (auto const& [guid, pending] : botLoading)
        guids.push_back(guid);

    for (ObjectGuid const& guid : guids)
    {
        auto itr = botLoading.find(guid);
        if (itr == botLoading.end())
            continue;

        WorldSession* session = itr->second.session;
        if (!itr->second.failed)
            session->ProcessQueryCallbacks();  // may complete the login (entry erased) or flag it failed

        itr = botLoading.find(guid);
        if (itr == botLoading.end() || !itr->second.failed)
            continue;

        // Safe here: the processor has returned, nothing of the session is on the stack
        session->LogoutPlayer(true);
        delete session;
        botLoading.erase(itr);
    }
}

void PlayerbotHolder::ClearPendingLogins()
{
    for (auto const& [guid, pending] : botLoading)
    {
        pending.session->LogoutPlayer(true);
        delete pending.session;
    }
    botLoading.clear();
}

bool PlayerbotHolder::IsAccountLinked(uint32 accountId, uint32 linkedAccountId)
{
    return CharacterDatabase.PQuery(
               "SELECT 1 FROM playerbots_account_links WHERE account_id = {} AND linked_account_id = {}",
               accountId, linkedAccountId) != nullptr;
}

void PlayerbotHolder::HandlePlayerBotLoginCallback(PlayerbotLoginQueryHolder const& holder, WorldSession* botSession)
{
    uint32 botAccountId = holder.GetAccountId();

    botSession->HandlePlayerLogin(holder);  // will delete lqh

    Player* bot = botSession->GetPlayer();
    if (!bot)
    {
        // Debug log
        TC_LOG_DEBUG("mod-playerbots", "Bot player could not be loaded for account ID: {}", botAccountId);
        // This runs inside botSession's own callback processor, so the teardown is deferred to UpdatePendingLogins()
        AbandonPendingLogin(holder.GetGuid());
        return;
    }

    uint32 masterAccountId = holder.GetMasterAccountId();
    WorldSession* masterSession = masterAccountId ? sWorld->FindSession(masterAccountId) : nullptr;

    // Check if masterSession->GetPlayer() is valid
    Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;
    if (masterSession && !masterPlayer)
    {
        TC_LOG_DEBUG("mod-playerbots", "Master session found but no player is associated for master account ID: {}",
                  masterAccountId);
    }

    sRandomPlayerbotMgr.OnPlayerLogin(bot);
    auto op = std::make_unique<OnBotLoginOperation>(bot->GetGUID(), masterAccountId);
    PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op));

    PlayerbotHolder::botLoading.erase(holder.GetGuid());
}

void PlayerbotHolder::UpdateSessions()
{
    for (PlayerBotMap::const_iterator itr = GetPlayerBotsBegin(); itr != GetPlayerBotsEnd(); ++itr)
    {
        Player* const bot = itr->second;
        // Nothing else drives a headless session's async query, transaction and holder callbacks
        bot->GetSession()->ProcessQueryCallbacks();
        if (bot->IsBeingTeleported())
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (botAI)
            {
                botAI->HandleTeleportAck();
            }
        }
    }
}

void PlayerbotHolder::LogoutAllBots()
{
    /*
    while (true)
    {
        PlayerBotMap::const_iterator itr = GetPlayerBotsBegin();
        if (itr == GetPlayerBotsEnd())
            break;

        Player* bot= itr->second;
        if (!IsSelfBot(bot))
            LogoutPlayerBot(bot->GetGUID());
    }
    */

    PlayerBotMap bots = playerBots;
    for (auto& itr : bots)
    {
        Player* bot = itr.second;
        if (!bot)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || IsSelfBot(bot))
            continue;

        LogoutPlayerBot(bot->GetGUID());
    }
}

void PlayerbotMgr::CancelLogout()
{
    Player* master = GetMaster();
    if (!master)
        return;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || IsSelfBot(bot))
            continue;

        if (bot->GetSession()->isLogingOut())
        {
            botAI->RunOnWorldThread("logout cancel", [](Player* bot) {
                WorldPacket raw(CMSG_LOGOUT_CANCEL);
                WorldPackets::Character::LogoutCancel data(std::move(raw));
                bot->GetSession()->HandleLogoutCancelOpcode(data);
            });
            botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "logout_cancel", "Logout cancelled!", {}));
        }
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || IsSelfBot(bot))
            continue;

        if (botAI->GetMaster() != master)
            continue;

        if (bot->GetSession()->isLogingOut())
        {
            botAI->RunOnWorldThread("logout cancel", [](Player* bot) {
                WorldPacket raw(CMSG_LOGOUT_CANCEL);
                WorldPackets::Character::LogoutCancel data(std::move(raw));
                bot->GetSession()->HandleLogoutCancelOpcode(data);
            });
        }
    }
}

void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)
{
    if (Player* bot = GetPlayerBot(guid))
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return;

        // A bot can ask to log out from inside its own update tick (logout timer, master's command).
        // Logging it out here would delete the player object while its update is still on the stack,
        // so hand the request to the world tick instead.
        if (BotTickEvent::IsTickingPlayer(guid))
        {
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::make_unique<BotLogoutOperation>(guid));
            return;
        }

        // Queue group cleanup operation for world thread
        auto cleanupOp = std::make_unique<BotLogoutGroupCleanupOperation>(guid);
        PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(cleanupOp));

        TC_LOG_DEBUG("playerbots", "Bot {} logging out", bot->GetName().c_str());

        // Remove taxi cheat flag on alts.
        if (!sRandomPlayerbotMgr.IsRandomBot(bot) && bot->isTaxiCheater())
            bot->SetTaxiCheater(false);

        bot->SaveToDB(false);

        WorldSession* botWorldSessionPtr = bot->GetSession();
        [[maybe_unused]] WorldSession* masterWorldSessionPtr = nullptr;     // Remove [[maybe_unused]] tag if timed logout implemented.

        if (botWorldSessionPtr->isLogingOut())
            return;

        Player* master = botAI->GetMaster();
        if (master)
            masterWorldSessionPtr = master->GetSession();

        // TODO: Review whether or not to implement timed logout.
        // Unused block. Useful only for timed logout.
/*
        // check for instant logout
        bool logout = botWorldSessionPtr->ShouldLogOut(time(nullptr));

        if (!masterWorldSessionPtr.IsEmpty() && masterWorldSessionPtr->ShouldLogOut(time(nullptr)))
            logout = true;

        if (masterWorldSessionPtr && !masterWorldSessionPtr->GetPlayer())
            logout = true;

        if (bot->HasPlayerFlag(PLAYER_FLAGS_RESTING) || bot->HasUnitState(UNIT_STATE_IN_FLIGHT) ||
            botWorldSessionPtr->GetSecurity() >= (AccountTypes)sWorld->getIntConfig(CONFIG_INSTANT_LOGOUT))
            logout = true;

        if (master &&
            (master->HasPlayerFlag(PLAYER_FLAGS_RESTING) || master->HasUnitState(UNIT_STATE_IN_FLIGHT) ||
             (masterWorldSessionPtr &&
              masterWorldSessionPtr->GetSecurity() >= (AccountTypes)sWorld->getIntConfig(CONFIG_INSTANT_LOGOUT))))
            logout = true;
*/
        // Instant logout (the only option right now)
        {
            std::string message = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "goodbye", "Goodbye!", {});
            botAI->TellMaster(message);
            RemoveFromPlayerbotsMap(guid);              // deletes bot player ptr inside this WorldSession PlayerBotMap
            botWorldSessionPtr->LogoutPlayer(true);     // this will delete the bot Player object and PlayerbotAI object
            delete botWorldSessionPtr;                  // finally delete the bot's WorldSession
        }
    }
}

void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)
{
    if (Player* bot = GetPlayerBot(guid))
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
        {
            return;
        }
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "goodbye", "Goodbye!", {}));
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();

        Group* group = bot->GetGroup();
        if (group && !bot->InBattleground() && !bot->InBattlegroundQueue() && IsRealPlayer(botAI->GetMaster()))
        {
            PlayerbotRepository::instance().Save(botAI);
        }

        TC_LOG_DEBUG("playerbots", "Bot {} logged out", bot->GetName().c_str());

        bot->SaveToDB(false);

        RemoveFromPlayerbotsMap(guid);  // deletes bot player ptr inside this WorldSession PlayerBotMap

        delete botAI;
    }
}

void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)
{
    playerBots.erase(guid);
}

Player* PlayerbotHolder::GetPlayerBot(ObjectGuid playerGuid) const
{
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? 0 : it->second;
}

Player* PlayerbotHolder::GetPlayerBot(ObjectGuid::LowType lowGuid) const
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? 0 : it->second;
}

void PlayerbotHolder::OnBotLogin(Player* const bot)
{
    // Prevent duplicate login
    if (playerBots.find(bot->GetGUID()) != playerBots.end())
    {
        return;
    }

    PlayerbotsMgr::instance().AddPlayerbotData(bot, true);
    playerBots[bot->GetGUID()] = bot;

    OnBotLoginInternal(bot);

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        // Log a warning here to indicate that the botAI is null
        TC_LOG_DEBUG("mod-playerbots", "PlayerbotAI is null for bot with GUID: {}", bot->GetGUID().ToString());
        return;
    }

    // Bots created before the classic talent walk landed (or otherwise never walked) have an empty trait
    // config: give them the built-in default build for their class once, here at login, instead of
    // waiting for the periodic randomize to reach them (plain logins never randomize, so such a bot
    // would stay untalented for good). The walk is budget-bounded; a bot that already holds ranks
    // (or is aligned by the addclass level sync further down) is skipped or re-walked idempotently.
    {
        namespace Classic = ::Playerbot::Classic;
        UF::TraitConfig const* active = bot->GetTraitConfig(bot->m_activePlayerData->ActiveCombatTraitConfigID);
        uint32 held = 0;
        if (active)
            for (UF::TraitEntry const& e : active->Entries)
                held += uint32(std::max<int32>(0, e.Rank));
        if (!active || held == 0)
        {
            std::vector<uint32> specs;
            for (Classic::TalentBuild const& build : Classic::GetTalentBuilds())
                if (build.cls == bot->GetClass() &&
                    std::find(specs.begin(), specs.end(), build.spec) == specs.end())
                    specs.push_back(build.spec);
            if (!specs.empty() && !Classic::GetSpecOverride(bot->GetGUID()))
                Classic::SetSpecOverride(bot, specs[urand(0, specs.size() - 1)]);
            Classic::ApplyClassicTalents(bot, /*force*/ true);
        }
    }

    Player* master = botAI->GetMaster();

    Group* group = bot->GetGroup();
    if (group)
    {
        bool groupValid = false;
        Group::MemberSlotList const& slots = group->GetMemberSlots();
        for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
        {
            ObjectGuid member = i->guid;
            if (master)
            {
                if (master->GetGUID() == member)
                {
                    groupValid = true;
                    break;
                }
            }

            // Don't disband alt groups when master goes away
            // Controlled by config
            if (sPlayerbotAIConfig.KeepAltsInGroup)
            {
                uint32 account = sCharacterCache->GetCharacterAccountIdByGuid(member);
                if (!sPlayerbotAIConfig.IsInRandomAccountList(account))
                {
                    groupValid = true;
                    break;
                }
            }
        }

        if (!groupValid)
        {
            botAI->LeaveOrDisbandGroup();
        }
    }

    group = bot->GetGroup();
    if (group)
    {
        botAI->ResetStrategies();
    }
    else
    {
        botAI->ResetStrategies(!sRandomPlayerbotMgr.IsRandomBot(bot));
    }
    PlayerbotRepository::instance().Load(botAI);

    // The loaded strategies were saved at the bot's last strategy change and can include another
    // instance's strategy, so apply the current map's again.
    if (sPlayerbotAIConfig.ApplyInstanceStrategies)
        botAI->ApplyInstanceStrategies(bot->GetMapId());

    if (master && !master->HasUnitState(UNIT_STATE_IN_FLIGHT))
    {
        bot->GetMotionMaster()->Clear();
        bot->CleanupAfterTaxiFlight();
    }

    // check activity
    botAI->AllowActivity(ALL_ACTIVITY, true);

    // set delay on login
    botAI->SetNextCheckDelay(urand(2000, 4000));

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "hello", "Hello!", {}), PLAYERBOT_SECURITY_TALK);

    // Queue group operations for world thread
    if (master && master->GetGroup() && !group)
    {
        Group* mgroup = master->GetGroup();
        if (mgroup->GetMembersCount() >= 5)
        {
            // A 5-member party is full, so only a raid can take a 6th member. GroupInviteOperation::
            // Execute() self-converts a non-raid group with >= 5 members to a raid before adding, so a
            // separate ConvertToRaid is unnecessary here. Queue the invite for a raid OR a plain
            // (non-LFG/BG/battlefield) party; we deliberately skip LFG/BG/BFG groups so a bot login
            // never force-converts a live dungeon-finder or battleground group into a raid.
            if (mgroup->isRaidGroup() || (!mgroup->isLFGGroup() && !mgroup->isBGGroup() && !mgroup->isBFGroup()))
            {
                // Queue AddMember operation; Execute() converts the party to a raid before adding.
                auto addOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
                PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(addOp));
            }
        }
        else
        {
            // Queue AddMember operation
            auto addOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(addOp));
        }
    }
    else if (master && !group)
    {
        // Queue group creation and AddMember operation
        auto inviteOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
        PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(inviteOp));
    }
    // if (master)
    // {
    //     // bot->TeleportTo(master);
    // }
    uint32 accountId = bot->GetSession()->GetAccountId();
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(accountId);

    if (isRandomAccount && sPlayerbotAIConfig.RandomBotFixedLevel)
    {
        bot->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
    else if (isRandomAccount && !sPlayerbotAIConfig.RandomBotFixedLevel)
    {
        bot->RemovePlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }

    bot->SaveToDB(false);
    bool addClassBot = sRandomPlayerbotMgr.IsAccountType(accountId, 2);
    if (addClassBot && master && abs((int)master->GetLevel() - (int)bot->GetLevel()) > 3)
    {
        // PlayerbotFactory factory(bot, master->GetLevel());
        // factory.Randomize(false);
        uint32 mixedGearScore =
            PlayerbotAI::GetMixedGearScore(master, true, false, 12) * sPlayerbotAIConfig.AutoInitEquipLevelLimitRatio;
        // work around: distinguish from 0 if no gear
        if (mixedGearScore == 0)
            mixedGearScore = 1;
        PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, mixedGearScore);
        factory.Randomize(false);
    }

    // bots join World chat if not solo oriented
    if (bot->GetLevel() >= 10 && sRandomPlayerbotMgr.IsRandomBot(bot) && GET_PLAYERBOT_AI(bot) &&
        GET_PLAYERBOT_AI(bot)->GetGrouperType() != GrouperType::SOLO)
    {
        // TODO make action/config
        // Make the bot join the world channel for chat. ChannelMgr/Channel state is shared across
        // maps, so the join runs in the world tick window.
        GET_PLAYERBOT_AI(bot)->RunOnWorldThread("join world channel", [](Player* bot) {
            WorldPacket raw(CMSG_CHAT_JOIN_CHANNEL);
            WorldPackets::Channel::JoinChannel packet(std::move(raw));
            packet.ChannelName = "World";
            packet.Password.clear();
            bot->GetSession()->HandleJoinChannel(packet);
        });
    }

    // join the constant channels available in the current zone (12.x derives the channel
    // ids and names from ChatChannels.db2 and the zone instead of the 3.3.5 hard-coded list)
    AreaTableEntry const* current_zone = GET_PLAYERBOT_AI(bot)->GetCurrentZone();
    if (current_zone)
    {
        if (ChannelMgr* cMgr = ChannelMgr::ForTeam(bot->GetTeam()))
        {
            for (ChatChannelsEntry const* channel : sChatChannelsStore)
            {
                if (!channel || !bot->CanJoinConstantChannelInZone(channel, current_zone))
                    continue;

                if (Channel* newChannel = cMgr->GetSystemChannel(channel->ID, current_zone))
                    newChannel->JoinChannel(bot);
            }
        }
    }

}

std::string const PlayerbotHolder::ProcessBotCommand(std::string const cmd, ObjectGuid guid, ObjectGuid masterguid,
                                                     bool admin, uint32 masterAccountId, uint32)
{
    if (!sPlayerbotAIConfig.Enabled || guid.IsEmpty())
        return "bot system is disabled";

    //bool isRandomBot = sRandomPlayerbotMgr.IsRandomBot(guid.GetCounter()); //not used, line marked for removal.
    //bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount); //not used, shadowed, line marked for removal.
    //bool isMasterAccount = (masterAccountId == botAccount); //not used, line marked for removal.

    if (cmd == "add" || cmd == "addaccount" || cmd == "login")
    {
        if (ObjectAccessor::FindPlayer(guid))
            return "player already logged in";

        // For addaccount command, verify it's an account name
        if (cmd == "addaccount")
        {
            uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
            if (!accountId)
            {
                return "character not found";
            }

            if (!sPlayerbotAIConfig.AllowAccountBots && accountId != masterAccountId &&
                !(sPlayerbotAIConfig.AllowTrustedAccountBots && IsAccountLinked(accountId, masterAccountId)))
            {
                return "you can only add bots from your own account or linked accounts";
            }
        }

        AddPlayerBot(guid, masterAccountId);
        return "ok";
    }
    else if (cmd == "remove" || cmd == "logout" || cmd == "rm")
    {
        if (!ObjectAccessor::FindPlayer(guid))
            return "player is offline";

        if (!GetPlayerBot(guid))
            return "not your bot";

        LogoutPlayerBot(guid);
        return "ok";
    }

    // if (admin)
    // {
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        bot = sRandomPlayerbotMgr.GetPlayerBot(guid);

    if (!bot)
        return "bot not found";

    bool addClassBot = sRandomPlayerbotMgr.IsAddclassBot(guid.GetCounter());

    if (!addClassBot)
    {
        if (!(cmd == "refresh=raid" && sPlayerbotAIConfig.ResetInstanceIdForAltBots))
            return "ERROR: You can only use this command on addclass bots.";
    }

    if (!admin)
    {
        Player* master = ObjectAccessor::FindConnectedPlayer(masterguid);
        if (master && (master->IsInCombat() || bot->IsInCombat()))
        {
            return "ERROR: You can not use this command during combat.";
        }
    }

    if (GET_PLAYERBOT_AI(bot))
    {
        if (Player* master = GET_PLAYERBOT_AI(bot)->GetMaster())
        {
            if (master->GetSession()->GetSecurity() <= SEC_PLAYER && sPlayerbotAIConfig.AutoInitOnly &&
                cmd != "init=auto")
            {
                return "The command is not allowed, use init=auto instead.";
            }

            //  Use boot guard
            BotInitGuard guard(bot->GetGUID());
            if (guard.IsLocked())
            {
                return "Initialization already in progress, please wait.";
            }

            int gs;
            if (cmd == "init=white" || cmd == "init=common")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_NORMAL);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=green" || cmd == "init=uncommon")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_UNCOMMON);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=blue" || cmd == "init=rare")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_RARE);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=epic" || cmd == "init=purple")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_EPIC);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=legendary" || cmd == "init=yellow")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=auto")
            {
                uint32 mixedGearScore = PlayerbotAI::GetMixedGearScore(master, true, false, 12) *
                                        sPlayerbotAIConfig.AutoInitEquipLevelLimitRatio;
                // work around: distinguish from 0 if no gear
                if (mixedGearScore == 0)
                    mixedGearScore = 1;
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, mixedGearScore);
                factory.Randomize(false);
                return "ok, gear score limit: " + std::to_string(mixedGearScore / PlayerbotAI::GetItemScoreMultiplier(ItemQualities(ITEM_QUALITY_EPIC))) +
                       "(for epic)";
            }
            else if (cmd.starts_with("init=") && sscanf(cmd.c_str(), "init=%d", &gs) != -1)
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, gs);
                factory.Randomize(false);
                return "ok, gear score limit: " + std::to_string(gs / PlayerbotAI::GetItemScoreMultiplier(ItemQualities(ITEM_QUALITY_EPIC))) + "(for epic)";
            }
        }

        if (cmd == "refresh=raid")
        {  // TODO: This function is not perfect yet. If you are already in a raid,
            // after the command is executed, the AI ​​needs to go back online or exit the raid and re-enter.
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.UnbindInstance();
            return "ok";
        }
    }

    if (cmd == "levelup" || cmd == "level")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.Randomize(true);
        // Reset channel, not the level-up channel: no GiveLevel happens here and no levelup packet
        // reaches AutoMaintenanceOnLevelupAction, and the persistence branch inside Randomize skips
        // the talent tree. Place the build here - the walk also trims a bot that holds more ranks
        // than its level pays for.
        factory.InitTalentsTree(true);
        return "ok";
    }
    else if (cmd == "refresh")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.Refresh();
        return "ok";
    }
    else if (cmd == "random")
    {
        sRandomPlayerbotMgr.Randomize(bot);
        return "ok";
    }
    else if (cmd == "quests")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitInstanceQuests();
        return "Initialization quests";
    }
    // }

    return "unknown command";
}

// Added for gender choice : Returns the gender of an offline character: 0 = male, 1 = female.
static uint8 GetOfflinePlayerGender(ObjectGuid guid)
{
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT gender FROM characters WHERE guid = {}", guid.GetCounter());

    if (result)
        return PlayerbotFieldGet<uint8>((*result)[0]);       // 0 = male, 1 = female

    return GENDER_MALE;                         // fallback value
}

bool PlayerbotMgr::HandlePlayerbotMgrCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.Enabled)
    {
        handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
        return false;
    }

    WorldSession* m_session = handler->GetSession();
    if (!m_session)
    {
        handler->PSendSysMessage("You may only add bots from an active session");
        return false;
    }

    Player* player = m_session->GetPlayer();
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
    {
        handler->PSendSysMessage("You cannot control bots yet");
        return false;
    }

    std::vector<std::string> messages = mgr->HandlePlayerbotCommand(args, player);
    if (messages.empty())
        return true;

    for (std::vector<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        // ChatHandler::PSendSysMessage is printf style: a "{}" placeholder would be printed as is.
        handler->SendSysMessage(*i);
    }

    return true;
}

std::vector<std::string> PlayerbotHolder::HandlePlayerbotCommand(char const* args, Player* master)
{
    std::vector<std::string> messages;

    if (!*args)
    {
        messages.push_back("usage: list/reload/tweak/self or add/addaccount/init/remove PLAYERNAME\n");
        messages.push_back("usage: addclass CLASSNAME [male|female|0|1]");
        return messages;
    }

    char* cmd = strtok((char*)args, " ");
    char* charname = strtok(nullptr, " ");
    char* genderArg = strtok(nullptr, " ");    // Added for gender choice [male|female|0|1] optionnel

    if (!cmd)
    {
        messages.push_back("usage: list/reload/tweak/self or add/init/remove PLAYERNAME or addclass CLASSNAME [male|female]");
        return messages;
    }

    if (!strcmp(cmd, "initself"))
    {
        if (master->CanBeGameMaster())
        {
            // OnBotLogin(master);
            PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_EPIC);
            factory.Randomize(false);
            messages.push_back("initself ok");
            return messages;
        }
        else
        {
            messages.push_back("ERROR: Only GM can use this command.");
            return messages;
        }
    }

    if (!strncmp(cmd, "initself=", 9))
    {
        if (!strcmp(cmd, "initself=uncommon"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_UNCOMMON);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=rare"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_RARE);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=epic"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_EPIC);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=legendary"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_LEGENDARY);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        int32 gs;
        if (sscanf(cmd, "initself=%d", &gs) != -1)
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_LEGENDARY, gs);
                factory.Randomize(false);
                messages.push_back("initself ok, gs = " + std::to_string(gs));
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
    }

    if (!strcmp(cmd, "list"))
    {
        messages.push_back(ListBots(master));
        return messages;
    }

    if (!strcmp(cmd, "reload"))
    {
        if (master->CanBeGameMaster())
        {
            sPlayerbotAIConfig.Initialize();
            messages.push_back("Config reloaded.");
            return messages;
        }
        else
        {
            messages.push_back("ERROR: Only GM can use this command.");
            return messages;
        }
    }

    if (!strcmp(cmd, "tweak"))
    {
        sPlayerbotAIConfig.TweakValue = (sPlayerbotAIConfig.TweakValue + 1) % 3;
        if (sPlayerbotAIConfig.TweakValue > 2)
            sPlayerbotAIConfig.TweakValue = 0;

        messages.push_back("Set tweakvalue to " + std::to_string(sPlayerbotAIConfig.TweakValue));
        return messages;
    }

    if (!strcmp(cmd, "self"))
    {
        if (GET_PLAYERBOT_AI(master))
        {
            messages.push_back("SelfBot is now deactivated.");
            delete GET_PLAYERBOT_AI(master);

            if (master->isTaxiCheater())
                master->SetTaxiCheater(false);
        }
        else if (sPlayerbotAIConfig.SelfBotLevel == 0)
            messages.push_back("SelfBot is disabled server-wide.");
        else if (sPlayerbotAIConfig.SelfBotLevel == 1 && !master->CanBeGameMaster())
            messages.push_back("SelfBot is restricted for this account.");
        else
        {
            messages.push_back("SelfBot is now active.");
            PlayerbotsMgr::instance().AddPlayerbotData(master, true);
            GET_PLAYERBOT_AI(master)->SetMaster(master);
            PlayerbotRepository::instance().Load(GET_PLAYERBOT_AI(master));
        }

        return messages;
    }

    if (!strcmp(cmd, "lookup"))
    {
        messages.push_back(LookupBots(master));
        return messages;
    }

    if (!strcmp(cmd, "addclass"))
    {
        if (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster())
        {
            messages.push_back("You do not have permission to create bot by addclass command");
            return messages;
        }
        if (!charname)
        {
            messages.push_back(
                "addclass: invalid CLASSNAME(warrior/paladin/hunter/rogue/priest/shaman/mage/warlock/druid)");
            return messages;
        }
        uint8 claz;
        if (!strcmp(charname, "warrior"))
        {
            claz = 1;
        }
        else if (!strcmp(charname, "paladin"))
        {
            claz = 2;
        }
        else if (!strcmp(charname, "hunter"))
        {
            claz = 3;
        }
        else if (!strcmp(charname, "rogue"))
        {
            claz = 4;
        }
        else if (!strcmp(charname, "priest"))
        {
            claz = 5;
        }
        else if (!strcmp(charname, "shaman"))
        {
            claz = 7;
        }
        else if (!strcmp(charname, "mage"))
        {
            claz = 8;
        }
        else if (!strcmp(charname, "warlock"))
        {
            claz = 9;
        }
        else if (!strcmp(charname, "druid"))
        {
            claz = 11;
        }
        // 1.60 has nine classes; any other class argument is rejected.
        else
        {
            messages.push_back("Error: Invalid Class. Try again.");
            return messages;
        }
        //  Added for gender choice : Parsing gender
        int8 gender = -1; // -1 = gender will be random
        if (genderArg)
        {
            std::string g = genderArg;
            std::transform(g.begin(), g.end(), g.begin(), ::tolower);

            if (g == "male" || g == "0")
                gender = GENDER_MALE; // 0
            else if (g == "female" || g == "1")
                gender = GENDER_FEMALE; // 1
            else
            {
                messages.push_back("Unknown gender : " + g + " (male/female/0/1)");
                return messages;
            }
        } //end

        uint8 teamId = master->GetTeamId();
        std::unordered_set<ObjectGuid> const& guidCache = sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(teamId == TEAM_ALLIANCE, claz)];
        auto const pickFirstEligible = [&](auto const& pool) -> ObjectGuid
        {
            for (ObjectGuid const& guid : pool)
            {
                // If the user requested a specific gender, skip any character that doesn't match.
                if (gender != -1 && GetOfflinePlayerGender(guid) != gender)
                    continue;
                if (botLoading.find(guid) != botLoading.end())
                    continue;
                if (ObjectAccessor::FindConnectedPlayer(guid))
                    continue;
                uint32 guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
                if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                    continue;
                return guid;
            }
            return ObjectGuid::Empty;
        };

        // The cache is an unordered_set: its iteration order does not change between calls, so the first
        // eligible character is always the same one. Walk a shuffled copy when a random one is wanted,
        // and the cache itself otherwise, so the default path copies nothing.
        ObjectGuid picked = ObjectGuid::Empty;
        if (sPlayerbotAIConfig.AddClassRandomCharacter)
        {
            std::vector<ObjectGuid> candidates(guidCache.begin(), guidCache.end());
            Trinity::Containers::RandomShuffle(candidates);
            picked = pickFirstEligible(candidates);
        }
        else
            picked = pickFirstEligible(guidCache);

        if (picked.IsEmpty())
        {
            messages.push_back("Add class failed, no available characters!");
            return messages;
        }

        AddPlayerBot(picked, master->GetSession()->GetAccountId());
        messages.push_back("Add class " + std::string(charname));
        return messages;
    }

    std::string charnameStr;

    if (!charname)
    {
        std::string name;
        bool isPlayer = sCharacterCache->GetCharacterNameByGuid(master->GetTarget(), name);
        // Player* tPlayer = ObjectAccessor::FindConnectedPlayer(master->GetTarget());
        if (isPlayer)
        {
            charnameStr = name;
        }
        else
        {
            messages.push_back("usage: list/reload/tweak/self or add/init/remove PLAYERNAME");
            return messages;
        }
    }
    else
    {
        charnameStr = charname;
    }

    std::string const cmdStr = cmd;

    std::unordered_set<std::string> bots;
    if (charnameStr == "*" && master)
    {
        Group* group = master->GetGroup();
        if (!group)
        {
            messages.push_back("you must be in group");
            return messages;
        }

        Group::MemberSlotList slots = group->GetMemberSlots();
        for (Group::member_citerator i = slots.begin(); i != slots.end(); i++)
        {
            ObjectGuid member = i->guid;

            if (member == master->GetGUID())
                continue;

            std::string bot;
            if (sCharacterCache->GetCharacterNameByGuid(member, bot))
                bots.insert(bot);
        }
    }

    if (charnameStr == "!" && master && master->GetSession()->GetSecurity() > SEC_GAMEMASTER)
    {
        for (PlayerBotMap::const_iterator i = GetPlayerBotsBegin(); i != GetPlayerBotsEnd(); ++i)
        {
            if (Player* bot = i->second)
                if (bot->IsInWorld())
                    bots.insert(bot->GetName());
        }
    }

    std::vector<std::string> chars = split(charnameStr, ',');
    for (std::vector<std::string>::iterator i = chars.begin(); i != chars.end(); i++)
    {
        std::string s = *i;

        if (!strcmp(cmd, "addaccount"))
        {
            // When using addaccount, first try to get account ID directly
            uint32 accountId = GetAccountId(s);
            if (!accountId)
            {
                // If not found, try to get account ID from character name
                std::string charName = s;
                if (!normalizePlayerName(charName))
                {
                    messages.push_back("Neither account nor character '" + s + "' found");
                    continue;
                }
                ObjectGuid charGuid = sCharacterCache->GetCharacterGuidByName(charName);
                if (!charGuid)
                {
                    messages.push_back("Neither account nor character '" + s + "' found");
                    continue;
                }
                accountId = sCharacterCache->GetCharacterAccountIdByGuid(charGuid);
                if (!accountId)
                {
                    messages.push_back("Could not find account for character '" + s + "'");
                    continue;
                }
            }

            QueryResult results = CharacterDatabase.PQuery("SELECT name FROM characters WHERE account = {}", accountId);
            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    std::string const charName = PlayerbotFieldGet<std::string>(fields[0]);
                    bots.insert(charName);
                } while (results->NextRow());
            }
        }
        else
        {
            // For regular add command, only add the specific character
            if (!normalizePlayerName(s))
            {
                messages.push_back("Character '" + *i + "' not found");
                continue;
            }
            ObjectGuid charGuid = sCharacterCache->GetCharacterGuidByName(s);
            if (!charGuid)
            {
                messages.push_back("Character '" + s + "' not found");
                continue;
            }
            bots.insert(s);
        }
    }

    for (auto i = bots.begin(); i != bots.end(); ++i)
    {
        std::string const bot = *i;

        std::ostringstream out;
        out << cmdStr << ": " << bot << " - ";

        ObjectGuid member = sCharacterCache->GetCharacterGuidByName(bot);
        if (!member)
        {
            out << "character not found";
        }
        else if (master && member != master->GetGUID())
        {
            out << ProcessBotCommand(cmdStr, member, master->GetGUID(),
                                     master->CanBeGameMaster(),
                                     master->GetSession()->GetAccountId(), master->GetGuildId());
        }
        else if (!master)
        {
            out << ProcessBotCommand(cmdStr, member, ObjectGuid::Empty, true, -1, -1);
        }

        messages.push_back(out.str());
    }

    return messages;
}

uint32 PlayerbotHolder::GetAccountId(std::string const name) { return AccountMgr::GetId(name); }

uint32 PlayerbotHolder::GetAccountId(ObjectGuid guid)
{
    if (!guid.IsPlayer())
        return 0;

    // prevent DB access for online player
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        return player->GetSession()->GetAccountId();

    ObjectGuid::LowType lowguid = guid.GetCounter();

    if (QueryResult result = CharacterDatabase.PQuery("SELECT account FROM characters WHERE guid = {}", lowguid))
    {
        uint32 acc = PlayerbotFieldGet<uint32>((*result)[0]);
        return acc;
    }

    return 0;
}

std::string const PlayerbotHolder::ListBots(Player* master)
{
    std::set<std::string> bots;
    std::map<uint8, std::string> classNames;

    classNames[CLASS_DRUID] = "Druid";
    classNames[CLASS_HUNTER] = "Hunter";
    classNames[CLASS_MAGE] = "Mage";
    classNames[CLASS_PALADIN] = "Paladin";
    classNames[CLASS_PRIEST] = "Priest";
    classNames[CLASS_ROGUE] = "Rogue";
    classNames[CLASS_SHAMAN] = "Shaman";
    classNames[CLASS_WARLOCK] = "Warlock";
    classNames[CLASS_WARRIOR] = "Warrior";

    std::map<std::string, std::string> online;
    std::vector<std::string> names;
    std::map<std::string, std::string> classes;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        std::string const name = bot->GetName();
        bots.insert(name);

        names.push_back(name);
        online[name] = "+";
        classes[name] = classNames[bot->GetClass()];
    }

    if (master)
    {
        QueryResult results = CharacterDatabase.PQuery("SELECT class, name FROM characters WHERE account = {}",
                                                      master->GetSession()->GetAccountId());
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                uint8 cls = PlayerbotFieldGet<uint8>(fields[0]);
                std::string const name = PlayerbotFieldGet<std::string>(fields[1]);
                if (bots.find(name) == bots.end() && name != master->GetSession()->GetPlayerName())
                {
                    names.push_back(name);
                    online[name] = "-";
                    classes[name] = classNames[cls];
                }
            } while (results->NextRow());
        }
    }

    std::sort(names.begin(), names.end());

    if (Group* group = master->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = ObjectAccessor::FindPlayer(itr->guid);
            if (member && sRandomPlayerbotMgr.IsRandomBot(member))
            {
                std::string const name = member->GetName();

                names.push_back(name);
                online[name] = "+";
                classes[name] = classNames[member->GetClass()];
            }
        }
    }

    std::ostringstream out;
    bool first = true;
    out << "Bot roster: ";
    for (std::vector<std::string>::iterator i = names.begin(); i != names.end(); ++i)
    {
        if (first)
            first = false;
        else
            out << ", ";

        std::string const name = *i;
        out << online[name] << name << " " << classes[name];
    }

    return out.str();
}

std::string const PlayerbotHolder::LookupBots(Player*)
{
    std::list<std::string> messages;
    messages.push_back("Classes Available:");
    messages.push_back("|TInterface\\icons\\INV_Sword_27.png:25:25:0:-1|t Warrior");
    messages.push_back("|TInterface\\icons\\INV_Hammer_01.png:25:25:0:-1|t Paladin");
    messages.push_back("|TInterface\\icons\\INV_Weapon_Bow_07.png:25:25:0:-1|t Hunter");
    messages.push_back("|TInterface\\icons\\INV_ThrowingKnife_04.png:25:25:0:-1|t Rogue");
    messages.push_back("|TInterface\\icons\\INV_Staff_30.png:25:25:0:-1|t Priest");
    messages.push_back("|TInterface\\icons\\inv_jewelry_talisman_04.png:25:25:0:-1|t Shaman");
    messages.push_back("|TInterface\\icons\\INV_staff_30.png:25:25:0:-1|t Mage");
    messages.push_back("|TInterface\\icons\\INV_staff_30.png:25:25:0:-1|t Warlock");
    messages.push_back("|TInterface\\icons\\Ability_Druid_Maul.png:25:25:0:-1|t Druid");
    messages.push_back("DK");
    messages.push_back("(Usage: .bot lookup CLASS)");
    std::string ret_msg;
    for (std::string msg : messages)
    {
        ret_msg += msg + "\n";
    }
    return ret_msg;
}


PlayerbotMgr::PlayerbotMgr(Player* const master) : PlayerbotHolder(), master(master), lastErrorTell(0) {}

PlayerbotMgr::~PlayerbotMgr()
{
    if (master)
        PlayerbotsMgr::instance().RemovePlayerBotData(master->GetGUID(), false);
}

void PlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool /*minimal*/)
{
    SetNextCheckDelay(sPlayerbotAIConfig.ReactDelay);
    CheckTellErrors(elapsed);
}

void PlayerbotMgr::HandleCommand(uint32 type, std::string const text)
{
    Player* master = GetMaster();
    if (!master)
        return;

    if (text.find(sPlayerbotAIConfig.CommandSeparator) != std::string::npos)
    {
        std::vector<std::string> commands;
        split(commands, text, sPlayerbotAIConfig.CommandSeparator.c_str());
        for (std::vector<std::string>::iterator i = commands.begin(); i != commands.end(); ++i)
        {
            HandleCommand(type, *i);
        }

        return;
    }

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleCommand(type, text, master);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == master)
            botAI->HandleCommand(type, text, master);
    }
}

void PlayerbotMgr::HandleMasterIncomingPacket(WorldPacket const& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleMasterIncomingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == GetMaster())
            botAI->HandleMasterIncomingPacket(packet);
    }

    switch (packet.GetOpcode())
    {
        // if master is logging out, log out all bots
        case CMSG_LOGOUT_REQUEST:
        {
            Player* master = GetMaster();
            if (master)
            {
                // Replicate the AFK logout prevention checks from WorldSession::HandleLogoutRequestOpcode
                // so bots are not logged out when the master's own logout is going to be prevented.
                // 12.x has no Afk.PreventLogout config; the master's own logout is never
                // blocked for being AFK, so the bots follow the same rule.
                if (false)
                {
                    break;
                }
            }

            LogoutAllBots();
            break;
        }
        // if master cancelled logout, cancel too
        case CMSG_LOGOUT_CANCEL:
        {
            CancelLogout();
            break;
        }
    }
}

void PlayerbotMgr::HandleMasterOutgoingPacket(WorldPacket const& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleMasterOutgoingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == GetMaster())
            botAI->HandleMasterOutgoingPacket(packet);
    }
}

void PlayerbotMgr::SaveToDB()
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        bot->SaveToDB(false);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (GET_PLAYERBOT_AI(bot) && GET_PLAYERBOT_AI(bot)->GetMaster() == GetMaster())
            bot->SaveToDB(false);
    }
}

void PlayerbotMgr::OnBotLoginInternal(Player* const bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return;
    }
    botAI->SetMaster(master);
    botAI->ResetStrategies();

    TC_LOG_INFO("playerbots", "Bot {} logged in", bot->GetName().c_str());
}

void PlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (!player)
        return;

    WorldSession* session = player->GetSession();
    if (!session)
    {
        TC_LOG_WARN("playerbots", "Unable to register locale priority for player {} because the session is missing", player->GetName());
        return;
    }

    // DB locale (source of bot text translation)
    LocaleConstant const databaseLocale = session->GetSessionDbLocaleIndex();

    // For bot texts (DB-driven), prefer the database locale with a safe fallback.
    LocaleConstant usedLocale = databaseLocale;
    if (usedLocale >= TOTAL_LOCALES)
        usedLocale = LOCALE_enUS; // fallback

    // set locale priority for bot texts
    PlayerbotTextMgr::instance().AddLocalePriority(usedLocale);

    if (sPlayerbotAIConfig.SelfBotLevel > 2)
        HandlePlayerbotCommand("self", player);

    if (!sPlayerbotAIConfig.BotAutologin)
        return;

    uint32 accountId = session->GetAccountId();
    QueryResult results = CharacterDatabase.PQuery("SELECT name FROM characters WHERE account = {}", accountId);
    if (results)
    {
        std::ostringstream out;
        out << "add ";
        bool first = true;
        do
        {
            Field* fields = results->Fetch();

            if (first)
                first = false;
            else
                out << ",";

            out << PlayerbotFieldGet<std::string>(fields[0]);
        } while (results->NextRow());

        HandlePlayerbotCommand(out.str().c_str(), player);
    }
}

void PlayerbotMgr::TellError(std::string const botName, std::string const text)
{
    std::set<std::string> names = errors[text];
    if (names.find(botName) == names.end())
    {
        names.insert(botName);
    }

    errors[text] = names;
}

void PlayerbotMgr::CheckTellErrors(uint32 /*elapsed*/)
{
    time_t now = time(nullptr);
    if ((now - lastErrorTell) < sPlayerbotAIConfig.ErrorDelay / 1000)
        return;

    lastErrorTell = now;

    for (PlayerBotErrorMap::iterator i = errors.begin(); i != errors.end(); ++i)
    {
        std::string const text = i->first;
        std::set<std::string> names = i->second;

        std::ostringstream out;
        bool first = true;
        for (std::set<std::string>::iterator j = names.begin(); j != names.end(); ++j)
        {
            if (!first)
                out << ", ";
            else
                first = false;

            out << *j;
        }

        out << "|cfff00000: " << text;

        ChatHandler(master->GetSession()).PSendSysMessage(out.str().c_str());
    }

    errors.clear();
}

void PlayerbotsMgr::AddPlayerbotData(Player* player, bool isBotAI)
{
    if (!player)
    {
        return;
    }
    // If the guid already exists in the map, remove it

    if (!isBotAI)
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsMgrMap.find(player->GetGUID());
        if (itr != _playerbotsMgrMap.end())
        {
            _playerbotsMgrMap.erase(itr);
        }
        PlayerbotMgr* playerbotMgr = new PlayerbotMgr(player);
        ASSERT(_playerbotsMgrMap.emplace(player->GetGUID(), playerbotMgr).second);

        playerbotMgr->OnPlayerLogin(player);
    }
    else
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsAIMap.find(player->GetGUID());
        if (itr != _playerbotsAIMap.end())
        {
            _playerbotsAIMap.erase(itr);
        }
        PlayerbotAI* botAI = new PlayerbotAI(player);
        ASSERT(_playerbotsAIMap.emplace(player->GetGUID(), botAI).second);
    }

    // Drive this player from its own update loop instead of a module-specific core hook. The event
    // looks both instances up by guid on every tick, so recreating either one needs no change here.
    BotTickEvent::Install(player);
}

void PlayerbotsMgr::RemovePlayerBotData(ObjectGuid const& guid, bool is_AI)
{
    if (is_AI)
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsAIMap.find(guid);
        if (itr != _playerbotsAIMap.end())
        {
            _playerbotsAIMap.erase(itr);
        }
    }
    else
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsMgrMap.find(guid);
        if (itr != _playerbotsMgrMap.end())
        {
            _playerbotsMgrMap.erase(itr);
        }
    }
}

PlayerbotAI* PlayerbotsMgr::GetPlayerbotAI(Player* player)
{
    if (!(sPlayerbotAIConfig.Enabled) || !player)
    {
        return nullptr;
    }
    // if (player->GetSession()->isLogingOut() || player->IsDuringRemoveFromWorld())
    // {
    //     return nullptr;
    // }
    auto itr = _playerbotsAIMap.find(player->GetGUID());
    if (itr != _playerbotsAIMap.end())
    {
        if (itr->second->IsBotAI())
            return dynamic_cast<PlayerbotAI*>(itr->second);
    }

    return nullptr;
}

PlayerbotMgr* PlayerbotsMgr::GetPlayerbotMgr(Player* player)
{
    if (!(sPlayerbotAIConfig.Enabled) || !player)
    {
        return nullptr;
    }
    auto itr = _playerbotsMgrMap.find(player->GetGUID());
    if (itr != _playerbotsMgrMap.end())
    {
        if (!itr->second->IsBotAI())
            return dynamic_cast<PlayerbotMgr*>(itr->second);
    }

    return nullptr;
}

void PlayerbotMgr::HandleSetSecurityKeyCommand(Player* player, std::string const& key)
{
    uint32 accountId = player->GetSession()->GetAccountId();

    // Hash the security key using SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)key.c_str(), key.size(), hash);

    // Convert the hash to a hexadecimal string
    std::ostringstream hashedKey;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        hashedKey << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    // Store the hashed key in the database (hex digest only, no escaping needed)
    CharacterDatabase.PExecute("REPLACE INTO playerbots_account_keys (account_id, security_key) VALUES ({}, '{}')",
                               accountId, hashedKey.str());

    ChatHandler(player->GetSession()).PSendSysMessage("Security key set successfully.");
}

void PlayerbotMgr::HandleLinkAccountCommand(Player* player, std::string const& accountName, std::string const& key)
{
    QueryResult result = LoginDatabase.PQuery("SELECT id FROM account WHERE username = '{}'", accountName);
    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Account not found.");
        return;
    }

    Field* fields = result->Fetch();
    uint32 linkedAccountId = PlayerbotFieldGet<uint32>(fields[0]);

    QueryResult keyResult = CharacterDatabase.PQuery(
        "SELECT security_key FROM playerbots_account_keys WHERE account_id = {}", linkedAccountId);
    if (!keyResult)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Invalid security key.");
        return;
    }

    // Hash the provided key
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)key.c_str(), key.size(), hash);

    // Convert the hash to a hexadecimal string
    std::ostringstream hashedKey;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        hashedKey << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    // Compare the hashed key with the stored hashed key
    std::string storedKey = PlayerbotFieldGet<std::string>(*keyResult->Fetch());
    if (hashedKey.str() != storedKey)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Invalid security key.");
        return;
    }

    uint32 accountId = player->GetSession()->GetAccountId();
    CharacterDatabase.PExecute("INSERT IGNORE INTO playerbots_account_links (account_id, linked_account_id) VALUES ({}, {})",
                               accountId, linkedAccountId);
    CharacterDatabase.PExecute("INSERT IGNORE INTO playerbots_account_links (account_id, linked_account_id) VALUES ({}, {})",
                               linkedAccountId, accountId);

    ChatHandler(player->GetSession()).PSendSysMessage("Account linked successfully.");
}

void PlayerbotMgr::HandleViewLinkedAccountsCommand(Player* player)
{
    uint32 accountId = player->GetSession()->GetAccountId();
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT linked_account_id FROM playerbots_account_links WHERE account_id = {}", accountId);

    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("No linked accounts.");
        return;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("Linked accounts:");
    do
    {
        Field* fields = result->Fetch();
        uint32 linkedAccountId = PlayerbotFieldGet<uint32>(fields[0]);

        QueryResult accountResult = LoginDatabase.PQuery("SELECT username FROM account WHERE id = {}", linkedAccountId);
        if (accountResult)
        {
            Field* accountFields = accountResult->Fetch();
            std::string username = PlayerbotFieldGet<std::string>(accountFields[0]);
            ChatHandler(player->GetSession()).SendSysMessage("- " + username);
        }
        else
        {
            ChatHandler(player->GetSession()).PSendSysMessage("- Unknown account");
        }
    } while (result->NextRow());
}

void PlayerbotMgr::HandleUnlinkAccountCommand(Player* player, std::string const& accountName)
{
    QueryResult result = LoginDatabase.PQuery("SELECT id FROM account WHERE username = '{}'", accountName);
    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Account not found.");
        return;
    }

    Field* fields = result->Fetch();
    uint32 linkedAccountId = PlayerbotFieldGet<uint32>(fields[0]);
    uint32 accountId = player->GetSession()->GetAccountId();

    CharacterDatabase.PExecute(
        "DELETE FROM playerbots_account_links WHERE (account_id = {} AND linked_account_id = {}) OR (account_id = {} AND linked_account_id = {})",
        accountId, linkedAccountId, linkedAccountId, accountId);

    ChatHandler(player->GetSession()).PSendSysMessage("Account unlinked successfully.");
}
