# Controlled identity-loss bench image

This standalone P4 image supports the #42 acceptance test: remove only
`device_ctrl/identity_v1` through the native NVS API, retaining the `device_id`
anchor and every other live NVS value. It does not contain the product UI,
network stack, audio flow, or normal provisioning entry point.

Use the verified physical board, its existing partition table and validated
application slot. Preserve a private full Flash backup and prove that the
administrator recovery operation and original product image can be resumed
before using the erase command. This image does not authenticate an operator;
operator authorization remains the responsibility of the bench and backend.

## Build and host checks

With ESP-IDF v5.5.5 activated, from the firmware repository root:

```sh
idf.py -C tools/identity-loss-test -B build/identity-loss-test \
  -DSDKCONFIG="$PWD/build/identity-loss-test/sdkconfig" -DIDF_TARGET=esp32p4 build
cmake -S tools/identity-loss-test/tests -B build/identity-loss-guards
cmake --build build/identity-loss-guards
ctest --test-dir build/identity-loss-guards --output-on-failure
```

The tests invoke the real request handler and identity loader with mocked NVS
and UART. They cover input validation, mismatched identity, preservation of all
NVS value types, one-attempt behavior, commit/read/open failures, postcondition
failure, missing identity and invalid anchor.

## UART protocol

UART uses 115200 baud. A ready frame repeats every four seconds while idle:

```json
{"version":1,"mode":"controlled_identity_loss","hardware_id":"<P4 ID>","write_attempted":false}
```

The request must have exactly these five fields:

```json
{
  "version": 1,
  "operation_id": "<32 lowercase hexadecimal characters>",
  "action": "erase_identity_v1",
  "identity": "<object containing the current six-field private identity>",
  "confirmation": "erase-device-ctrl-identity-v1-only"
}
```

The `identity` value is an object, using the schema validated by
`scripts/provision_device.py`; the string above is a placeholder. The whole
identity must match the current record and physical P4. Never put an actual
request, identity, UART capture or NVS backup into Git.

Before its sole persistent action, the image reads every live NVS record and
checks the intact anchor. It then erases only the hardcoded identity key and
commits. A success receipt identifies the operation and confirms that the key
is absent, the anchor is preserved, all other logical NVS records are identical,
and the normal identity loader refuses the missing identity.

There is at most one persistent attempt per boot. `storage_unverified` and
`postcondition_unverified` require independent readback; they do not mean that
the original identity is intact. A reset clears the in-memory attempt latch,
so the bench must also persist its intent and never automatically resend an
uncertain erase request.

Restore the exact accepted product application to the same slot and verify
that it reports missing identity and refuses normal use. Then use the existing
administrator recovery flow with one durable new recovery journal. After
credential rotation, continue that journal and keep the newly issued identity;
restoring an old NVS backup cannot restore the backend credential generation.
