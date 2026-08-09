# Update Boot Guard v0

Status: deterministic host-tested OTA trial/rollback policy core, 2026-08-09.
This is not an updater, downloader, signature verifier, partition writer,
bootloader binding, persistent state format, or physical recovery result.

## Boundary

`UpdateBootGuard` consumes evidence from separately reviewed adapters and
controls only the lifecycle decision:

`idle -> staged -> pending reboot -> trial -> confirmed`

Any boot mismatch, exact deadline expiry, trial-boot limit, or explicit health
failure moves the lifecycle to `rollback required`; only observed boot of the
original version/slot completes `rolled back`.

The guard never downloads images, accepts keys, changes boot slots, writes
flash, confirms a bootloader image, or executes rollback. Boolean verification
evidence is only meaningful when produced by trusted target adapters.

## Candidate admission and write evidence

A policy fixes the exact hardware ID, running version/slot, allowed image size,
role-specific required health mask, minimum stable time, confirmation deadline,
and maximum trial boots.

A candidate must:

- target the exact hardware ID and inactive slot;
- strictly advance the running version;
- fit the configured image-size bound;
- arrive with authenticity, integrity, compatibility, and rollback-image
  verification evidence.

Staging alone cannot trigger a reboot. The guard advances only after the image
writer reports the exact candidate version/slot, full-image readback
verification, and persisted boot selection. A staged candidate can be canceled;
a written/pending candidate cannot be silently canceled through this API.

## Trial health

The fixed health vocabulary covers runtime start, watchdog, core I/O,
configuration, display, and radio. Each target policy chooses a nonzero subset:

- a gateway can require runtime, watchdog, core I/O, configuration, and radio;
- a gauge can additionally require display;
- a GPS node can use runtime, watchdog, core I/O, configuration, and its chosen
  transport health while GNSS fix itself may legitimately be unavailable.

Passing observations accumulate only for the current nonzero boot-session ID.
A reboot into the candidate starts a new trial window and clears prior health.
Repeated trial boots are bounded. Monotonic time cannot regress within one
trial.

Confirmation requires all configured health bits and `elapsed >= minimum`, but
must occur while `elapsed < deadline`. At exact deadline, rollback is mandatory.
The bootloader adapter must persist confirmation atomically; the host guard's
`confirmed` state alone does not make an image permanent.

## Rollback and fleet behavior

Rollback completion requires an observed new boot session running the exact
original version and slot. A different version/slot remains a mismatch.

Gateway, gauges, GPS, and auxiliary nodes update independently. Fleet
coordination must keep useful instrumentation available, roll out one role/node
at a time by default, reject incompatible protocol/schema combinations, and
allow recovery through USB/bootloader even when radio configuration is broken.

## Host evidence

`tests/host/update_boot_guard_tests.cpp` covers eight groups:

1. policy and candidate hardware/version/slot/size/verification validation;
2. staged cancel plus exact write/readback/boot-selection evidence;
3. accumulated health and minimum stable-time confirmation;
4. exact confirmation deadline rollback;
5. bounded repeated trial boots;
6. boot mismatch and exact original-image rollback completion;
7. explicit health rollback plus health/session validation;
8. monotonic time, duplicate boot session, and lifecycle stop.

The suite also repeated 100 times with zero failures.

## Remaining OTA gates

- select signed-image format, approved algorithms, trust roots, key custody,
  rotation/revocation, anti-rollback version source, and secure-boot policy;
- implement bounded download/resume, manifest parsing, hardware/partition
  compatibility, full readback/hash/signature verification, and inactive-slot
  write adapters;
- persist lifecycle/candidate/trial attempts atomically across every reset and
  corruption point;
- bind target-specific boot confirmation/rollback and watchdog health;
- test power loss at every download/write/verify/boot/confirm/rollback phase;
- test corrupt/incompatible/wrong-board/old images, full flash, weak battery,
  radio loss, and broken configuration;
- document per-board USB/BOOT recovery and restore a known-good image physically;
- stage fleet updates so gateway and all displays are never removed together.
