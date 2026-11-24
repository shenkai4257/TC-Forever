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
#include "PlayerbotsDatabase.h"
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
#include "cmath"

namespace
{
    // Set once the world initialization completed; every periodic hook short-circuits
    // when it is false so a missing module database disables the module instead of
    // letting later ticks dereference uninitialized managers.
    bool playerbotsWorldInitialized = false;
}

class PlayerbotsDatabaseScript : public DatabaseScript
{
public:
    PlayerbotsDatabaseScript() : DatabaseScript("PlayerbotsDatabaseScript") {}

    bool OnModuleDatabasesLoading() override
    {
        // Fires from StartDB() before the module script loader has registered this
        // script on 12.x, so it normally never reaches us; the world script opens
        // the pool instead. Kept idempotent for cores that run it after loading.
        return OpenPlayerbotsDatabase();
    }

    void OnModuleDatabasesKeepAlive() override { PlayerbotsDatabase.KeepAlive(); }

    void OnModuleDatabasesClosing() override { PlayerbotsDatabase.Close(); }

    void OnDatabaseWarnAboutSyncQueries(bool apply) override { PlayerbotsDatabase.WarnAboutSyncQueries(apply); }

    void OnDatabaseGetDBRevision(std::map<std::string, std::string>& revisions) override
    {
        std::string revision;
        if (QueryResult resultPlayerbot =
                PlayerbotsDatabase.PQuery("SELECT date FROM version_db_playerbots ORDER BY date DESC LIMIT 1"))
        {
            Field* fields = resultPlayerbot->Fetch();
            revision = PlayerbotFieldGet<std::string>(fields[0]);
        }

        if (revision.empty())
            revision = "Unknown Playerbots Database Revision";

        revisions["Playerbots"] = revision;
    }
};

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

    void OnPlayerBeforeLogout(Player* player) override
    {
        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
        {
            PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

            if (botAI == nullptr || IsSelfBot(player))
                playerbotMgr->LogoutAllBots();
        }

        sRandomPlayerbotMgr.OnPlayerLogout(player);
    }

    void OnCreatureKillCredit(Player* player, Creature* killed) override
    {
        GuildTaskMgr::instance().CheckKillTask(player, killed);
    }

    void OnBeforePetitionSign(Player* player, ObjectGuid /*petitionGuid*/, bool& alreadySignedByAccount) override
    {
        if (!alreadySignedByAccount)
            return;

        // bots share accounts, so the same-account rule must not stop them from signing each other
        if (PlayerbotsMgr::instance().GetPlayerbotAI(player) != nullptr)
            alreadySignedByAccount = false;
    }

    void OnAfterUpdate(Player* player, uint32 diff) override
    {
        PlayerbotAI* const botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI != nullptr)
        {
            botAI->UpdateAI(diff);
        }

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
        {
            playerbotMgr->UpdateAI(diff);
        }
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

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
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

class PlayerbotsMiscScript : public MiscScript
{
public:
    PlayerbotsMiscScript() : MiscScript("PlayerbotsMiscScript") {}

    void OnDestructPlayer(Player* player) override
    {
        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);

        if (botAI != nullptr)
            delete botAI;

        if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
            delete playerbotMgr;
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

class PlayerbotsSessionScript : public SessionScript
{
public:
    PlayerbotsSessionScript() : SessionScript("PlayerbotsSessionScript") {}

