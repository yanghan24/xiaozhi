-- Preserve the explicit device permission boundary across delayed grant rejection.
ALTER TABLE dc_device ADD permission_authorization_order BIGINT NOT NULL DEFAULT 0;
