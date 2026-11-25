/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Playerbots.h"
#include "Channel.h"
#include "CheckMountStateAction.h"
#include "ClassicSpec.h"
#include "ClassicTalentsApply.h"
#include "ClassicTalentBuilds.h"
#include "Config.h"
#include "BuiltInConfig.h"
#include "DBUpdater.h"
#include "DatabaseEnv.h"
#include "DatabaseEnv.h"
#include <mysqld_error.h>
#include "GatherNodeMgr.h"
#include "GuildTaskMgr.h"
#include "PhasingHandler.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotCommandScript.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotSpellRepository.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "cmath"

namespace
{
    // Set once the world initialization completed; every periodic hook short-circuits
    // when it is false so a missing module database disables the module instead of
    // letting later ticks dereference uninitialized managers.
    bool playerbotsWorldInitialized = false;
}

class PlayerbotsPlayerScript : public PlayerScript
{
public:
    PlayerbotsPlayerScript() : PlayerScript("PlayerbotsPlayerScript") {}

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player->GetSession()->IsBot())
        {
            PlayerbotsMgr::instance().AddPlayerbotData(player, false);
            sRandomPlayerbotMgr.OnPlayerLogin(player);

            // Before modifying the following messages, please make sure it does not violate the GNU GPLv2
            // license especially if you are distributing a repack or hosting a public server
            // e.g. you can replace the URL with your own repository,
            // but it should be publicly accessible and include all modifications you've made
            if (sPlayerbotAIConfig.Enabled)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cff00ff00This server runs with |cff00ccffmod-playerbots|r "
                    "|cffcccccchttps://github.com/mod-playerbots/mod-playerbots|r");
            }

            if (sPlayerbotAIConfig.Enabled || sPlayerbotAIConfig.RandomBotAutologin)
            {
                std::string maxAllowedBotCount = std::to_string(sRandomPlayerbotMgr.GetMaxAllowedBotCount());

                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cff00ff00Playerbots:|r The server is configured with " + maxAllowedBotCount + " bots.");
            }
        }
        else if (player->IsInWorld())
        {
            // Headless bot sessions: the core sets PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME only from the
            // time-sync queue (CharacterHandler), which never fills for a sessionless bot, so CanNeverSee rejected
            // every creature and the bot could not see or attack any mob. And PhasingHandler::OnMapChange (normally
            // reached through SendInitialPacketsAfterAddToMap) never ran, leaving the bot's PhaseShift empty — the
            // bot sat in NO phase. Both fixes are from the V2 login pipeline and are idempotent.
            player->SetPlayerLocalFlag(PLAYER_LOCAL_FLAG_OVERRIDE_TRANSPORT_SERVER_TIME);
            player->UpdateZone(player->GetZoneId(), player->GetAreaId());
            PhasingHandler::OnMapChange(player);
        }
    }

    // The upstream logout hook fires from WorldSession::LogoutPlayer while the player object is still
    // alive, right before it is deleted - the same point where the module's own OnDestructPlayer hook
    // used to release these two. Doing it here also leaves both registries clean (their destructors
    // unregister by guid), and a bot that was already disposed by DisablePlayerBot is simply not found.
    void OnLogout(Player* player) override
    {
        // What the module's own OnPlayerBeforeLogout hook used to do: take the master's bots down first
        // (a self bot keeps running on the player's own AI), then let the random-bot manager see the
        // logout. The upstream hook fires slightly later (after the player is saved), which is what
        // makes this hook enough on its own.
        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
        {
            PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

            if (botAI == nullptr || IsSelfBot(player))
                playerbotMgr->LogoutAllBots();
        }

        sRandomPlayerbotMgr.OnPlayerLogout(player);

        // Release the per-player module data while the player object is still alive. Both destructors
        // unregister by guid, and a bot already disposed by DisablePlayerBot is simply not found here.
        // This replaces the module's own MiscScript::OnDestructPlayer hook.
        if (PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player))
            delete botAI;

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
            delete playerbotMgr;
    }

    void OnBeforePetitionSign(Player* player, ObjectGuid /*petitionGuid*/, bool& alreadySignedByAccount) override
    {
        if (!alreadySignedByAccount)
            return;

        // bots share accounts, so the same-account rule must not stop them from signing each other
        if (PlayerbotsMgr::instance().GetPlayerbotAI(player) != nullptr)
            alreadySignedByAccount = false;
    }

    using PlayerScript::OnChat;  // keep the base overloads visible

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || lang == LANG_ADDON || !receiver)
            return;

        if (PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(receiver))
            botAI->HandleCommand(type, msg, player);
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Group* group) override
    {
        // Addon traffic (DBM, Carbonite, ...) is no bot command; its text used to trigger item commands like trade.
        if (!group || lang == LANG_ADDON)
            return;

        for (GroupReference const& gref : group->GetMembers())
        {
            Player* const member = gref.GetSource();

            if (member == nullptr)
                continue;

            PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);

            if (botAI == nullptr)
                continue;

            botAI->HandleCommand(type, msg, player);
        }
    }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Guild* /*guild*/) override
    {
        if (type != CHAT_MSG_GUILD || lang == LANG_ADDON)
            return;

        PlayerbotMgr* playerbotMgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);

        if (playerbotMgr == nullptr)
            return;

        for (PlayerBotMap::const_iterator it = playerbotMgr->GetPlayerBotsBegin(); it != playerbotMgr->GetPlayerBotsEnd(); ++it)
        {
            Player* const bot = it->second;

            if (bot == nullptr)
                continue;

            if (bot->GetGuildId() != player->GetGuildId())
                continue;

            PlayerbotsMgr::instance().GetPlayerbotAI(bot)->HandleCommand(type, msg, player);
        }
    }

    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Channel* channel) override
    {
        PlayerbotMgr* const playerbotMgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);

        if (playerbotMgr != nullptr && channel->GetFlags() & 0x18)
            playerbotMgr->HandleCommand(type, msg);

        sRandomPlayerbotMgr.HandleCommand(type, msg, player);
    }

    // The core's own OnGiveXP hook: it fires on every path that actually grants XP, with the same
    // writable amount (the xpSource parameter of the AC signature was never used here).
    void OnGiveXP(Player* player, uint32& amount, Unit* /*victim*/) override
    {
        // early return
        if (sPlayerbotAIConfig.RandomBotXPRate == 1.0 || !player)
            return;

        // no XP multiplier, when player is no bot.
        if (!player->GetSession()->IsBot() || !sRandomPlayerbotMgr.IsRandomBot(player))
            return;

        // no XP multiplier, when bot is in a group with a real player.
        if (Group* group = player->GetGroup())
        {
            for (GroupReference const& gref : group->GetMembers())
            {
                Player* member = gref.GetSource();
                if (!member)
                    continue;

                if (!member->GetSession()->IsBot())
                    return;
            }
        }

        // otherwise apply bot XP multiplier.
        amount = static_cast<uint32>(std::round(static_cast<float>(amount) * sPlayerbotAIConfig.RandomBotXPRate));
    }
};

