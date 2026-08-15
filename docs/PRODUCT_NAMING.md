# Limited Underground Display Product Naming

Status: owner-approved working naming decision, 2026-08-14. These names are
locked for current planning and implementation, but remain provisional pending
comprehensive trademark clearance. They are not registered marks, and this
decision does not establish hardware support or production readiness.

## Customer-facing hierarchy

| Role | Locked working name | Usage boundary |
| --- | --- | --- |
| Parent brand | **Limited Underground** | The maker and primary brand across the product family |
| Vehicle firmware and software family | **Limited Underground Display** | Vehicle telemetry, warnings, gauges, display software, and associated firmware |
| Tested hardware-and-software package | **Limited Underground Display Premium Bundle** | Use only for an exact hardware and software combination that has passed its documented compatibility and acceptance gates |
| Vehicle acquisition component | **Display Gateway** | A component name inside a bundle; present it as a separate public product only if it is actually offered separately |
| Shared firmware utility | **Limited Underground Firmware Loader** | The customer tool for compatible-device inspection and, only after validation, firmware installation and recovery |

Until firmware writing and recovery have passed documented physical-hardware
acceptance, the utility must be presented as:

> **Limited Underground Firmware Loader — Preview**
>
> Inspection only. Firmware installation is not yet available.

The loader name is shared across Limited Underground product families. This
OpenGauge repository records the vehicle-product relationship but does not own
or absorb another project's source, artifacts, or private planning.

## Stability boundary

The public naming decision does not rename the engineering project. Preserve:

- the `OpenGauge` repository and folder name;
- `opengauge` source namespaces and include paths;
- existing `OG-` work-item, protocol, fixture, evidence, and inventory IDs;
- wire magic, versions, schema names, storage keys, cryptographic context, and
  compatibility identifiers;
- board identifiers and recovery records.

Customer-facing wording must remain outside those stable technical contracts so
future branding changes do not break compatibility, persistence, recovery, or
test evidence.

## Release safeguards

- Do not use the registered-mark symbol.
- Do not imply that the working names have completed legal clearance.
- Do not call candidate or partially tested hardware a **Premium Bundle**.
- Do not advertise **Display Gateway** as a separately purchasable product
  unless that offering is deliberately created and supported.
- Remove **Preview** and the inspection-only limitation from the Firmware
  Loader only after real flashing and recovery pass their documented acceptance
  gates on every claimed target.
