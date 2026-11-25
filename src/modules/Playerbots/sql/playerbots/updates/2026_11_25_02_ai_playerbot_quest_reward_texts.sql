-- Quest reward lines that were hard coded English in TalkToQuestGiverAction
-- (Classic clients show them straight to the player, so they need the Chinese column).
DELETE FROM `ai_playerbot_texts` WHERE `name` IN ('quest_reward_received', 'quest_no_suitable_reward');

INSERT INTO `ai_playerbot_texts`
  (`id`, `name`, `text`, `say_type`, `reply_type`, `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`, `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
  (1948, 'quest_reward_received', 'Rewarded %item', 0, 0, '', '', '', '获得了 %item', '', '', '', ''),
  (1949, 'quest_no_suitable_reward', 'Unable to find suitable reward. Asking for help....', 0, 0, '', '', '', '找不到合适的奖励，正在向主人求助……', '', '', '', '');
