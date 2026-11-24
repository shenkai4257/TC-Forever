/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ChangeTalentsAction.h"
#include "AiFactory.h"
#include "AiObjectContext.h"
#include "ChatHelper.h"
#include "ClassicSpec.h"
#include "ClassicTalentBuilds.h"
#include "ClassicTalentsApply.h"
#include "DB2Stores.h"
#include "Event.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

using namespace ::Playerbot::Classic;

static std::string SpecName(uint32 spec)
{
    if (ChrSpecializationEntry const* entry = sChrSpecializationStore.LookupEntry(spec))
        return std::string(entry->Name[DEFAULT_LOCALE]);
    return std::to_string(spec);
}

static std::vector<uint32> SpecsForClass(uint8 cls)
{
    std::vector<uint32> specs;
    for (TalentBuild const& build : GetTalentBuilds())
        if (build.cls == cls && std::find(specs.begin(), specs.end(), build.spec) == specs.end())
            specs.push_back(build.spec);
    return specs;
}

std::string ChangeTalentsAction::SpecList()
{
    std::vector<uint32> const specs = SpecsForClass(bot->GetClass());
    std::ostringstream out;
    if (specs.empty())
    {
        out << "No classic talent builds for my class.";
        return out.str();
    }

    for (size_t i = 0; i < specs.size(); ++i)
        out << (i + 1) << ". " << SpecName(specs[i]) << "\n";
    out << "Usage: talents spec <number>, talents auto, talents apply <link>";
    return out.str();
}

std::string ChangeTalentsAction::ApplySpec(uint32 specNo)
{
    std::vector<uint32> const specs = SpecsForClass(bot->GetClass());
    if (specNo == 0 || specNo > specs.size())
    {
        std::ostringstream out;
        out << "No such spec (1-" << specs.size() << ").";
        return out.str();
    }

    uint32 const spec = specs[specNo - 1];
    SetSpecOverride(bot, spec);
    TalentsApplyResult const result = ApplyClassicTalents(bot, /*force*/ true);
    std::ostringstream out;
    out << "Switching to " << SpecName(spec) << ": "
        << (result == TalentsApplyResult::Ok ? "talents set" : "not applied now, ask me again");
    botAI->ResetStrategies();
    return out.str();
}

bool ChangeTalentsAction::Execute(Event event)
{
    std::string const param = event.getParam();
    std::ostringstream out;

    if (param.empty())
    {
        out << "My spec is " << SpecName(EffectiveSpecId(bot)) << ". " << SpecList();
    }
    else if (param.find("help") != std::string::npos)
    {
        out << SpecList();
    }
    else if (param.find("autopick") != std::string::npos || param.find("auto") != std::string::npos)
    {
        TalentsApplyResult const result = ApplyClassicTalents(bot, /*force*/ true);
        out << (result == TalentsApplyResult::Ok ? "Talents set" : "Talents not applied now, ask me again");
        botAI->ResetStrategies();
    }
    else if (param.find("spec list") != std::string::npos)
    {
        out << SpecList();
    }
    else if (param.rfind("spec ", 0) == 0 || param.rfind("setspec ", 0) == 0)
    {
        out << ApplySpec(atol(param.c_str() + param.find(' ') + 1));
    }
    else if (param.find("apply ") != std::string::npos)
    {
        std::string const code = param.substr(6);
        // Strip a Wowhead link down to its code if the master pasted a URL.
        size_t const slash = code.rfind('/');
        std::string const tail = slash != std::string::npos ? code.substr(slash + 1) : code;
        TalentsApplyResult const result = ApplyTalentCode(bot, tail);
        switch (result)
        {
            case TalentsApplyResult::Ok:
                out << "Talent code applied";
                botAI->ResetStrategies();
                break;
            case TalentsApplyResult::Locked:
                out << "I am busy, ask me again";
                break;
            case TalentsApplyResult::InvalidTarget:
                out << "This talent code does not fit my class";
                break;
            default:
                out << "I cannot read this talent code";
                break;
        }
    }
    else
    {
        // A bare Wowhead code or link.
        TalentsApplyResult const result = ApplyTalentCode(bot, param);
        if (result != TalentsApplyResult::NotKnown)
        {
            out << (result == TalentsApplyResult::Ok ? "Talent code applied"
                    : result == TalentsApplyResult::Locked ? "I am busy, ask me again"
                    : result == TalentsApplyResult::InvalidTarget ? "This talent code does not fit my class"
                                                                 : "I cannot read this talent code");
            if (result == TalentsApplyResult::Ok)
                botAI->ResetStrategies();
        }
        else
            out << "Unknown command. " << SpecList();
    }

    botAI->TellMaster(out);
    return true;
}

bool AutoSetTalentsAction::Execute(Event /*event*/)
{
    TalentsApplyResult const result = ApplyClassicTalents(bot, /*force*/ true);
    return result == TalentsApplyResult::Ok;
}
