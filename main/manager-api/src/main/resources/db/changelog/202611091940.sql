-- Serializes only short identity-publication / logout transactions, never remote requests.
CREATE TABLE customer_identity_barrier (id INT PRIMARY KEY) ENGINE=InnoDB;
INSERT INTO customer_identity_barrier(id) VALUES (1);
