# Contributing to OpenGauge

OpenGauge is an early architecture and proof-of-concept project released under
the [Apache License 2.0](LICENSE). Contributions are welcome, but vehicle data,
hardware, display, and safety claims must remain proportional to their
evidence.

## Before starting

- Open an issue before a large architecture, protocol, safety, hardware, or
  dependency change so its scope can be reviewed.
- Never post credentials, pairing secrets, private keys, vehicle-specific
  identifiers, precise private locations, or sensitive vehicle captures.
- Distinguish proposed behavior, synthetic/captured-frame tests, bench
  evidence, vehicle evidence, and supported hardware.
- Include exact hardware, wiring, termination, bus conditions, firmware,
  toolchain, limitations, and observed results with physical-test claims.
- Keep early CAN work passive and listen-only. Do not submit vehicle-control
  behavior without a separately reviewed fail-safe design.

Repository work must follow [AGENTS.md](AGENTS.md), the architecture boundaries
in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and the acceptance criteria in
[tasks/BACKLOG.md](tasks/BACKLOG.md).

## Pull requests

1. Keep each pull request bounded to one coherent change.
2. Add deterministic malformed, boundary, timeout, and compatibility tests for
   parser, codec, cache, alarm, and protocol behavior.
3. Update the backlog and project status whenever evidence or a design
   constraint changes.
4. Run `tools/Test-Host.ps1` and report the exact result. Report skipped
   hardware or vehicle checks as unverified rather than passed.
5. Describe user impact, safety implications, compatibility effects, and
   remaining limitations in the pull request.

By intentionally submitting a contribution for inclusion in OpenGauge, you
agree that it is licensed under Apache-2.0 as described by section 5 of the
license. Only submit work that you have the right to contribute. No separate
contributor license agreement is currently required.

Security vulnerabilities and sensitive findings must follow
[SECURITY.md](SECURITY.md), not a public issue.
