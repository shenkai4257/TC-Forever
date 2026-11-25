-- 1.60 has no arena: the arena team lines of ai_playerbot_texts are unused, and the currency
-- line keeps its honour part only (the base file carries the same change).
DELETE FROM `ai_playerbot_texts` WHERE `name` IN ('pvp_arena_team', 'pvp_no_arena_team');
DELETE FROM `ai_playerbot_texts_chance` WHERE `name` IN ('pvp_arena_team', 'pvp_no_arena_team');

UPDATE `ai_playerbot_texts` SET
  `text` = CONCAT('[PVP] ', SUBSTRING(`text`, LOCATE('| ', `text`) + 2)),
  `text_loc1` = CONCAT('[PVP] ', SUBSTRING(`text_loc1`, LOCATE('| ', `text_loc1`) + 2)),
  `text_loc2` = CONCAT('[PVP] ', SUBSTRING(`text_loc2`, LOCATE('| ', `text_loc2`) + 2)),
  `text_loc3` = CONCAT('[PVP] ', SUBSTRING(`text_loc3`, LOCATE('| ', `text_loc3`) + 2)),
  `text_loc4` = CONCAT('[PVP] ', SUBSTRING(`text_loc4`, LOCATE('| ', `text_loc4`) + 2)),
  `text_loc5` = CONCAT('[PVP] ', SUBSTRING(`text_loc5`, LOCATE('| ', `text_loc5`) + 2)),
  `text_loc6` = CONCAT('[PVP] ', SUBSTRING(`text_loc6`, LOCATE('| ', `text_loc6`) + 2)),
  `text_loc7` = CONCAT('[PVP] ', SUBSTRING(`text_loc7`, LOCATE('| ', `text_loc7`) + 2)),
  `text_loc8` = CONCAT('[PVP] ', SUBSTRING(`text_loc8`, LOCATE('| ', `text_loc8`) + 2))
WHERE `name` = 'pvp_currency';