class PlayerbotsServerScript : public ServerScript
{
public:
    PlayerbotsServerScript() : ServerScript("PlayerbotsServerScript") {}

    void OnPacketSend(WorldSession* session, WorldPacket& packet) override
    {
        Player* player = session->GetPlayer();

        if (player == nullptr)
            return;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI != nullptr)
            botAI->HandleBotOutgoingPacket(packet);

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
            playerbotMgr->HandleMasterOutgoingPacket(packet);
    }

    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (Player* player = session->GetPlayer())
            if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
                playerbotMgr->HandleMasterIncomingPacket(packet);
    }
};

class PlayerbotsUnitScript : public UnitScript
{
public:
    PlayerbotsUnitScript() : UnitScript("PlayerbotsUnitScript") {}

    // Guild task kill credit. The module used to get this from its own PlayerScript::OnCreatureKillCredit
    // hook; the upstream OnUnitDeath hook fires for every kill, and resolving the attacker to the player
    // controlling it gives the same credit the core hook had (a pet's or totem's kill counts for its
    // owner). Only the creature entry matters to the task check, so running before the death state is
    // set is fine.
    void OnUnitDeath(Unit* victim, Unit* killer) override
    {
        Creature* creature = victim ? victim->ToCreature() : nullptr;
        if (!creature || !killer)
            return;

        if (Player* player = killer->GetCharmerOrOwnerPlayerOrPlayerItself())
            GuildTaskMgr::instance().CheckKillTask(player, creature);
    }
};

