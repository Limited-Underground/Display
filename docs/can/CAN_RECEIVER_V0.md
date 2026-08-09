# Passive CAN Receiver v0

Status: host-tested interface and deterministic fake, 2026-08-09. This is not
a production driver or evidence that any vehicle, adapter, transceiver, or
ESP32 target has been validated.

## Purpose and safety boundary

`CanReceiver` is the hardware-neutral input boundary between a Classical CAN
controller and OpenGauge parsing/decoding. Its public interface has no transmit
operation. Initial hardware adapters must put both controller and transceiver
in a genuinely passive/listen-only mode that neither transmits frames nor
acknowledges traffic.

Host evidence cannot prove that a physical adapter is electrically passive.
Before any vehicle test, the selected implementation still requires review of
the controller, transceiver mode pins, voltage compatibility, isolation,
protection, termination, connector pinout, power, grounding, bitrate, and
failure behavior.

## Bounded Classical CAN frame

Each accepted frame carries:

- an 11-bit standard or 29-bit extended identifier;
- data or remote-frame kind;
- a Classical CAN data length from zero through eight bytes;
- an eight-byte canonical buffer whose bytes beyond the declared length are
  zero;
- a receiver-local monotonic millisecond capture time.

Remote-frame buffers must be entirely zero. CAN FD payloads, bit-rate switching,
error frames, controller-specific flags, and transport-protocol reassembly are
outside v0. The downstream J1939 parser independently rejects standard frames,
out-of-range identifiers, and unsupported J1939 forms.

## Listen policy

The host fake accepts 125 kbit/s, 250 kbit/s, 500 kbit/s, and 1 Mbit/s. A policy
must enable at least one of standard or extended frames and may independently
allow remote frames. This list describes the fake's deterministic test surface;
a production adapter must report and enforce its actual capabilities.

`start_listen_only` is valid only while offline. `stop` discards queued frames,
clears counters and injected faults, and returns the fake to a restartable
offline state.

## Receive and diagnostic behavior

The receiver exposes error-active, error-warning, error-passive, and bus-off
states. A successful receive returns the state captured with that frame, the
cumulative overflow count, and queue depth after removal. Status also exposes:

- operating mode and bitrate;
- current bus state and last operation result;
- queue depth and capacity;
- accepted and filtered frame counts;
- newest-frame overflow drops;
- bus-state transition count.

The 16-frame fake queue is FIFO. When full, it retains every already queued
frame and drops the newly injected frame. A regressing capture timestamp fails
closed. Bus-off and injected hardware faults reject new frames without
inventing data, while recovery preserves already queued valid frames.

`no_frame`, `filtered`, and `queue_full` are explicit results rather than
plausible frames. Production task code must drain with a fixed budget, surface
overflow/bus errors, and never block wireless publication or display tasks.

## Host evidence

`tests/host/can_receiver_tests.cpp` covers eight groups:

1. offline/listen-only lifecycle and policy validation;
2. identifier, length, unused-byte, kind, and format validation;
3. standard/extended/remote filtering and counters;
4. FIFO order, capture timestamps, per-frame bus metadata, and clock regression;
5. bounded queue overflow and drop-newest behavior;
6. bus-off recovery and injected hardware failure;
7. stop/restart queue and counter reset;
8. an extended EEC1 frame received through the interface, decoded, normalized,
   and stored as `engine.speed` without bypassing the real decoder/cache path.

This completes the OG-004 host abstraction acceptance criterion. It does not
complete the physical CAN hardware or ESP-IDF adapter gate.

## Production-adapter checklist

Before treating a concrete adapter as usable:

1. prove controller and transceiver listen-only configuration from current
   device documentation and inspect the actual board wiring;
2. confirm voltage levels, isolation decision, surge/ESD protection, grounding,
   connector, and that the test lead does not add unintended termination;
3. record exact MCU, controller/transceiver, board revision, firmware/toolchain,
   bus speed, vehicle state, and wiring;
4. test idle, valid traffic, malformed/error traffic, saturation/overflow,
   bus-off, disconnect, brownout, restart, and USB recovery;
5. capture only legally obtained traffic, reconcile decoded values against
   licensed/current data, and redact vehicle-specific identifiers before
   publication;
6. demonstrate that attaching, resetting, failing, or removing OpenGauge does
   not alter vehicle operation.
