# Peer Approval and Authorization v0

Status: deterministic host-tested logical registry, 2026-08-09. This is not
ESP-NOW pairing, cryptography, a key store, discovery UI, persistent identity,
radio authentication, or a physical provisioning result.

## Boundary

`PeerAuthorizationRegistry` holds at most eight logical peers. Its API accepts
only:

- nonzero opaque logical peer and approval-request IDs;
- gateway, gauge, GPS, or OpenTrail-bridge role;
- a role-scoped permission mask;
- radio channel 1 through 14;
- a nonzero opaque secure-key handle and authorization epoch.

It accepts and stores no raw key bytes, PINs, passphrases, MAC addresses,
Bluetooth identities, discovery payloads, or user-facing codes. A target
adapter must map an authenticated radio identity to the logical peer and resolve
the handle inside protected key storage.

## Approval workflow

Discovery is always untrusted and cannot create a registry entry. A caller
opens one bounded approval window for one fully described candidate. The later
`approve` invocation represents a distinct local/user-confirmed action and must
provide a key handle already provisioned by a secure adapter.

Only one candidate can be pending, preventing simultaneous prompts from being
silently confused. At exact `elapsed >= approval_window_ms`, the request expires
and is cleared. Request/peer IDs, role permissions, channel, key handle, epoch,
capacity, and duplicate peer/key use are validated before registry mutation.

The host API does not define how local presence is proved. Candidate display,
numeric comparison/QR, button confirmation, anti-spoof context, PIN policy, and
accessible replacement/reset UX remain physical design gates.

## Authorization and lifecycle

Every decision checks the active logical peer, exact opaque key handle, exact
channel, and one required role permission. Channel agreement is admission
metadata, not a security mechanism. Allowed permissions are:

- gateway: publish telemetry, receive configuration;
- gauge: receive telemetry, publish alarm acknowledgement;
- GPS: publish GPS;
- OpenTrail bridge: receive critical alerts, publish alarm acknowledgements.

Revocation immediately marks a peer inactive and clears its key handle.
Replacement requires revoke, explicit forget, and a fresh approval. Key rotation
requires a unique handle and strictly increasing authorization epoch; the old
handle stops authorizing immediately. Full snapshots are all-or-nothing.

This registry does not replace transport encryption/authentication, message
integrity, application session/sequence replay checks, rate limits, or protocol
authorization. It composes with those layers.

## Threats and required controls

- discovery flooding/spoofing: one local bounded window, rate limiting and UI
  provenance still required;
- approval confusion: show exact role/capabilities and require physical/local
  confirmation, never background approval;
- key extraction: keep raw keys out of this API and use protected storage;
- stolen/replaced node: revoke and rotate remaining trust, then require fresh
  presence-based approval;
- replay/downgrade: protocol session/sequence plus persistent authorization
  epoch and anti-rollback storage remain required;
- lost display or broken radio: retain a documented USB/local reset/recovery
  path that cannot silently authorize a replacement;
- factory reset: require local confirmation, erase key material, invalidate
  peers, and make the reset visible to every affected role.

## Host evidence

`tests/host/peer_authorization_tests.cpp` covers eight groups:

1. lifecycle and approval-window configuration;
2. request/peer/role/permission/channel validation;
3. single pending request, clock regression, and exact expiration;
4. approval plus peer/key/channel/permission authorization decisions;
5. eight-peer capacity and duplicate peer/key handling;
6. revoke, inactive denial, explicit forget, and replacement;
7. unique key rotation with strictly increasing epoch;
8. atomic snapshots and bounded opaque-handle entry shape.

The suite also repeated 100 times with zero failures. The separate
[peer-authorization checkpoint](PEER_AUTHORIZATION_CHECKPOINT_V0.md) now
provides a canonical boot-only record for logical peers, active/revoked state,
opaque key handles, and authorization epochs. Its separate
[two-slot host store](PEER_AUTHORIZATION_CHECKPOINT_STORE_V0.md) adds recoverable
generations and interruption handling without claiming protected target storage.

## Remaining pairing gates

- choose authenticated ESP-NOW provisioning and key-storage APIs for each exact
  board, including entropy and credential generation;
- define local-presence proof, candidate comparison, accessibility, cancel,
  timeout, replacement, revoke, lost-node, and factory-reset workflows;
- bind the canonical registry checkpoint to protected target storage with
  corruption/power-loss recovery and rollback resistance, without publishing
  device-specific identities;
- rate-limit discovery/approval/authorization failures and connect typed
  redacted diagnostics;
- fuzz radio/discovery adapters and test spoof, flood, replay, wrong key,
  wrong channel, reboot, key rotation, revoke, and recovery physically;
- document owner-controlled backup/recovery without exporting reusable secrets.
