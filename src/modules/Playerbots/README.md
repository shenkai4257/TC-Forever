# Playerbots module — Classic 1.60 bots (M0 skeleton)

A player bot module for the Forever Classic realm, built into the worldserver
through the core module mechanism (`src/modules/README.md`): the core only
offers ScriptMgr hooks and this module registers the ones it needs.

This module is a from-scratch build, not a port. The architecture decisions
come from four sources of evidence; the planning documents live outside the
repo (in the operator's knowledge base):

- **Thread model**: AI never touches live world objects outside the world tick
  window. Today the module ticks from `WorldScript::OnUpdate`/`OnBeforeMapsUpdate`
  (world thread, after `MapUpdater::wait()` completed) and applies intents
  directly in that window; the `SnapshotView` / `IntentSink` interfaces are
  already shaped for the target architecture (value snapshots + an intent ring
  drained by a single world-side writer), so moving the AI to worker threads
  later is an implementation swap, not an AI-code rewrite.
- **Decision engine**: the AC playerbot lineage (Strategy/Action/Trigger/Value,
  co/nc strategy sets) with the gate pipeline (execution gate -> sentinel guard
  -> candidate frequency) instead of AC's "evaluate everything every tick".
- **Behaviour content**: translated semantics from AC mod-playerbots (Glossary/
  co/nc strategies) and from the operator's Legion port — not copied code.
  Classic is the 3.3.5 content set minus the WotLK layer, so the AC strategy
  vocabulary carries over.
- **Engineering policy**: `Core/Policy.h` (thread assertion, invariants,
  GUID-not-pointer for cross-tick references).

## M0 scope (current)

- Headless bot sessions (`BotSession` / `BotSessionMgr`): the module owns a
  parallel session table, never registers with `sWorld->AddSession()`. The
  forever mainline already provides every bot door the session needs
  (`isBot` ctor flag, `Playerbot::BotSession` friend, `LoginQueryHolder`
  public header, `Bots.SkipClientPackets` headless object updates).
- Commands: `.bot summon|dismiss|dismissall|info|perf|testcast`.
- Scripted GCD test: `.bot testcast <char> <spellId> [count]` casts the spell
  `count` times, one per GCD cycle (emit -> GCD wait -> wake -> emit). This
  exercises the full timing loop that a real rotation will use.
- Performance monitor (`Core/Perf.h`): per-phase tick costs (sessions / ai /
  intents / total), 1 s window averages, 60 s log line and `.bot perf`.

## Building

```
cmake -S . -B build -DBUILD_PLAYERBOTS=1
cmake --build build -j$(nproc) --target worldserver
```

The module is an option (`BUILD_PLAYERBOTS`, default OFF). Without it the
worldserver is byte-identical to the upstream build.

## Config

`conf/playerbots.conf.dist` is copied to `worldserver.conf.d/` at install;
rename it to `playerbots.conf` to activate.

## Layout

```
Playerbots.cpp        module entry: world script + loader (AddSC_playerbots_all)
Core/Policy.h         thread assertion + invariants
Core/Perf.*           performance monitor
Core/BotSession.*     headless WorldSession
Core/BotSessionMgr.*  parallel session table
AI/BotAI.*            per-bot brain (M0: heartbeat + scripted test cast)
AI/SnapshotView.h     read-only world view for AI code (M0: live facade)
AI/Intent.h           intent types + sink interface (M0: direct executor)
Commands/cs_playerbots.cpp   .bot commands
conf/playerbots.conf.dist
```
