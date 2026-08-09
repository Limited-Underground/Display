# Critical Alert ACK Ingress v0

Status: deterministic host-tested admission and outbox-correlation policy,
2026-08-09. This is not a physical transport, persistent replay journal, key
store, or proof that an ACK crossed USB, BLE, ESP-NOW, MeshCore, or LoRa.

## Boundary

`CriticalAlertAckIngress` is the only host component in this increment allowed
to turn received `OGK0` bytes into an outbox acknowledgement. Before an accepted
ACK removes an alert, it requires all of:

1. adapter metadata explicitly says the frame was authenticated;
2. the logical peer, opaque secure-key handle, channel, and
   `publish_alarm_ack` permission pass `PeerAuthorizationRegistry`;
3. the peer has an explicitly configured nonzero consumer ID and boot session;
4. the independent ACK codec accepts the 64 canonical bytes and CRC;
5. consumer, local producer, boot session, and configured maximum observed age
   match;
6. sequence admission passes a fixed 32-packet replay window; and
7. event ID, condition ID, asserted/cleared lifecycle, and at least one locally
   accepted send match a retained outbox entry.

CRC is corruption detection only. `authenticated=true` is an adapter assertion
that must be backed by the eventual cryptographic transport; this host policy
does not create that proof.

## Session and replay policy

Up to eight logical consumers can be bound. A received frame cannot create a
binding or silently replace a boot session. Explicit local rebind resets that
consumer's in-memory replay window.

The highest accepted sequence and a 32-bit bitmap admit bounded reordering,
reject duplicates, reject values at least 32 behind, and reject the ambiguous
half-range serial-number case. Unsigned serial arithmetic permits normal
32-bit wrap. A sequence is committed only after authorization, semantic
validation, and outbox correlation succeed, so a mismatched frame cannot burn
a valid sequence number.

Replay state is RAM-only. Persistent authorization epoch, session/window
checkpointing, rollback resistance, revoke/restart behavior, and rate limits
remain required for production.

## Accepted versus rejected ACK

An accepted/none ACK that passes every gate calls the outbox acknowledgement
operation and removes exactly one retained lifecycle. A rejected/nonzero-reason
ACK must still correlate and consumes its sequence, but never increments the
outbox acknowledgement count or removes the event. The returned disposition
and canonical reason are explicit negative evidence for a later retry/terminal
policy; they are never delivery success.

## Host evidence

`tests/host/critical_alert_ack_ingress_tests.cpp` covers eight groups:

1. lifecycle, dependency/configuration checks, and eight bindings;
2. authenticated accepted ACK completing exactly one outbox entry;
3. unauthenticated, wrong-key, and wrong-channel denial;
4. codec, consumer, producer, session, and observed-age rejection;
5. bounded out-of-order admission and duplicate rejection;
6. too-old/half-range rejection and explicit session rebind;
7. correlated remote rejection remaining non-success; and
8. outbox mismatch and clock regression without sequence consumption.

The full OpenGauge host matrix passes, and this focused suite repeated 100
times with zero failures.

## Remaining gates

- implement authenticated framed serial/local-wireless transport and protected
  key-handle resolution;
- generate `OGK0` only from a final OpenTrail ingress decision;
- persist authorization/session/replay state with corruption, power-loss,
  rollback, key-rotation, revoke, and recovery tests;
- define bounded retry/terminal behavior for each negative ACK reason;
- add typed redacted diagnostics and rate limits; and
- validate physical two-Heltec delivery, loss/reordering/restart, and repeater
  behavior without claiming the repeater creates application ACKs.
