/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_CHANGETALENTSACTION_H
#define PLAYERBOTS_CHANGETALENTSACTION_H

#include "Action.h"

class PlayerbotAI;

// The 1.60 talent command ("talents" chat trigger): applies one of the class's classic talent builds
// (Api/ClassicTalentBuilds) through the ported V2 walk, or an explicit Wowhead talent calculator code.
// The 3.3.5 Talentspec stack (premade links, dual spec) does not exist in 1.60 and is gone.
class ChangeTalentsAction : public Action
{
public:
    ChangeTalentsAction(PlayerbotAI* botAI, std::string const name = "talents") : Action(botAI, name) {}

    bool Execute(Event event) override;

private:
    std::string SpecList();
    std::string ApplySpec(uint32 specNo);
};

class AutoSetTalentsAction : public ChangeTalentsAction
{
public:
    AutoSetTalentsAction(PlayerbotAI* botAI) : ChangeTalentsAction(botAI, "auto talents") {}

    bool Execute(Event event) override;
};

#endif
