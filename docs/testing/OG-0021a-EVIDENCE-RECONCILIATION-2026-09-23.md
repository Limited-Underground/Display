# OG-0021a Display evidence and remaining scope

Evidence reviewed 2026-09-23; owner-accepted reconciliation. No new hardware,
vehicle, target, or production acceptance. Display remains optional to base
Trail V1. The required Display product is one protected listen-only vehicle
gateway and at least one gauge showing trustworthy telemetry, warnings and
conspicuous stale/error states without affecting vehicle operation.

## Scope and source reconciliation

This report reconciles existing source and dated evidence. It does not infer
current connected devices or add physical acceptance.

Sources are the [backlog](../../tasks/BACKLOG.md),
[engineering status](../PROJECT_STATUS.md),
[product boundaries](../PRODUCT_BOUNDARIES_V0.md). Code/test presence was
checked in the canonical checkout. Historical test results below are reused
from their linked records, not represented as freshly executed tests.

## Five outcome map

| Checklist outcome | Reusable evidence and layer | Remaining gate and existing child work |
| --- | --- | --- |
| OG-0021: accepted scope, hardware, wiring and evidence | OG-003A inventory identifies candidate roles; OG-012A records vendor display observations and one recovery-first cycle. Product boundaries separate gateway, one required gauge and optional roles. Evidence is planning plus bounded physical candidate observation, not supported hardware. | OG-0021b: owner selects one vehicle/engine, legal signal definitions, gauges/alarms, bus, protected CAN interface, power and environment. OG-0021c: reconcile one exact display revision and recovery/test plan. No vehicle or protected gateway is selected by this report. |
| OG-0022: acquisition, screens, alerts and fault behavior | OG-004 through OG-010D provide host CAN/parser/decoder/cache/telemetry composition; OG-014/014A add local alarms. Display receiver, view model, trends, layouts and renderer coordination have deterministic host tests. OG-017 and OG-018AE/AF provide bounded diagnostics/recovery status. | OG-0022a/b/c bind those existing components to selected acquisition, display/input and diagnostic targets. Actual electrical passivity, rendering/readability and target timing remain unproved. Do not reimplement or reopen completed host tasks. |
| OG-0023: wireless and safe recovery; optional integration decisions | Explicit normalized ESP-NOW contract/codecs, peer authorization, OGL0 layout recovery, and OG-018 recovery components are host-tested. OG-018H-M provide limited host-mediated physical alert/ACK evidence. OG-015 GPS and OG-016A update boot guard are host contracts. | OG-0023a binds authenticated target radio/peer lifecycle; OG-0023b resolves protected keys, independent trusted generation, reset authority and concrete persistence. OG-0023c decides optional GNSS/Trail bridge/OTA transport and allocates any included dependent work. Host metadata or CRC cannot supply physical authentication. |
| OG-0024: supported combinations, startup, loss, thermal and power faults | OG-012A vendor HelloWorld/factory return and cold BOOT recovery are useful candidate evidence. Host gateway/renderer/recovery tests establish expected failure semantics. No selected vehicle, complete target or production endurance evidence exists. | OG-0024a measures exact display and synthetic wireless bench operation; OG-0024b proves selected-vehicle passivity and signal truth; OG-0024c proves power interruption/endurance/environment limits. Each physical operation needs its separate exact setup authorization. |
| OG-0025: setup, calibration, service and release acceptance | Existing component contracts, candidate bring-up and recovery records supply inputs. They do not constitute installation instructions or accepted vehicle/display support. | OG-0025a writes instructions against validated configurations; OG-0025b assembles traceable acceptance/release evidence. Signing/update/recovery policy, target artifacts, supported combinations and service limits must be settled; no package or release is created here. |

## Specific evidence and its limits

### Gateway and signals: reuse OG-010D

The [gateway loop](../gateway/GATEWAY_TELEMETRY_LOOP_V0.md) and
[host tests](../../tests/host/gateway_telemetry_loop_tests.cpp) cover nine groups:
bounded CAN draining, real EEC1 decode/cache/encode through fake radio, no-value
replacement, stale publication during bus-off, same-sequence retry, overflow,
and stop/restart. The actual
[component](../../firmware/components/gateway/src/gateway_telemetry_loop.cpp)
is reusable source, not a target firmware artifact. Synthetic EEC1 is not proof
that a selected vehicle exposes that signal or that its scaling is correct.
Vehicle definitions must be reconciled against legally available documentation
and captured data. Electrical passivity, ISR queues, task timing, watchdogs,
automotive power/protection and failure independence still need selected-target
and vehicle evidence. The `firmware/targets` directory is currently empty.

