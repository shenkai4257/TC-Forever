# Playerbots module - player bots for the Classic 1.60 realm.
#
# Optional: configured and built only with -DBUILD_PLAYERBOTS=1.
# Sources are the ported mod-playerbots (AC) module: engine under Ai/Base,
# content under Ai/{Class,Raid,World}, infrastructure under {Bot,Db,Mgr,Script,Util}.

option(BUILD_PLAYERBOTS "Build the Playerbots module" 0)
if(NOT BUILD_PLAYERBOTS)
  return()
endif()

message(STATUS "Playerbots module enabled")

# The README's CollectSourceFiles() macro does not exist in this tree; glob directly.
file(GLOB_RECURSE PRIVATE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# Battlegrounds and arena are not part of the module (the battleground layer is to be rewritten).
# Talentspec: the 3.3.5 three-tree talent stack (premade spec links) — not portable to the 1.6x
# trait-config system. ChangeTalentsAction was rewritten for the classic talent walk instead.
list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "Talentspec\\.(cpp|h)$")

add_library(playerbots-module STATIC ${PRIVATE_SOURCES})

target_link_libraries(playerbots-module
  PRIVATE
    trinity-core-interface
    mysql
  PUBLIC
    game-interface)

# The ported module includes by bare file name ("Action.h", "MageActions.h"),
# so every source directory is on the include path, like AC's module system does.
file(GLOB_RECURSE _module_headers
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(_module_include_dirs)
foreach(_header ${_module_headers})
  get_filename_component(_dir "${_header}" DIRECTORY)
  list(APPEND _module_include_dirs "${_dir}")
endforeach()
list(REMOVE_DUPLICATES _module_include_dirs)

target_include_directories(playerbots-module
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_module_include_dirs})

# The ported module was written against AC's fat module PCH; source files there
# rely on WorldSession/Log/etc. being visible transitively. Rebuild that union here.
target_precompile_headers(playerbots-module PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/Playerbots.h"
  <WorldSession.h>
  <World.h>
  <Log.h>
  <ObjectMgr.h>
  <ObjectAccessor.h>
  <ScriptMgr.h>
  <DatabaseEnv.h>
  <DB2Stores.h>
  <Loot.h>
  <LootMgr.h>
  <Battleground.h>
  <BattlegroundMgr.h>
  <ArenaTeam.h>
  <Guild.h>
  <GameTime.h>
  <Timer.h>
  <CellImpl.h>
  <GridDefines.h>
  <GridNotifiers.h>
  <GridNotifiersImpl.h>
  <Creature.h>
  <GameObject.h>
  <Item.h>
  <MapManager.h>
  <QuestDef.h>
  <MotionMaster.h>
  <PathGenerator.h>
  <Chat.h>
  <PlayerbotAI.h>)

trinity_add_module(playerbots-module Addmod_playerbotsScripts
  CONFIGS
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/playerbots.conf.dist)
