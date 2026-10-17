# OG-0021c Display candidate recovery-plan diagnosis

Evidence review: 2026-10-12. Status: **BLOCKED**; task completion is not established.

## Smallest unresolved prerequisite

The exact received-board revision is **UNKNOWN**. The existing arrival photos do
not show it, and the later Phase-B/Phase-C records explicitly do not establish
it. Procurement mapping SKU 31262 and the public model label
`ESP32-S3-Touch-AMOLED-1.75-B` cannot substitute for a received-board revision.
A recovery plan that claims the exact candidate is fully identified would be
unsupported. No vendor lookup or device access was performed to fill this gap.

## Already-held evidence and its limits

- [Inventory](../../hardware/INVENTORY.md) and
  [bring-up procedure](../../hardware/WAVESHARE_31262_BRINGUP.md) identify the
  intended standard non-GPS, protective-case candidate; they retain the exact
  revision gate.
- [Arrival record](../../tests/hardware/OG-012A-ARRIVAL-2026-08-14.md): the package
  model is visible, but SKU/revision and package-to-unit association were not
  established in that observation.
- [Phase-B record](../../tests/hardware/OG-012A-PHASE-B-2026-08-14.md): a
  16,777,216-byte original full-flash read was hashed and retained privately
  outside Git. Its hash and location are intentionally absent from public
  evidence. It was not restore-tested, and the preexisting application was not
  identified byte-for-byte. This review does not verify that private file's
  current presence, integrity, custody, or candidate association.
- [Phase-C record](../../tests/hardware/OG-012A-PHASE-C-2026-08-14.md): the operator
  associated `OG-DISP-001` with the active battery-free USB candidate, confirmed
  cold BOOT recovery, and preserved disconnected `OG-DISP-002`. This is a dated
  association and power-state observation, not current device verification.
  The original private backup was neither used nor restore-tested.

## Historical rollback boundary

Phase C records successful offset `0x0` writes, esptool hash verification and
visual application return for the official HelloWorld combined image followed
by the official factory image. The documented battery-free fallback is full
USB disconnection, confirmation that the unit is off, holding BOOT while
reconnecting USB, observing the ROM interface, releasing BOOT, then reconnecting
without BOOT and visually confirming application return. This is historical
procedure/evidence, not authorization or a newly executed recovery result.

The factory image is a vendor rollback image. It is not the private original
image and does not prove restoration of original bytes. The dated artifact
sizes and SHA-256 values in Phase B/C are historical evidence; none was
rehashed in this task. No current installed image is established by those
records.

## Completion boundary and next gate

This diagnosis stops before preparing a complete recovery test plan. OG-0021c
requires an exact received-board revision and a current private original-image
provenance/restore decision; neither is resolved by the reviewed evidence.
The smallest next action is to supply already-held revision evidence bound to
`OG-DISP-001`, or separately authorize a bounded identity-only observation.
If no existing revision evidence exists, task completion remains blocked.
Any later original-image custody verification or restore trial must retain its
private evidence and obtain its own applicable authorization.

After those prerequisites are resolved, a separately scoped plan must specify
display/color/geometry, touch/rotation, PMIC, IMU, RTC, LVGL resource/timing,
USB power, heat/stability, synthetic stale/error presentation and repeat
recovery gates. These remain untested; no numerical compatibility threshold
or peripheral result is invented here. The retained second candidate must stay
separate from the active candidate. Vehicle access and firmware implementation
are outside this diagnosis.

## Host validation scope

Approved checks for this document are the publication-safety scanner scenarios,
the tracked-content publication scan, a direct `scan_text` check of this new
untracked report, `git diff --check`, and manual relative-link resolution.
`tools/check_repository_docs.py` and `tests/host/repository_docs_tests.py` are
absent in this checkout, so their documentation validation gate is unavailable.
Validation results are reported to the task controller after execution.

Only this diagnosis file was added. Existing bring-up instructions, backlog
owner edits, other dirty files, private artifacts and progress are preserved.
No hardware, firmware/source, native application, publication or website action
was performed. Host success cannot establish physical acceptance; accepted
hardware evidence and public website capability status did not change.

## Independent diagnosis review - 2026-10-12

The separate display_recovery_review reviewer approved this blocked diagnosis
only, with no substantive findings. OG-0021c completion remains blocked. The
reviewer independently inspected the live revision-2 criteria and source records,
confirmed five contained links, and kept the private backup unverified rather
than claimed missing. Factory rollback remains distinct from original restoration.
Reviewed report before this review record: 4,812 bytes; SHA-256
4BCACAF6A1DE966B0BB0C00EBE22A11F37B6D4A86A6511B47D51364C19B562E3.
Publication-safety tests, tracked scan, direct untracked-report scan and diff
check passed. This review grants no physical acceptance or execution authority.