### Display: reuse OG-012A without promoting the candidate

[Inventory](../../hardware/INVENTORY.md) and the
[Phase-C record](../../tests/hardware/OG-012A-PHASE-C-2026-08-14.md) identify
OG-DISP-001 as the battery-free USB candidate that passed cold BOOT recovery,
verified official HelloWorld programming and visible output, then verified
factory-image programming and a visibly good demo. OG-DISP-002 remained
untouched in that cycle. This does not prove exact received revision, private
original-backup restoration, touch, peripherals, OpenGauge pixels, timing,
memory, stability, power/heat or paired independence. Historical acquisition
statements do not prove present possession or connection; the bench mule and
GNSS candidate remain unverified beyond the dated inventory's ordered status.

The [renderer runtime](../display/GAUGE_RENDERER_RUNTIME_V0.md) and
[exact-generation presentation gate](../configuration/GAUGE_LAYOUT_PRESENTATION_COMPLETION_V0.md)
provide reusable host lifecycle/backpressure/completion semantics. Their tests
use a fake renderer; no pixels, touch, fonts or physical accessibility were
accepted. Historical matrix sizes differ as suites were added; this report
preserves each linked acceptance-time count and creates no new combined count.

### OG-018: separate bridge evidence from recovery infrastructure

[OG-018H](../../tests/hardware/OG-018H-2026-08-09.md) carried normative alerts
and ACKs through external radios with host processing. Later
[OG-018M](../../tests/hardware/OG-018M-2026-08-09.md) retained one real OpenGauge
host process across two role-reversed four-leg retry lifecycles; both ended
with one acknowledgement and no queued/in-flight/terminal-failure entry.
These are close-bench, host-mediated observations. They do not prove an
on-device authenticated Display gateway, ESP-NOW telemetry, restart durability,
vehicle acquisition or field range. Radio completion alone is not an
application acknowledgement, and host-supplied authenticated metadata is not
proof of transport authentication.

OG-018's codec/outbox/replay/authorization and coordinated ORS0 recovery work is
already reusable host implementation. The
[key/value adapter and composed restart tests](../integration/CRITICAL_ALERT_SYSTEM_RECOVERY_KV_TARGET_ADAPTER_V0.md)
record 13 groups, 100 focused repeats and their contemporary 43-executable
matrix. They include applied and unapplied uncertain commits with real boot/save
coordinators, while the trusted generation source remains injected. Ordinary
NVS names do not create protected keys, authenticated integrity or rollback
resistance. OG-018Y's concrete target obligation remains partial. Reuse this
policy/composition where selected by OG-0023b; do not make the optional Trail
bridge a prerequisite for local vehicle gauges merely because its recovery
code exists.

## Core versus optional choices

The core is a passive acquisition gateway, normalized local alarms, authenticated
local telemetry and at least one gauge with safe loss/recovery behavior. Required
safe update/recovery policy is separate from optional OTA delivery transport.

GNSS/time, more displays, a larger-screen alternative, the Trail alert bridge,
diagnostic discovery and auxiliary/APU functions remain separately selected
roles. A compact gauge does not require a larger display. The generic OBD-II
adapter is discovery equipment, not proof of a passive J1939 production gateway.
No vehicle control is included. Only bounded normalized versioned alerts/ACKs
may cross the Trail boundary; raw CAN/J1939, VINs, keys and unrestricted text
remain excluded. Loss of an optional role must not disable local gauges or the
base Trail communication path.

## Next choices in plain English

1. Use the accepted evidence map. Completed host work remains completed;
   target integration and physical validation are the missing layers.
2. Review OG-0021b's first vehicle, desired readings/warnings and protected
   gateway design. Until those inputs exist, no vehicle connection is justified.
3. Review OG-0021c's exact display candidate and remaining vendor/synthetic
   test plan, preserving the second unit. This is planning before a separately
   authorized hardware session.
4. Decide optional release features at OG-0023c after its listed dependencies;
   included features require their own bounded work and evidence. Do not expand
   the first vehicle/display support matrix by inference.

## Validation and closeout

This documentation checkpoint reconciles scope, five-outcome coverage, source
and test paths, the empty target directory and linked historical records.
Historical test results remain tied to their original evidence; no completed
host matrix was rerun and no source, target, vehicle or hardware behavior changed.
No V1 completion or public website status changed. The progress record predates
some later evidence; this reconciliation does not recalculate it.

The publication preparation checks relative links, privacy and whitespace on the
selected documentation. The repository does not provide
`tools/check_repository_docs.py` or `tests/host/repository_docs_tests.py`.
