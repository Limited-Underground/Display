# Target System-Recovery Adapter Plan

Status: design and acceptance boundary, 2026-08-09. No OpenGauge ESP-IDF target,
NVS adapter, protected monotonic source, or on-device boot composition currently
implements this plan.

## Why this is separate

The host-tested recovery core now provides:

- exact `OPA0` logical peer authorization;
- exact `OCR0` ACK replay plus alert-outbox state;
- one combined `ORS0` generation;
- a recoverable two-slot store; and
- caller-supplied trusted generation-floor enforcement.

Those contracts do not decide where bytes, raw keys, or the trusted generation
live on an ESP32. The target adapter must supply those mechanisms without
weakening the host invariants or claiming that CRC provides security.

The connected Heltec and SenseCAP units currently run MeshCore firmware. They
cannot validate this OpenGauge target-storage plan without selecting/building an
OpenGauge target and a documented recovery path first.

## Required target boundaries

### Two `ORS0` slots

The storage adapter must reserve two independently addressable slots of exactly
1280 bytes and implement the existing `CriticalAlertSystemRecoveryStorage`
contract:

- missing slot is distinct from I/O failure;
- reads return the complete requested slot or fail;
- writes never report success until all bytes are accepted by the backend;
- erase is explicit for each slot;
- slot A and slot B failures remain independently observable; and
- no adapter may synthesize a valid record or silently substitute defaults.

The core performs byte-for-byte readback and decode after every reported write.
The target must still document backend atomicity, erase/write granularity,
wear behavior, and what an interrupted API call can leave behind.

### Protected key resolution

`OPA0` stores opaque key handles only. A separate protected-key adapter must:

- resolve each active handle to the intended nonexportable/authenticated key;
- reject missing, duplicated, stale, revoked, or wrong-purpose handles;
- never log raw key material or device-specific pairing identities;
- make key-store loss distinguishable from an empty first boot; and
- coordinate replacement and factory reset with authorization epochs.

The host recovery API now exposes
`CriticalAlertSystemRecoveryKeyValidator` and a key-validating direct/store
restore path. It presents only active `PeerAuthorizationEntry` metadata and the
opaque handle before any live import. The concrete target implementation and
key-store selection remain open.

### Trusted generation source

The caller-supplied trusted value must not be stored only inside the same two
rollbackable `ORS0` slots. The target design must select an authenticated,
monotonic, or otherwise rollback-resistant source and document:

- read, advance, exhaustion, and recovery semantics;
- update atomicity and readback;
- behavior when the trusted source is ahead of both slots;
- behavior when a valid slot is below the trusted floor;
- authorized reset/replacement of the trusted value; and
- endurance limits and fleet/service implications.

This plan intentionally does not select a secure element, eFuse policy, NVS
encryption configuration, or other mechanism before an exact target is chosen.

## Boot sequence

The host layer now provides
`CriticalAlertSystemRecoveryBootCoordinator` for the policy portion of this
sequence. Under exclusive ownership, a target should:

1. initialize storage and protected-key services;
2. read the trusted minimum generation, failing visibly if its state is
   unreadable or ambiguous;
3. start an empty authorization registry, outbox, and ACK ingress with the exact
   local policies;
4. run the coordinator's key-validating `restore_at_or_above` path with the
   trusted minimum;
5. if the selected local generation is newer than the trusted source, advance
   the source and require exact readback;
6. expose degraded single-slot recovery and schedule a bounded repair save;
7. treat rollback, conflict, checkpoint rejection, key loss, or trusted-source
   mismatch as typed safe-mode/service conditions rather than silent defaults;
   and
8. enable alert transport only when the coordinator reports an operational
   restored state.

An empty store is a first-boot condition only when the trusted source and local
provisioning state independently agree that no prior generation existed.
The coordinator enforces this by requiring uninitialized trust, an explicitly
unprovisioned caller state, and two exactly empty inspected slots.

## Save and trusted-floor ordering

Normal persistence should:

1. call `save_next_after` using the last trusted generation;
2. require successful exact readback/decode;
3. if the save reports uncertain commit, inspect/reconcile slots before deciding
   whether the generation exists;
4. only after a verified committed `ORS0`, advance and read back the separate
   trusted generation source; and
5. keep transport state fail-visible if trusted-floor advancement is uncertain.

Advancing the trusted source before confirming the corresponding `ORS0` can
intentionally fail closed but may strand otherwise recoverable state. Retrying a
commit-uncertain write without inspection can overwrite the prior good slot.

## Reset and replacement

`store.reset()` alone is not a factory reset. An authorized target workflow must
coordinate:

- disabling transport;
- erasing both logical recovery slots;
- erasing or revoking protected keys;
- invalidating peer authorizations and epochs;
- resetting/reseeding the trusted generation under explicit authority; and
- producing operator-visible completion or partial-failure evidence.

Power loss at every step must have a documented safe restart result.

## Acceptance matrix

Before any target durability claim, record evidence for:

- clean first boot versus lost/corrupt prior state;
- all eleven modeled `ORS0` interruption boundaries;
- backend-specific commit boundaries before/during/after slot write and erase;
- full record written followed by reported I/O failure;
- slot read failure, invalid slot, equal-generation conflict, and exhaustion;
- trusted value below/equal/above local generations;
- interruption before/during/after trusted-floor advancement;
- missing/wrong/revoked protected key handles and authorization epoch mismatch;
- repeated save/repair cycles sufficient for an explicit endurance budget;
- firmware rollback/update interaction; and
- USB/local recovery without exposing reusable secrets.

Evidence must identify the exact board, flash layout, ESP-IDF version, storage
configuration, power-interruption method, firmware commit, and recovery result.
