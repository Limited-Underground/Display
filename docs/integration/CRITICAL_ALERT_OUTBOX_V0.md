# Critical Alert Delivery Outbox v0

Status: deterministic host-tested application-delivery policy, 2026-08-09.
This is not a transport, ACK wire format, physical OpenTrail delivery result,
persistent journal, or claim that an alert was sent over MeshCore/ESP-NOW/USB.

## Boundary

`CriticalAlertOutbox` accepts only a valid existing 64-byte `OGA0` frame and
retains at most eight unique event IDs. It separates:

1. local queue rejection: the adapter did not accept the frame; no delivery
   attempt is consumed;
2. local queue acceptance: one attempt is in flight, but delivery is unknown;
3. authenticated application ACK: OpenTrail accepted the exact event lifecycle;
   only this is success.

Radio send completion, serial write completion, and local queue acceptance are
never treated as an OpenTrail ACK.

## Capacity and priority

Configuration reserves 1 through 7 of the eight slots for emergency alerts.
Non-emergency critical alerts cannot consume the reserve. Among ready entries,
the oldest emergency is prepared before the oldest lower-severity alert. The
reserve protects admission under pressure; it does not make delivery reliable.

Duplicate retained event IDs and malformed/corrupt semantic frames fail before
mutation. Entries remain fixed 64-byte frames; no raw CAN, key, free text, or
location is added.

## Two-phase send

`prepare(now)` returns one opaque token and frame. Exactly one send may be
prepared at once. `commit_local_send(token, accepted, now)` then records the
adapter outcome:

- rejection returns the event to queued state after bounded backoff without
  increasing attempts;
- acceptance increases attempts and starts the application-ACK timer;
- a wrong token leaves the prepared event intact;
- an abandoned prepared token is released at the exact local-commit timeout
  without consuming an attempt.

Ready selection is emergency-first and then oldest enqueue time, independent of
array holes. All time is local monotonic and regressions fail closed.

## ACK, retry, and terminal failure

An ACK must match nonzero retained event ID, condition ID, and asserted/cleared
state after at least one locally accepted attempt. A delayed valid ACK is still
accepted after the event has been released for retry but before terminal
removal. This prevents a late application response from causing a duplicate
retry when it can still be correlated safely.

At exact ACK timeout, the entry either becomes retryable after backoff or emits
a terminal timeout after the configured attempt limit. At exact maximum
lifetime, any queued/prepared/in-flight entry emits terminal lifetime failure.
Terminal failure is explicit diagnostic state, not silent success.

Correlated remote rejection now follows a fixed policy. `rate_limited` and
`internal_error` cancel any outstanding prepare and release the entry after the
normal retry backoff while attempts remain. `unauthorized`, `stale`,
`duplicate`, `conflict`, `malformed`, and `unsupported` terminate immediately.
A retryable response at the configured attempt limit also terminates. Terminal
evidence preserves event ID, condition ID, attempt count, and the typed remote
reason. Rejection never increments the successful acknowledgement counter.

The mirrored `OGK0` frame and ACK ingress now provide explicit serialization,
adapter-authentication admission, logical peer/key-handle/channel/permission
authorization, explicit consumer-session binding, a 32-sequence replay window,
observed-age policy, and exact outbox correlation. These remain host contracts:
the eventual adapter must provide the cryptographic authentication proof, and
replay/authorization state is not yet persistent.

## Host evidence

`tests/host/critical_alert_outbox_tests.cpp` covers eight groups:

1. lifecycle/configuration plus malformed-frame rejection;
2. emergency reserve/priority, duplicate/capacity, and oldest-ready ordering;
3. local rejection, backoff, and no attempt consumption;
4. exact ACK timeout, retry, attempt limit, and terminal evidence;
5. valid late ACK after retry release;
6. token and ACK lifecycle mismatch preserving the entry;
7. abandoned prepare timeout without attempt consumption;
8. exact maximum lifetime and monotonic clock regression.

The refined suite repeated 100 times with zero failures.

## Remaining physical-delivery gates

- choose framed serial, authenticated local wireless, or another exact adapter;
- compose alarm exporter -> outbox -> transport -> OpenTrail ingress -> ACK with
  queue loss, radio loss, duplicates, reordering, restart, and channel change;
- persist critical entries/attempts if required, with corruption, wear, privacy,
  power-loss, reset, and expiry policy;
- define bounded operator-visible terminal-failure behavior without distracting
  from vehicle operation;
- validate actual two-Heltec delivery and the SenseCAP repeater topology without
  claiming a repeater provides end-to-end application acknowledgement.
