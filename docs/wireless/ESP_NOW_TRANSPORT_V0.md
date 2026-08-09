# ESP-NOW Transport Contract and Fake v0

Status: deterministic host proof, 2026-08-09. No ESP-IDF adapter, radio,
channel/coexistence measurement, peer key, or physical packet has been tested.

## Scope

The transport moves opaque unicast datagrams between a gateway and configured
gauge peers. It owns only adapter lifecycle, peer table operations, bounded
queues, nonblocking send/receive, radio-delivery completion, and link metadata.

Serialization, sequence numbers, duplicate handling, application
acknowledgements, subscriptions, retries, pairing approval, key custody, and
gauge state belong above this boundary.

## Bounded contract

- maximum v0 storage/MTU: 250 bytes
- unicast addresses only; zero, multicast, and broadcast addresses fail closed
- explicit channel 1-14; channel zero/current-channel behavior is deliberately
  excluded from the deterministic v0 contract
- peer table operations reject self, duplicate, wrong-channel, missing, and
  over-capacity entries
- encrypted unicast is required by the default policy
- a send copies the complete payload into a four-entry queue or returns an
  explicit error
- receive and delivery-completion queues are four entries; no frame is silently
  truncated or removed after a buffer-too-small result
- completion backpressure pauses outgoing work until the application polls
  receipts
- stop clears volatile peer and queue state; restart requires explicit peer
  reprovisioning

The 250-byte limit intentionally stays compatible with ESP-NOW v1 frames even
though newer ESP-NOW v2 implementations can support larger frames. The later
packet codec should remain well below this ceiling.

## Delivery meaning

`delivered_to_peer_radio` means the fake models a successful ESP-NOW send
callback at the destination radio. It does not mean the destination application
decoded, accepted, cached, displayed, or persisted the packet.

Espressif's programming guide makes the same MAC/application distinction and
recommends an application acknowledgement and sequence number when that
guarantee is required. See the
[official ESP-NOW programming guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-reference/network/esp_now.html).

## Security boundary

The fake records whether both peers expect an encrypted link and rejects a
mismatch. It does not model or store PMK/LMK bytes and is not cryptographic
evidence. Production work still needs:

- approved peer identity and replacement/revoke flow
- unique key generation, provisioning, storage, rotation, and zeroization
- rejection of default keys
- replay/duplicate protection above the radio link
- a decision for discovery, since ESP-NOW multicast/broadcast encryption is not
  equivalent to encrypted unicast
- ESP-IDF build-time encrypted-peer limits and Wi-Fi coexistence measurement

No address, key, or pairing secret belongs in public logs.

## Deterministic fake

The fake connects independently configured peer instances and simulates:

- fixed delivery latency and RSSI metadata
- encrypted peer agreement
- missing link and unavailable peer
- channel mismatch and receiver rejection
- injected loss
- receiver queue exhaustion
- completion-queue backpressure
- stop/offline behavior

This is a logic test, not RF propagation or ESP-IDF behavior evidence.

## Host evidence

`tests/host/esp_now_transport_tests.cpp` exercises eight scenario groups:

1. identity, unicast, channel, start, MTU, and state validation
2. peer self/duplicate/channel/capacity/remove rules
3. encrypted binary delivery, latency, RSSI, token, and completion metadata
4. encryption-policy, receiver-peer, and channel rejection
5. receive-buffer preservation and transmit-queue backpressure
6. missing-link, peer-not-ready, and injected-loss outcomes
7. bounded receiver/completion queues and completion backpressure
8. stop clearing peers/pending queues without claiming delivery

Run all host suites with `tools/Test-Host.ps1`.

## Next boundary

OG-010 now defines the fixed 96-byte `OGT0` telemetry frame with gateway and
boot-session identity, sequence, source age, three registered typed values,
canonical reserved bytes, and CRC corruption detection. See
`TELEMETRY_PACKET_V0.md`. OG-011 must define pairing/key lifecycle before
physical encrypted traffic. An ESP-IDF adapter remains an OG-009 hardware
binding, not implied by this host fake or packet codec.
