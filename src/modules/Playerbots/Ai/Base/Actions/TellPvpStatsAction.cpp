/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TellPvpStatsAction.h"
#include "Event.h"
#include "Language.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include <map>

bool TellPvpStatsAction::Execute(Event event)
{
    if (!bot)
        return false;

    // Prefer the actual chat sender (whisper / say / etc.) if available.
    Player* requester = nullptr;

    if (Unit* owner = event.getOwner())
        requester = owner->ToPlayer();

    // Fallback to master if event owner is not available.
    if (!requester)
        requester = GetMaster();

    // If we still do not have a valid player to answer to, bail out.
    if (!requester)
        return false;

    // 1.60 has no arena points; classic honor lives in its own currency.
    std::map<std::string, std::string> currencyPlaceholders;
    currencyPlaceholders["%honor_points"] = std::to_string(bot->GetCurrencyQuantity(CURRENCY_HONOR_CLASSIC));

    std::string const currencyText = PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "pvp_currency", "[PVP] Honor Points: %honor_points", currencyPlaceholders);

    bot->Whisper(currencyText, LANG_UNIVERSAL, requester);

    return true;
}
