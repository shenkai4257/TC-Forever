-- Playerbots module tables, merged into the core databases (2026-11-25).
-- Static data lives in `world`, per-character/account data in `characters`.
-- The module no longer has a database of its own: every statement in the code targets the
-- world or characters pool directly.

USE `world`;

CREATE TABLE IF NOT EXISTS `ai_playerbot_texts` (
  `id` smallint NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL COMMENT 'name - used in strategies/code as filter',
  `text` varchar(1024) NOT NULL COMMENT 'text',
  `say_type` tinyint NOT NULL DEFAULT '0' COMMENT '0 - say, 1 - yell',
  `reply_type` tinyint NOT NULL DEFAULT '0' COMMENT 'if > 0 then can be filtered as a response to chat',
  `text_loc1` varchar(1024) NOT NULL DEFAULT '',
  `text_loc2` varchar(1024) NOT NULL DEFAULT '',
  `text_loc3` varchar(1024) NOT NULL DEFAULT '',
  `text_loc4` varchar(1024) NOT NULL DEFAULT '',
  `text_loc5` varchar(1024) NOT NULL DEFAULT '',
  `text_loc6` varchar(1024) NOT NULL DEFAULT '',
  `text_loc7` varchar(1024) NOT NULL DEFAULT '',
  `text_loc8` varchar(1024) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=1973 DEFAULT CHARSET=utf8mb3 ROW_FORMAT=DYNAMIC
;

CREATE TABLE IF NOT EXISTS `ai_playerbot_texts_chance` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `probability` bigint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=246 DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_bis_gear` (
  `class` tinyint unsigned NOT NULL,
  `tab` tinyint unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `faction` tinyint unsigned NOT NULL DEFAULT '0',
  `auto_gear_score_limit` smallint unsigned NOT NULL,
  `item_id` int unsigned NOT NULL,
  `phase` varchar(32) NOT NULL DEFAULT '',
  `class_name` varchar(16) NOT NULL,
  `spec_name` varchar(32) NOT NULL,
  `slot_name` varchar(16) NOT NULL,
  `faction_name` varchar(8) NOT NULL DEFAULT 'Both',
  `item_name` varchar(96) NOT NULL,
  PRIMARY KEY (`class`,`tab`,`slot`,`faction`,`auto_gear_score_limit`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
;

CREATE TABLE IF NOT EXISTS `playerbots_dungeon_suggestion_abbrevation` (
  `id` tinyint NOT NULL AUTO_INCREMENT,
  `definition_slug` varchar(255) NOT NULL,
  `abbrevation` varchar(16) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `id` (`id`),
  KEY `definition_slug` (`definition_slug`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbot dungeon suggestion abbrevations'
;

CREATE TABLE IF NOT EXISTS `playerbots_dungeon_suggestion_definition` (
  `id` tinyint NOT NULL AUTO_INCREMENT,
  `slug` varchar(255) NOT NULL,
  `name` varchar(255) NOT NULL,
  `expansion` tinyint(1) NOT NULL,
  `difficulty` tinyint(1) NOT NULL,
  `min_level` tinyint DEFAULT NULL,
  `max_level` tinyint DEFAULT NULL,
  `comment` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `id` (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=89 DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbot dungeon suggestion definitions'
;

CREATE TABLE IF NOT EXISTS `playerbots_dungeon_suggestion_strategy` (
  `id` tinyint NOT NULL AUTO_INCREMENT,
  `definition_slug` varchar(255) NOT NULL,
  `strategy` varchar(255) NOT NULL,
  `difficulty` tinyint(1) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `id` (`id`),
  KEY `definition_slug` (`definition_slug`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbot dungeon suggestion strategies'
;

CREATE TABLE IF NOT EXISTS `playerbots_enchants` (
  `class` tinyint NOT NULL,
  `spec` tinyint NOT NULL,
  `spellid` int NOT NULL,
  `slotid` tinyint NOT NULL DEFAULT '1',
  `name` varchar(255) NOT NULL COMMENT 'name of the enchant',
  PRIMARY KEY (`class`,`spec`,`spellid`,`slotid`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1
;

CREATE TABLE IF NOT EXISTS `playerbots_equip_cache` (
  `id` int NOT NULL AUTO_INCREMENT,
  `clazz` tinyint NOT NULL,
  `lvl` int NOT NULL,
  `slot` tinyint NOT NULL,
  `quality` int NOT NULL,
  `item` int NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbots equip Cache'
;

CREATE TABLE IF NOT EXISTS `playerbots_item_info_cache` (
  `id` int NOT NULL,
  `quality` int DEFAULT NULL,
  `slot` int DEFAULT NULL,
  `source` bigint DEFAULT NULL,
  `sourceId` bigint DEFAULT NULL,
  `team` bigint DEFAULT NULL,
  `faction` bigint DEFAULT NULL,
  `factionRepRank` bigint DEFAULT NULL,
  `minLevel` bigint DEFAULT NULL,
  `scale_1` bigint DEFAULT NULL,
  `scale_2` bigint DEFAULT NULL,
  `scale_3` bigint DEFAULT NULL,
  `scale_4` bigint DEFAULT NULL,
  `scale_5` bigint DEFAULT NULL,
  `scale_6` bigint DEFAULT NULL,
  `scale_7` bigint DEFAULT NULL,
  `scale_8` bigint DEFAULT NULL,
  `scale_9` bigint DEFAULT NULL,
  `scale_10` bigint DEFAULT NULL,
  `scale_11` bigint DEFAULT NULL,
  `scale_12` bigint DEFAULT NULL,
  `scale_13` bigint DEFAULT NULL,
  `scale_14` bigint DEFAULT NULL,
  `scale_15` bigint DEFAULT NULL,
  `scale_16` bigint DEFAULT NULL,
  `scale_17` bigint DEFAULT NULL,
  `scale_18` bigint DEFAULT NULL,
  `scale_19` bigint DEFAULT NULL,
  `scale_20` bigint DEFAULT NULL,
  `scale_21` bigint DEFAULT NULL,
  `scale_22` bigint DEFAULT NULL,
  `scale_23` bigint DEFAULT NULL,
  `scale_24` bigint DEFAULT NULL,
  `scale_25` bigint DEFAULT NULL,
  `scale_26` bigint DEFAULT NULL,
  `scale_27` bigint DEFAULT NULL,
  `scale_28` bigint DEFAULT NULL,
  `scale_29` bigint DEFAULT NULL,
  `scale_30` bigint DEFAULT NULL,
  `scale_31` bigint DEFAULT NULL,
  `scale_32` bigint DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='PlayerbotAI item info Cache'
;

CREATE TABLE IF NOT EXISTS `playerbots_preferred_mounts` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guid` int NOT NULL,
  `type` tinyint NOT NULL COMMENT '0: Ground, 1: Flying',
  `spellid` int NOT NULL,
  PRIMARY KEY (`id`),
  KEY `guid` (`guid`),
  KEY `type` (`type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_rarity_cache` (
  `id` int NOT NULL AUTO_INCREMENT,
  `item` int NOT NULL,
  `rarity` float NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbots Rarity Cache'
;

CREATE TABLE IF NOT EXISTS `playerbots_rnditem_cache` (
  `id` int NOT NULL AUTO_INCREMENT,
  `lvl` int NOT NULL,
  `type` int NOT NULL,
  `item` int NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=3454 DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbots Random Item Cache'
;

CREATE TABLE IF NOT EXISTS `playerbots_tele_cache` (
  `id` int NOT NULL AUTO_INCREMENT,
  `level` tinyint NOT NULL,
  `map_id` mediumint NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='Playerbots Tele Cache'
;

CREATE TABLE IF NOT EXISTS `playerbots_travelnode` (
  `id` mediumint NOT NULL,
  `name` varchar(1024) NOT NULL,
  `map_id` mediumint NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `linked` tinyint DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='PlayerbotAI Travel Node'
;

CREATE TABLE IF NOT EXISTS `playerbots_travelnode_link` (
  `node_id` mediumint NOT NULL,
  `to_node_id` mediumint NOT NULL,
  `type` tinyint NOT NULL,
  `object` mediumint NOT NULL,
  `distance` float NOT NULL,
  `swim_distance` float NOT NULL,
  `extra_cost` float NOT NULL,
  `calculated` tinyint(1) NOT NULL,
  `max_creature_0` tinyint NOT NULL,
  `max_creature_1` tinyint NOT NULL,
  `max_creature_2` tinyint NOT NULL,
  PRIMARY KEY (`node_id`,`to_node_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='PlayerbotAI Travel Node link'
;

CREATE TABLE IF NOT EXISTS `playerbots_travelnode_path` (
  `node_id` mediumint NOT NULL,
  `to_node_id` mediumint NOT NULL,
  `nr` mediumint NOT NULL,
  `map_id` mediumint NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  PRIMARY KEY (`node_id`,`to_node_id`,`nr`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 ROW_FORMAT=FIXED COMMENT='PlayerbotAI Travel Node path'
;

CREATE TABLE IF NOT EXISTS `playerbots_weightscale_data` (
  `id` int NOT NULL,
  `field` varchar(18) NOT NULL,
  `val` smallint unsigned NOT NULL,
  KEY `id` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_weightscales` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(32) NOT NULL,
  `class` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=33 DEFAULT CHARSET=utf8mb3 ROW_FORMAT=COMPACT
;

USE `characters`;

CREATE TABLE IF NOT EXISTS `playerbots_random_bots` (
  `id` int NOT NULL AUTO_INCREMENT,
  `owner` int NOT NULL,
  `bot` int NOT NULL,
  `time` int NOT NULL,
  `validIn` int DEFAULT NULL,
  `event` varchar(45) DEFAULT NULL,
  `value` int DEFAULT NULL,
  `data` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `bot` (`bot`),
  KEY `event` (`event`)
) ENGINE=InnoDB AUTO_INCREMENT=154652 DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_guild_tasks` (
  `id` int NOT NULL AUTO_INCREMENT,
  `owner` int NOT NULL,
  `guildid` int NOT NULL,
  `time` int NOT NULL,
  `validIn` int DEFAULT NULL,
  `type` varchar(45) DEFAULT NULL,
  `value` int DEFAULT NULL,
  `data` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `guildid` (`guildid`),
  KEY `type` (`type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_account_type` (
  `account_id` int unsigned NOT NULL,
  `account_type` tinyint unsigned NOT NULL DEFAULT '0' COMMENT '0 = unassigned, 1 = RNDbot, 2 = AddClass',
  `assignment_date` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Playerbot account type assignments'
;

CREATE TABLE IF NOT EXISTS `playerbots_account_links` (
  `id` int NOT NULL AUTO_INCREMENT,
  `account_id` int NOT NULL,
  `linked_account_id` int NOT NULL,
  `created_at` timestamp NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `account_link` (`account_id`,`linked_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1
;

CREATE TABLE IF NOT EXISTS `playerbots_account_keys` (
  `account_id` int NOT NULL,
  `security_key` varchar(255) NOT NULL,
  `created_at` timestamp NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1
;

CREATE TABLE IF NOT EXISTS `playerbots_custom_strategy` (
  `id` int NOT NULL AUTO_INCREMENT,
  `name` varchar(255) NOT NULL,
  `idx` int DEFAULT NULL,
  `owner` int DEFAULT NULL,
  `action_line` varchar(1024) NOT NULL,
  PRIMARY KEY (`id`),
  KEY `name` (`name`)
) ENGINE=InnoDB AUTO_INCREMENT=8 DEFAULT CHARSET=utf8mb3
;

CREATE TABLE IF NOT EXISTS `playerbots_db_store` (
  `id` int NOT NULL AUTO_INCREMENT,
  `guid` int NOT NULL,
  `key` varchar(32) NOT NULL,
  `value` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3
;
