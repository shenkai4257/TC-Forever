-- Obsolete after the 2026-11-25 merge into the core databases. See sql/README.md.

UPDATE `updates_include`
SET `path` = '$/data/sql/playerbots/updates'
WHERE `state` = 'RELEASED';

UPDATE `updates_include`
SET `path` = '$/data/sql/playerbots/custom'
WHERE `state` = 'CUSTOM';

UPDATE `updates_include`
SET `path` = '$/data/sql/playerbots/archive'
WHERE `state` = 'ARCHIVED';
