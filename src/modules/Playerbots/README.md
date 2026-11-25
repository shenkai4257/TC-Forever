# Playerbots Module (ForeverCore)

Player bots for the WoW Forever realm (Classic 1.60.1), built into
[ForeverCore](https://github.com/Karrz1/ForeverCore) (a TrinityCore 12.x fork)
through the core module mechanism.

## Origin

This module is **derived from `mod-playerbots` by liyunfan1223**
(https://github.com/mod-playerbots/mod-playerbots), an AzerothCore module that
itself continues IKE3's [mangosbot](https://github.com/ike3/mangosbot) project.

It has been ported to TrinityCore 12.x and adapted to the WoW Forever client,
with substantial modifications: retail-era features that do not exist in
Classic 1.60 are removed, packet handling is rewritten for the modern client
formats, and new systems are added where the underlying core differs from
AzerothCore. All credit for the original architecture and code belongs to the
upstream authors; the upstream project remains the authoritative source for
anything not specific to the Forever client.

## Purpose

This repository is published **for study, research and private-server use
only**. It is not a commercial product and is not affiliated with, endorsed by,
or connected to Blizzard Entertainment in any way.

## Disclaimer / No Warranty

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED. The maintainers of this fork:

- are **not responsible** for any damage, data loss, account action or any
  other consequence arising from the use or misuse of this code;
- do **not** provide any support, uptime or compatibility guarantees;
- are **not** the maintainers of the underlying core (ForeverCore) — issues
  located in the core itself belong to its own project and should be reported
  there.

World of Warcraft is a trademark of Blizzard Entertainment. All game content,
names and related assets are the property of their respective owners. This
module contains no game assets.

## License

Distributed under the **GNU General Public License v2**, the same license as
the upstream playerbots module and TrinityCore. The original copyright notices
of mod-playerbots and TrinityCore apply to the code inherited from them.

## Layout

```
Ai/     strategies, triggers, actions, values (Base / Class / World)
Bot/    per-bot AI core, random bot factory, outgoing-packet handling
Mgr/    travel manager, security and shard helpers
Api/    integration points with the core
Db/     module database repository
Script/ core script hooks
conf/   playerbots.conf.dist
sql/    module SQL (base tables + updates; see sql/README.md)
```

## Building

The module is optional (`BUILD_PLAYERBOTS`, default OFF); without it the
worldserver builds byte-identical to the upstream core.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSERVERS=1 -DBUILD_PLAYERBOTS=1
cmake --build build -j$(nproc) --target worldserver
```

## 中文说明

- 本模块基于 liyunfan1223 的 playerbots 模块(mod-playerbots,
  https://github.com/mod-playerbots/mod-playerbots)修改移植而来,用于
  TrinityCore 12.x(ForeverCore)与 WoW Forever(经典 1.60)客户端。
- 本仓库仅供学习与研究及私服用途,与暴雪娱乐无关,亦不代表任何官方项目。
- 代码按原上游许可(GNU GPL v2)发布,原项目版权声明对继承代码继续有效。
- 本软件按"现状"提供,不附带任何明示或默示的保证;使用本代码的一切风险与
  后果由使用者自行承担,本仓库维护者不承担任何责任,亦不提供任何形式的
  支持与保证。核心(ForeverCore)本身的问题请向其所属项目反馈。
