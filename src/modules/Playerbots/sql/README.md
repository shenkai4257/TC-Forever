# Playerbots SQL

The module has no database of its own. Since 2026-11-25 all of its tables live in the two
databases the worldserver already maintains: static data in `world`, per-character/account
data in `characters`. The code addresses the two core connection pools directly, with no
database-name prefix, and nothing is created automatically at startup - the tables have to
exist before the module first runs.

## Where the tables live

| database | tables |
|---|---|
| `world` | `ai_playerbot_texts`, `ai_playerbot_texts_chance`, `playerbots_bis_gear`, `playerbots_dungeon_suggestion_abbrevation`, `playerbots_dungeon_suggestion_definition`, `playerbots_dungeon_suggestion_strategy`, `playerbots_enchants`, `playerbots_equip_cache`, `playerbots_item_info_cache`, `playerbots_preferred_mounts`, `playerbots_rarity_cache`, `playerbots_rnditem_cache`, `playerbots_rpg_races`, `playerbots_tele_cache`, `playerbots_travelnode`, `playerbots_travelnode_link`, `playerbots_travelnode_path`, `playerbots_weightscale_data`, `playerbots_weightscales` |
| `characters` | `playerbots_account_keys`, `playerbots_account_links`, `playerbots_account_type`, `playerbots_custom_strategy`, `playerbots_db_store`, `playerbots_guild_names`, `playerbots_guild_tasks`, `playerbots_names`, `playerbots_random_bots` |

## Fresh install

The fastest path is the published database baseline: release `db-20261126`
(`forever-playerbot-database-2026-11-26.7z`, ~163 MB). It contains a full `world` dump
(including all playerbots tables that live in `world`), a full `hotfixes` dump, and the
playerbots tables of `characters` - no `auth` data and no player characters/accounts.
After core creates the four databases, import each file into its database:

```bash
mysql -h127.0.0.1 -P3317 -utrinity world      < world.sql
mysql -h127.0.0.1 -P3317 -utrinity hotfixes   < hotfixes.sql
mysql -h127.0.0.1 -P3317 -utrinity characters < characters_playerbots.sql
```

Otherwise build the tables and data from the files in this directory:

1. Start the worldserver once so that core creates `world` and `characters`.
2. Create the module tables:

   ```bash
   mysql -h127.0.0.1 -P3317 -utrinity \
       < playerbots/2026_11_25_00_merge_into_core_databases.sql
   ```

   The file switches between `USE world` and `USE characters` itself.

3. Load the data. The most reliable way is to dump the tables from a running installation,
   because the data has been patched by every update below:

   ```bash
   mysqldump -h127.0.0.1 -P3317 -utrinity --no-create-info \
       world <world tables from the table above> \
       | mysql -h127.0.0.1 -P3317 -utrinity world
   mysqldump -h127.0.0.1 -P3317 -utrinity --no-create-info \
       characters <characters tables from the table above> \
       | mysql -h127.0.0.1 -P3317 -utrinity characters
   ```

   Alternatively replay the upstream baseline: `playerbots/base/*.sql`, then
   `playerbots/updates/*.sql` in filename order, then `world/base`, `world/updates`,
   `characters/base` and `characters/updates`. Import every file with `-D world` or
   `-D characters` depending on which database owns its tables - nearly all update files
   touch only `ai_playerbot_texts*` (world), while `playerbots_random_bots`,
   `playerbots_account_*` and `playerbots_guild_tasks` are in `characters`.

   Note that the base files start with `DROP TABLE IF EXISTS`; do not run them over a
   database whose module data you want to keep.

4. The 67 MB travel-node path dump (`playerbots/base/playerbots_travelnode_path.sql`) is
   **not** kept in git. Download `playerbots-db-base.7z` from the `db-base-20261124`
   release asset and import it into `world` once:

   ```bash
   7z x playerbots-db-base.7z
   mysql -h127.0.0.1 -P3317 -utrinity -D world < playerbots_travelnode_path.sql
   ```

## Obsolete files

These belong to the old standalone-database + updater setup and must not be executed any
more (they are kept only for history):

- `playerbots/create/` - created the old standalone `acore_playerbots` database.
- `playerbots/base/updates.sql`, `playerbots/base/updates_include.sql`,
  `playerbots/base/version_db_playerbots.sql` - bookkeeping tables of the old updater.
- `playerbots/updates/2024_11_25_00.sql` - rewrites `updates_include` paths.
- `playerbots/updates/2025_05_05_00_accountlinking.sql` - creates the pre-rename
  `playerbot_account_*` tables (single `playerbot`).
- `playerbots/archive/`, `playerbots/custom/` - upstream updater layout, unused here.

The module SQL is not registered with the core updater: apply new scripts by hand.

## Adding a new SQL change

Put a dated `YYYY_MM_DD_NN_description.sql` file in `playerbots/updates/` (never edit a
file that is already applied) and address the tables without a database prefix, exactly as
the code does. Import it into the database that owns the affected tables, or add
`USE world;` / `USE characters;` at the top of the file so it can be applied as-is.

Core-level SQL changes (world/hotfixes) follow the usual `sql/custom/` and `sql/updates/`
layout at the repository root.
