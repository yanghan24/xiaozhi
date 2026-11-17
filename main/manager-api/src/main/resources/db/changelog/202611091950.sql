-- Database ordering prevents late older callbacks from replacing a newer grant,
-- including callbacks whose flows were created in the same millisecond.
ALTER TABLE customer_identity_flow ADD creation_order BIGINT NOT NULL AUTO_INCREMENT UNIQUE;
ALTER TABLE customer_authorization ADD started_order BIGINT NOT NULL DEFAULT 0;
