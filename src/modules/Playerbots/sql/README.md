# Playerbots SQL

The database assets are split the same way TrinityCore splits its own:

- `base/` - schema and the initial data set. The large travel-node table
  (`playerbots/base/playerbots_travelnode_path.sql`, 67 MB) is **not** kept in git.
  Download it from the `db-base-20261124` release asset and import it once:

  ```bash
  7z x playerbots-db-base.7z
  mysql -h127.0.0.1 -P3317 -utrinity playerbot < playerbots_travelnode_path.sql
  ```

- `updates/` - dated, small changes applied after the baseline. Add a new
  `YYYY_MM_DD_NN_description.sql` file here for every schema or data change;
  never edit an update that is already published.
- `create/` - database creation statements.

Core-level SQL changes (world/hotfixes) follow the usual `sql/custom/` and
`sql/updates/` layout at the repository root.
