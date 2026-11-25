-- Obsolete: this file created the old standalone playerbot database.
-- The module tables now live in the world/characters databases. See sql/README.md.

REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'acore'@'localhost';

DROP USER 'acore'@'localhost';

DROP DATABASE IF EXISTS `acore_playerbots`;
