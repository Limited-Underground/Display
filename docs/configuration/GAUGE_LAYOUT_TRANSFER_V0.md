# Gauge Layout Transfer Composition v0

Status: deterministic host-tested composition, updated 2026-08-12. This is not
a file-transfer protocol, network endpoint, removable-media workflow,
device/user authorization policy, authenticity mechanism, or physical-device
result.

## Boundaries used

Transfer uses only the existing memory-level contracts:

1. source `GaugeLayoutStore::export_current` selects and encodes one canonical
   576-byte `OGL0` record;
2. destination `GaugeLayoutChangeWorkflow::stage_import_record` validates and
   previews it without writing;
3. exact destination-local confirmation consumes the request and calls the
   normal store-owned update path;
4. destination restart uses normal two-slot selection; and
5. destination export reuses the same canonical exporter.

There is no privileged copy, direct slot write, or caller-selected destination
generation.

## Generation ownership

The exported source generation remains preview metadata. It cannot roll back,
skip, or exhaust destination generation state. In the host proof, source
generation 2 is imported into a destination whose active generation is 10.
Exact confirmation stores the transferred content as destination generation 11.

An identical later import still requires a fresh local confirmation request,
but the existing semantic comparison returns unchanged at generation 11 and
performs no additional write.

## Evidence

The cumulative eleventh layout-change workflow group proves:

- source generation 2 exports from the newest valid source slot;
- destination generation 10 remains the only write during preview;
- exact confirmation produces destination generation 11 with zero erases;
- restart selects generation 11 and transferred content;
- a new confirmed identical import performs no write; and
- destination canonical export decodes as generation 11 with the transferred
  content.

The focused suite passes 100/100 repeats plus the complete 47-executable host
matrix including publication safety. File or network transport,
source/destination authorization and authenticity, target UI, interruption
cleanup, and physical hardware remain required.
