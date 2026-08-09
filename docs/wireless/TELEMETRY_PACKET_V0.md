# Gateway-to-Gauge Telemetry Packet v0

Status: host-tested engineering contract, 2026-08-09. This is not a frozen
production protocol or evidence of physical ESP-NOW performance.

## Purpose and boundary

The `OGT0` packet carries a small batch of normalized OpenGauge signals from a
provisioned gateway to one gauge. It is an explicit byte format; C/C++ object
memory is never transmitted. The packet fits inside the v1-compatible
250-byte ESP-NOW payload ceiling and is intended for the encrypted-unicast
transport contract in `ESP_NOW_TRANSPORT_V0.md`.

CRC-32 detects accidental corruption only. It does not authenticate a gateway,
hide data, prevent replay, or prove that the receiver rendered a value. The
selected production transport must still provision an authorized peer and
keys. The boot-session ID and sequence support receiver replay/loss decisions,
but their generation and persistence policy remain unresolved.

## Fixed 96-byte frame

All multibyte integers are little-endian. Signed values use 64-bit two's
complement. Reserved and unused bytes must be zero. A decoder rejects any
length other than exactly 96 bytes.

| Offset | Bytes | Field | Rule |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `OGT0` |
| 4 | 1 | Version | `0` |
| 5 | 1 | Message type | `1` = telemetry batch |
| 6 | 2 | Encoded length | `96` |
| 8 | 8 | Gateway ID | Provisioned nonzero opaque ID; not a vehicle VIN |
| 16 | 4 | Boot-session ID | Nonzero and different after a gateway restart |
| 20 | 4 | Sequence | Increments per packet; wraps modulo 2^32 |
| 24 | 8 | Gateway uptime, ms | Diagnostic only; never compared directly with the gauge clock |
| 32 | 1 | Signal count | `1..3` |
| 33 | 3 | Reserved | Zero |
| 36 | 54 | Three 18-byte signal slots | Used slots first; every unused slot is all zero |
| 90 | 2 | Reserved | Zero |
| 92 | 4 | CRC-32/ISO-HDLC | Bytes 0 through 91 |

CRC uses reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, and a
final XOR of `0xFFFFFFFF`. The check value for ASCII `123456789` is
`0xCBF43926`.

### Signal entry

| Relative offset | Bytes | Field | Rule |
| ---: | ---: | --- | --- |
| 0 | 2 | Signal code | Registered code below; unique within a packet |
| 2 | 1 | Value type | Normalized `SignalValueType` numeric value |
| 3 | 1 | Unit | Normalized `SignalUnit` numeric value |
| 4 | 1 | Quality | Normalized `SignalQuality` numeric value |
| 5 | 1 | Flags | Bit 0 = value present; all other bits zero |
| 6 | 8 | Raw value | Canonical integer value or zero when absent |
| 14 | 4 | Source age, ms | Age already accumulated at the gateway |

`valid` and `suspect` require a value. `unavailable`, `error`,
`out_of_range`, `stale`, and `unknown` require no value and a zero raw value.
The cache-to-wire adapter deliberately strips the numeric value whenever the
effective quality does not permit one.

## v0 signal registry

The code, normalized ID, value type, and unit are one indivisible definition.
A decoder rejects an unknown code or a mismatched type/unit combination.

| Code | Normalized ID | Type | Unit |
| ---: | --- | --- | --- |
| 1 | `powertrain.engine_speed` | unsigned integer | milli-revolutions/minute |
| 2 | `powertrain.engine_coolant_temperature` | signed integer | milli-Celsius |
| 3 | `vehicle.speed` | unsigned integer | millimetres/second |
| 4 | `electrical.system_voltage` | unsigned integer | millivolt |

Adding a signal requires a reviewed registry entry and compatibility tests. It
does not change the packet version as long as old receivers fail closed on an
unknown code. Changing an existing code's meaning requires a new protocol
version.

## Sequence, restart, and age behavior

A gauge tracks each authorized gateway separately.

- First packet initializes the stream.
- A sequence delta of one is in order.
- A forward delta greater than one reports the exact missing-packet count.
- Equal sequence is a duplicate and does not advance state.
- A delta in the backward half of the 32-bit sequence space is out of order.
- Ordinary `0xFFFFFFFF` to `0` wrap is in order.
- A changed nonzero boot-session ID starts a new sequence epoch.
- A packet from a different gateway ID is rejected by an initialized tracker.

Gateway uptime and gauge uptime are not synchronized clocks. For freshness,
the gauge adds receiver-local elapsed time since packet receipt to the encoded
source age. At the exact configured stale threshold, a valid/suspect value
becomes `stale` and its display value is removed. Loss therefore ages the last
accepted state rather than freezing a plausible number forever.

## Payload and rate budget

The fixed packet consumes 96 of 250 compatible ESP-NOW payload bytes (38.4%)
and carries at most three signal updates. Because delivery is unicast, every
gauge peer consumes a separate transmission.

| Packet rate per gauge | Signal-update ceiling per gauge | Payload bytes/s, 1 gauge | Payload bytes/s, 8 gauges |
| ---: | ---: | ---: | ---: |
| 1 Hz | 3/s | 96 | 768 |
| 5 Hz | 15/s | 480 | 3,840 |
| 10 Hz | 30/s | 960 | 7,680 |
| 20 Hz | 60/s | 1,920 | 15,360 |

These figures are serialized application payload only. They exclude Wi-Fi MAC,
ESP-NOW, encryption, contention, retries, coexistence, and application-task
overhead. Ten packets/s per gauge is a planning point, not an accepted target.
Physical OG-010A tests must measure loss, latency, airtime/coexistence, peer
scaling, and current draw before selecting a production rate.

## Fail-closed validation

The codec rejects null/short/long buffers, wrong magic/version/type/length,
bad CRC, nonzero reserved bits/bytes, zero identity/session, zero or excessive
signal count, duplicate/unknown codes, unknown enums, incompatible type/unit,
invalid Boolean/range/value state, and nonzero unused entries. Encode validates
before writing, and decode returns a fresh result only after the complete frame
passes.

Eight deterministic host-test groups cover a normative 96-byte golden vector,
round trip, cache conversion, malformed/incompatible/corrupt frames, canonical
unused bytes, sequence gaps/duplicates/wrap/restart, exact freshness, and
delivery plus injected loss through two fake encrypted-unicast peers.

## Deferred work

- ESP-IDF adapter and selected-board builds
- Peer discovery, provisioning, key generation/storage/rotation, authorization,
  and recovery
- Application acknowledgement policy, if any
- Cache/task composition and any packet-priority extension beyond the
  host-tested subscription/change/deadband scheduler
- Physical RF, latency, coexistence, reboot, range, peer-count, and rate evidence
- Version negotiation and capability exchange