    // Runs on the world thread for every real session: drains the packet queues of the bots it owns
    void OnSessionUpdate(WorldSession* session, uint32 /*diff*/) override
    {
        if (Player* player = session->GetPlayer())
            if (PlayerbotMgr* playerbotMgr = GET_PLAYERBOT_MGR(player))
                playerbotMgr->UpdateSessions();
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

        // 12.x registers scripts only from World::SetInitialWorldSettings(), after
        // StartDB() already fired the DatabaseScript hook, so open the module pool
        // here before anything touches it.
        if (!OpenPlayerbotsDatabase())
        {
            TC_LOG_ERROR("server.playerbots", "Playerbots module stays disabled: its database is unavailable.");
            return;
        }

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


        // TEMP 1.60 content audit (remove once the class skill list is verified):
        // report which hardcoded class spell ids the 1.60 client data does not have.
        struct ClassSpellAuditEntry { char const* name; uint32 id; };
        static constexpr ClassSpellAuditEntry kClassSpellAudit[] =
        {
            { "SPELL_DIVINE_SHIELD", 642 },
            { "SPELL_CONJURE_MANA_AGATE", 759 },
            { "SPELL_CAT_FORM", 768 },
            { "SPELL_HAND_OF_PROTECTION", 1022 },
            { "SPELL_HAND_OF_SALVATION", 1038 },
            { "SPELL_HAND_OF_FREEDOM", 1044 },
            { "SPELL_STEALTH", 1784 },
            { "SPELL_SPRINT_RANK_1", 2983 },
            { "SPELL_CONJURE_MANA_JADE", 3552 },
            { "SPELL_WRATH_OF_AIR_TOTEM", 3738 },
            { "SPELL_MANA_SPRING_TOTEM", 5675 },
            { "SPELL_RAVAGE_RANK_1", 6785 },
            { "SPELL_HAND_OF_SACRIFICE", 6940 },
            { "SPELL_CLEANSING_TOTEM", 8170 },
            { "SPELL_GROUNDING_TOTEM", 8177 },
            { "SPELL_FLAMETONGUE_TOTEM", 8227 },
            { "SPELL_WINDFURY_TOTEM", 8512 },
            { "SPELL_POUNCE_RANK_1", 9005 },
            { "SPELL_CONJURE_MANA_CITRINE", 10053 },
            { "SPELL_CONJURE_MANA_RUBY", 10054 },
            { "SPELL_ICE_SHARDS", 15047 },
            { "SPELL_BLESSING_OF_MIGHT", 19740 },
            { "SPELL_BLESSING_OF_WISDOM", 19742 },
            { "SPELL_IMPROVED_MIGHT_R1", 20042 },
            { "SPELL_IMPROVED_MIGHT_R2", 20045 },
            { "SPELL_BLESSING_OF_KINGS", 20217 },
            { "SPELL_RETALIATION", 20230 },
            { "SPELL_IMPROVED_WISDOM_R1", 20244 },
            { "SPELL_IMPROVED_WISDOM_R2", 20245 },
            { "SPELL_BLESSING_OF_SANCTUARY", 20911 },
            { "SPELL_GREATER_BLESSING_OF_MIGHT", 25782 },
            { "SPELL_GREATER_BLESSING_OF_WISDOM", 25894 },
            { "SPELL_GREATER_BLESSING_OF_KINGS", 25898 },
            { "SPELL_GREATER_BLESSING_OF_SANCTUARY", 25899 },
            { "SPELL_CONJURE_MANA_EMERALD", 27101 },
            { "SPELL_TOTEM_OF_WRATH", 30706 },
            { "SPELL_TREE_OF_LIFE", 33891 },
            { "SPELL_BLESSING_OF_PROTECTION", 41450 },
            { "SPELL_CONJURE_MANA_SAPPHIRE", 42985 },
            { "SPELL_DEEP_FREEZE", 44572 },
            { "SPELL_FROSTFIRE_BOLT", 44614 },
            { "SPELL_ICE_BLOCK", 45438 },
            { "SPELL_ECLIPSE_SOLAR", 48517 },
            { "SPELL_ECLIPSE_LUNAR", 48518 },
            { "SPELL_VIGILANCE", 50720 },
            { "SPELL_FOCUS_MAGIC", 54646 },
            { "SPELL_RENEWED_HOPE_R1", 57470 },
            { "SPELL_RENEWED_HOPE_R2", 57472 },
            { "SPELL_MASTER_POISONER_RANK_3", 58410 },
            { "SPELL_GLYPH_OF_BLAST_WAVE", 62126 },
            { "SPELL_GLYPH_OF_THUNDERSTORM", 62132 },
            { "SPELL_GLYPH_OF_TYPHOON", 62135 },
            { "SPELL_LIFE_TAP_GLYPH", 63320 },
            { "SPELL_SHATTERING_THROW", 64382 },
        };
        for (ClassSpellAuditEntry const& e : kClassSpellAudit)
        {
            SpellInfo const* si = sSpellMgr->GetSpellInfo(e.id, DIFFICULTY_NONE);
            if (!si)
                TC_LOG_WARN("playerbots", "[SpellAudit] MISSING {} = {}", e.name, e.id);
            else
                TC_LOG_INFO("playerbots", "[SpellAudit] OK {} = {} name={}", e.name, e.id,
                    si->SpellName->Str[sWorld->GetDefaultDbcLocale()]);
        }

        playerbotsWorldInitialized = true;
    }

    void OnUpdate(uint32 diff) override
    {
        if (!playerbotsWorldInitialized)
            return;

        PlayerbotHolder::UpdatePendingLogins();  // Headless sessions whose login holder is in flight
        sRandomPlayerbotMgr.UpdateSessions();  // Per-bot packet queues, world thread only
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
    new PlayerbotsDatabaseScript();
    new PlayerbotsPlayerScript();
    new PlayerbotsMiscScript();
    new PlayerbotsServerScript();
    new PlayerbotsSessionScript();
    new PlayerbotsWorldScript();
    AddPlayerbotsCommandscripts();
    AddPlayerbotsSecureLoginScripts();
    AddPlayerbotsSelfBotAfkScripts();
    PlayerBotsGuildValidationScript();
    AddSC_randombot_level_mgr();
}
