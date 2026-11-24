-- WoW Forever: remove the 5-second looping emote from Ironforge Guard (entry 5595)
--
-- TDB ships this row as VMaNGOS EventAI legacy: UPDATE_OOC every 5 s -> EMOTE 18.
-- The value set comes from the 3.3.5 emote table; on the 1.60 client Emotes.db2
-- the ids are remapped and the id plays a face-covering/crying loop instead.
-- The official guards stand idle, so the row is a data-port residue.
--
-- Reported 2026-11-23 (player observation inside Ironforge); verified against
-- world.smart_scripts and official-server behaviour.
-- Idempotent: safe to re-run on every worldserver start.

DELETE FROM `smart_scripts`
WHERE `source_type` = 0
  AND `entryorguid` = 5595
  AND `event_type` = 1        -- SMART_EVENT_UPDATE_OOC
  AND `action_type` = 5       -- SMART_ACTION_EMOTE
  AND `action_param1` = 18;
