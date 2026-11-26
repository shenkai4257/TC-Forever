/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "DatabaseEnv.h"
#include "GridDefines.h"
#include "GuildTaskMgr.h"
#include "Map.h"
#include "MapManager.h"
#include "PerfMonitor.h"
#include "PhasingHandler.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RBAC.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace Trinity::ChatCommands;

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") {}

    // The 3.3.5 table declared security levels (SEC_PLAYER / SEC_GAMEMASTER). A 12.x command carries an RBAC
    // permission and the session only checks the permissions the account was granted, so the two command roles
    // stand in for the levels: every account is granted the player command role, a game master and up the
    // game master command role.
    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable playerbotsAccountCommandTable = {
            {"setKey", HandleSetSecurityKeyCommand, rbac::RBAC_ROLE_PLAYER, Console::No},
            {"link", HandleLinkAccountCommand, rbac::RBAC_ROLE_PLAYER, Console::No},
            {"linkedAccounts", HandleViewLinkedAccountsCommand, rbac::RBAC_ROLE_PLAYER, Console::No},
            {"unlink", HandleUnlinkAccountCommand, rbac::RBAC_ROLE_PLAYER, Console::No},
        };

        static ChatCommandTable playerbotsCommandTable = {
            {"bot", HandlePlayerbotCommand, rbac::RBAC_ROLE_PLAYER, Console::No},
            {"exec", HandleExecCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"inspect", HandleInspectCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"gtask", HandleGuildTaskCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"pmon", HandlePerfMonCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"spawnimport", HandleSpawnImportCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"rndbot", HandleRandomPlayerbotCommand, rbac::RBAC_ROLE_GAMEMASTER, Console::Yes},
            {"account", playerbotsAccountCommandTable},
        };

        static ChatCommandTable commandTable = {
            {"playerbots", playerbotsCommandTable},
        };

        return commandTable;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, args);
    }

    // Sends its own error so the core does not append its generic help text.
    static PlayerbotAI* GetOnlineBotAI(ChatHandler* handler, PlayerIdentifier const& target)
    {
        Player* player = target.GetConnectedPlayer();
        PlayerbotAI* botAI = player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
        if (!botAI)
            handler->SendSysMessage(Trinity::StringFormat("{} is not an online bot", target.GetName()));
        return botAI;
    }

    // From the console or SOAP there is no player object to send the command, so the bot sends it to itself.
    // The bot's security check always lets a bot command itself.
    static bool HandleExecCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        if (command.empty())
        {
            handler->SendSysMessage("Usage: .playerbots exec <bot> <command> (state-changing commands; "
                                    "query replies go to the bot's master - use inspect)");
            return false;
        }

        Player* from = handler->GetSession() ? handler->GetSession()->GetPlayer() : botAI->GetBot();
        botAI->HandleCommand(CHAT_MSG_WHISPER, std::string(command), from);
        handler->SendSysMessage(Trinity::StringFormat("{}: '{}' sent", target.GetName(), std::string(command)));
        return true;
    }

    static bool HandleInspectCommand(ChatHandler* handler, PlayerIdentifier target, Tail command)
    {
        PlayerbotAI* botAI = GetOnlineBotAI(handler, target);
        if (!botAI)
            return false;

        if (command.empty())
        {
            handler->SendSysMessage("Usage: .playerbots inspect <bot> <query> (state, position, tpos, "
                                    "movement, target, hp, strategy, action, values, travel, budget)");
            return false;
        }

        std::string const result = botAI->HandleRemoteCommand(std::string(command));
        handler->SendSysMessage(Trinity::StringFormat("{}: {}", target.GetName(), result.empty() ? "<empty>" : result));
        return true;
    }

    static bool HandleRandomPlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, args);
    }

    static bool HandleGuildTaskCommand(ChatHandler* handler, char const* args)
    {
        return GuildTaskMgr::HandleConsoleCommand(handler, args);
    }

    static bool HandlePerfMonCommand(ChatHandler* /*handler*/, char const* args)
    {
        if (!strcmp(args, "reset"))
        {
            sPerfMonitor.Reset();
            return true;
        }

        if (!strcmp(args, "tick"))
        {
            sPerfMonitor.PrintStats(true, false);
            sPerfMonitor.DumpJson(true);
            return true;
        }

        if (!strcmp(args, "stack"))
        {
            sPerfMonitor.PrintStats(false, true);
            sPerfMonitor.DumpJson(false);
            return true;
        }

        if (!strcmp(args, "toggle"))
        {
            sPlayerbotAIConfig.PerfMonEnabled = !sPlayerbotAIConfig.PerfMonEnabled;
            if (sPlayerbotAIConfig.PerfMonEnabled)
                TC_LOG_INFO("playerbots", "Performance monitor enabled");
            else
                TC_LOG_INFO("playerbots", "Performance monitor disabled");
            return true;
        }

        sPerfMonitor.PrintStats();
        sPerfMonitor.DumpJson(false);
        return true;
    }

    // One-shot import of the Forever community spawn points (forever-db spm/spw segments).
    // Those carry planar coordinates only (a Questie-style addon needs no more), so the ground
    // height is resolved here through vmaps before the creature row is written. File format,
    // one point per line: entry<TAB>map<TAB>areaId<TAB>x<TAB>y
    // Guids start at 930000000, above the compacted range (max 920014001), so a re-run inserts a
    // second copy - check world.creature first when importing a newer snapshot.
    static bool HandleSpawnImportCommand(ChatHandler* handler, char const* args)
    {
        std::string const path = (args && *args) ? args : "/home/shenk/tools/tmp/spawn_import.tsv";
        std::ifstream in(path);
        if (!in)
        {
            handler->SendSysMessage(Trinity::StringFormat("spawn import: cannot open {}", path));
            return false;
        }

        PhaseShift const& phase = PhasingHandler::GetEmptyPhaseShift();
        std::unordered_map<uint32, Map*> maps;
        uint32 inserted = 0, noGround = 0, badLine = 0;
        uint64 guid = 930000000;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream fields(line);
            uint32 entry = 0, mapId = 0, areaId = 0;
            float x = 0.0f, y = 0.0f;
            if (!(fields >> entry >> mapId >> areaId >> x >> y))
            {
                ++badLine;
                continue;
            }

            auto [itr, created] = maps.try_emplace(mapId, nullptr);
            if (created)
                itr->second = sMapMgr->PrewarmContinentMap(mapId);
            Map* map = itr->second;
            if (!map)
            {
                ++badLine;
                continue;
            }

            float const z = map->GetHeight(phase, x, y, MAX_HEIGHT, true);
            if (z <= INVALID_HEIGHT + 1.0f)
            {
                ++noGround;
                continue;
            }

            WorldDatabase.PExecute(
                "INSERT INTO creature (guid, id, map, zoneId, areaId, position_x, position_y, position_z, orientation, spawntimesecs)"
                " VALUES ({}, {}, {}, 0, {}, {}, {}, {}, {}, 300)",
                guid++, entry, mapId, areaId, x, y, z, float(urand(0, 628)) / 100.0f);
            ++inserted;
        }

        handler->SendSysMessage(Trinity::StringFormat(
            "spawn import: inserted {}, no ground found {}, bad lines {} (from {})", inserted, noGround, badLine, path));
        return true;
    }


    static bool HandleSetSecurityKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots account setKey <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string key = args;

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleSetSecurityKeyCommand(player, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleLinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        char* key = strtok(nullptr, " ");

        if (!accountName || !key)
        {
            handler->PSendSysMessage("Usage: .playerbots account link <accountName> <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleLinkAccountCommand(player, accountName, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleViewLinkedAccountsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleViewLinkedAccountsCommand(player);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleUnlinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        if (!accountName)
        {
            handler->PSendSysMessage("Usage: .playerbots account unlink <accountName>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleUnlinkAccountCommand(player, accountName);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }
};

void AddPlayerbotsCommandscripts() { new playerbots_commandscript(); }
