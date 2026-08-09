# Security Policy

OpenGauge is pre-release research software. It is not suitable for
safety-critical vehicle control or as the sole source of vehicle warnings.

## Reporting a security concern

Do not include credentials, private keys, pairing secrets, vehicle-specific
identifiers, sensitive captures, or exploit details in a public issue. Prefer
a private GitHub Security Advisory when that feature is enabled. Otherwise,
contact the repository owner privately before sharing sensitive details.

## Current security boundaries

- Initial CAN/J1939 work is passive and listen-only.
- Pairing, encryption, key storage, replay protection, OTA signing, and
  recovery policies are not finalized.
- Displayed warnings are supplemental instrumentation; stale, missing, or
  invalid source data must remain conspicuous.
- APU and auxiliary control are outside the core telemetry path and require a
  separate safety and authorization design.
- No firmware or hardware configuration should be described as secure,
  automotive-qualified, or production-ready without documented validation.