class PlayerbotsWorldScript : public WorldScript
{
public:
    PlayerbotsWorldScript() : WorldScript("PlayerbotsWorldScript") {}

    void OnWorldInitialized() override
    {
        // Before modifying the following messages, please make sure it does not violate the GNU GPLv2
        // license especially if you are distributing a repack or hosting a public server
        // e.g. you can replace the URL with your own repository,
        // but it should be publicly accessible and include all modifications you've made
        TC_LOG_INFO("server.loading", "╔══════════════════════════════════════════════════════════╗");
        TC_LOG_INFO("server.loading", "║                                                          ║");
        TC_LOG_INFO("server.loading", "║              AzerothCore Playerbots Module               ║");
        TC_LOG_INFO("server.loading", "║                                                          ║");
        TC_LOG_INFO("server.loading", "╟──────────────────────────────────────────────────────────╢");
        TC_LOG_INFO("server.loading", "║     mod-playerbots is a community-driven open-source     ║");
        TC_LOG_INFO("server.loading", "║  project based on AzerothCore, licensed under GNU GPLv2  ║");
        TC_LOG_INFO("server.loading", "╟──────────────────────────────────────────────────────────╢");
        TC_LOG_INFO("server.loading", "║     https://github.com/mod-playerbots/mod-playerbots     ║");
        TC_LOG_INFO("server.loading", "╚══════════════════════════════════════════════════════════╝");

        uint32 oldMSTime = getMSTime();

        TC_LOG_INFO("server.loading", " ");
        TC_LOG_INFO("server.loading", "Load Playerbots Config...");

        sPlayerbotAIConfig.Initialize();

        TC_LOG_INFO("server.loading", ">> Loaded playerbots config in {} ms", GetMSTimeDiffToNow(oldMSTime));
        TC_LOG_INFO("server.loading", " ");

        PlayerbotSpellRepository::Instance().Initialize();
        CheckMountStateAction::LoadPreferredMounts();

        TC_LOG_INFO("server.loading", "Loading gather node index...");
        GatherNodeMgr::instance().Load();

        TC_LOG_INFO("server.loading", "Playerbots World Thread Processor initialized");

        playerbotsWorldInitialized = true;
    }

    void OnUpdate(uint32 diff) override
    {
        if (!playerbotsWorldInitialized)
            return;

        PlayerbotHolder::UpdatePendingLogins();  // Headless sessions whose login holder is in flight

        // Session upkeep for the bots a real player owns: async query callbacks and pending teleport
        // acks, both world-thread only. This replaces the module-specific SessionScript::OnSessionUpdate
        // hook - the world tick already runs on the world thread after every map update.
        for (auto const& [accountId, session] : sWorld->GetAllSessions())
        {
            if (Player* player = session->GetPlayer())
                if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
                    playerbotMgr->UpdateSessions();
        }

        sRandomPlayerbotMgr.UpdateSessions();
        PlayerbotWorldThreadProcessor::instance().Update(diff);
        sRandomPlayerbotMgr.UpdateAI(diff);  // World thread only
    }

    // Runs before the sessions are kicked on server shutdown
    void OnShutdown() override
    {
        if (!playerbotsWorldInitialized)
            return;

        TC_LOG_INFO("playerbots", "Logging out all bots...");
        sRandomPlayerbotMgr.LogoutAllBots();
        PlayerbotHolder::ClearPendingLogins();
    }
};

void AddPlayerbotsSecureLoginScripts();
void AddPlayerbotsSelfBotAfkScripts();
void AddSC_randombot_level_mgr();

void AddPlayerbotsScripts()
{
    new PlayerbotsPlayerScript();
    new PlayerbotsServerScript();
    new PlayerbotsUnitScript();
    new PlayerbotsWorldScript();
    AddPlayerbotsCommandscripts();
    AddPlayerbotsSecureLoginScripts();
    AddPlayerbotsSelfBotAfkScripts();
    PlayerBotsGuildValidationScript();
    AddSC_randombot_level_mgr();
}
