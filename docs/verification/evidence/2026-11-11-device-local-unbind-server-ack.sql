-- Read-only B state / stop-ACK evidence for the 860e045 physical acceptance.
-- No credentials, customer profile, or conversation content are selected.
SELECT 'B' AS device_alias, admitted, (owner_subject IS NOT NULL) AS bound,
       local_enabled, reason, permission_version, ownership_version, revision
FROM dc_device WHERE device_id='device_unmJnAE7EQIAyhGBeOmpsLoroMGk5lJN823eZ8cFzK4';
SELECT revision, permission_version, audio_stopped, received_at_ms
FROM dc_state_ack_history
WHERE device_id='device_unmJnAE7EQIAyhGBeOmpsLoroMGk5lJN823eZ8cFzK4' AND received_at_ms>=1794387823726
ORDER BY received_at_ms ASC, revision ASC;
