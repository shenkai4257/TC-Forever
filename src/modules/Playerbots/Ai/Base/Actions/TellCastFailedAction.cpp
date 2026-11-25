/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TellCastFailedAction.h"
#include "ChatHelper.h"
#include "Event.h"
#include "Playerbots.h"

bool TellSpellAction::Execute(Event event)
{
    std::string const spell = event.getParam();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!spellInfo)
        return false;

    std::ostringstream out;
    out << chat->FormatSpell(spellInfo);
    botAI->TellError(out.str());
    return true;
}
