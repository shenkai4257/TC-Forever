-- Bot chat lines that were hard coded English in the action code (guild/mail/gossip/
-- ready-check/emote remarks). They are shown to the player as the bot's own words, so they
-- belong in the text table with the Chinese column filled.
DELETE FROM `ai_playerbot_texts` WHERE `name` LIKE 'chat_%';

INSERT INTO `ai_playerbot_texts`
  (`id`, `name`, `text`, `say_type`, `reply_type`, `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`, `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
  (1950, 'chat_emote_wait_here', 'Fine.. I''ll stay right here..', 0, 0, '', '', '', '好吧……我就站这儿。', '', '', '', ''),
  (1951, 'chat_emote_follow_anywhere', 'Wherever you go, I''ll follow..', 0, 0, '', '', '', '你去哪我就跟你到哪。', '', '', '', ''),
  (1952, 'chat_emote_ominous_music', 'I''m just waiting for the ominous music now...', 0, 0, '', '', '', '我就在等那段不祥的音乐了……', '', '', '', ''),
  (1953, 'chat_emote_ask_directions', 'Noooooooo.. you just couldn''t ask for directions, huh?', 0, 0, '', '', '', '不~~~你就是不肯问路，是吧？', '', '', '', ''),
  (1954, 'chat_emote_genius_idea', 'Oh boy.. another genius idea...', 0, 0, '', '', '', '哦豁……又一个天才主意……', '', '', '', ''),
  (1955, 'chat_emote_no_thanks_village', 'No thanks.. I had some back at the last village', 0, 0, '', '', '', '谢了……我上个村子已经吃过了。', '', '', '', ''),
  (1956, 'chat_emote_look_like_dog', 'Do I look like a dog to you?!', 0, 0, '', '', '', '我看上去像狗吗？！', '', '', '', ''),
  (1957, 'chat_emote_roll_eyes', 'Keep doing that and I''ll roll those eyes right out of your head..', 0, 0, '', '', '', '再来一次我就把眼珠子翻出来给你看……', '', '', '', ''),
  (1958, 'chat_emote_lovely', 'Lovely... just lovely..', 0, 0, '', '', '', '好极了……真是好极了……', '', '', '', ''),
  (1959, 'chat_guild_already_member', 'Sorry, I am in a guild already', 0, 0, '', '', '', '抱歉，我已经在公会里了。', '', '', '', ''),
  (1960, 'chat_guild_already_invited', 'Sorry, I am invited to a guild already', 0, 0, '', '', '', '抱歉，我已经收到公会邀请了。', '', '', '', ''),
  (1961, 'chat_guild_invite_thanks', 'Thanks for the invite!', 0, 0, '', '', '', '谢谢邀请！', '', '', '', ''),
  (1962, 'chat_wait_for_me', 'Wait for me!', 0, 0, '', '', '', '等等我！', '', '', '', ''),
  (1963, 'chat_guild_bank_not_member', 'I''m not in your guild!', 0, 0, '', '', '', '我不在你的公会里！', '', '', '', ''),
  (1964, 'chat_guild_bank_not_nearby', 'Cannot find the guild bank nearby', 0, 0, '', '', '', '附近找不到公会银行。', '', '', '', ''),
  (1965, 'chat_mail_no_bag_space', 'Not enough bag space', 0, 0, '', '', '', '背包空间不够。', '', '', '', ''),
  (1966, 'chat_mail_no_mailbox', 'There is no mailbox nearby', 0, 0, '', '', '', '附近没有邮箱。', '', '', '', ''),
  (1967, 'chat_gossip_unknown_option', 'Unknown gossip option', 0, 0, '', '', '', '未知的对话选项。', '', '', '', ''),
  (1968, 'chat_gossip_talk_first', 'I need to talk first', 0, 0, '', '', '', '我得先跟你说话。', '', '', '', ''),
  (1969, 'chat_ready_no_ammo', 'Out of ammo!', 0, 0, '', '', '', '没弹药了！', '', '', '', ''),
  (1970, 'chat_ready_no_pet', 'No pet!', 0, 0, '', '', '', '没有宠物！', '', '', '', ''),
  (1971, 'chat_ready_pet_unhappy', 'Pet is unhappy!', 0, 0, '', '', '', '宠物不高兴了！', '', '', '', ''),
  (1972, 'chat_loot_guild_only', 'I will play with this loot type only if I''m in your guild :/', 0, 0, '', '', '', '只有我在你公会里时才会用这种拾取方式 :/', '', '', '', '');
