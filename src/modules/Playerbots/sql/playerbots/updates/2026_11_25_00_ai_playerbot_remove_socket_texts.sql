-- 1.60 has no item sockets: the two socketing lines of ai_playerbot_texts are no longer referenced.
DELETE FROM `ai_playerbot_texts` WHERE `id` IN (1669, 1691);
