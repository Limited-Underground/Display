# Critical Alert ACK Rejection Policy v0

Status: deterministic host-tested retry/terminal policy, 2026-08-09. This is
not a physical transport result, operator notification design, or persistent
delivery journal.

## Boundary

This policy runs only after `CriticalAlertAckIngress` has authenticated,
authorized, decoded, replay-checked, and exactly correlated a rejected `OGK0`
frame to a retained `OGA0` event with at least one locally accepted attempt.
An untrusted, malformed, mismatched, or replayed frame cannot invoke it.

The rejection is always explicit non-success. The outbox successful
acknowledgement counter is never incremented.

## Canonical actions

| `OGK0` reason | Action | Rationale |
| --- | --- | --- |
| `unauthorized` | terminal | Reconfiguration or local approval is required; automatic traffic must stop |
| `stale` | terminal | Retrying cannot make the same event younger |
| `duplicate` | terminal | The consumer intentionally refused this duplicate; it is not treated as accepted |
| `conflict` | terminal | The same event identity has incompatible content or lifecycle |
| `rate_limited` | retry | Transient capacity may recover after the configured outbox backoff |
| `malformed` | terminal | Repeating the same canonical event cannot repair a semantic refusal |
| `unsupported` | terminal | Version/type/policy support requires a configuration or software change |
| `internal_error` | retry | A bounded retry may succeed after a transient consumer failure |

Retry never resets the event enqueue time or attempt count. It cancels any
prepared-but-not-yet-committed send, queues the retained event at
`now + retry_backoff`, and remains subject to maximum lifetime. If the locally
accepted attempt count already equals the configured maximum, a retryable
reason becomes terminal instead.

Terminal removal emits fixed typed evidence containing event ID, condition ID,
`remote_rejection`, the canonical remote reason, and locally accepted attempt
count. Operator wording and severity are deliberately outside this component.

Replay sequence state is committed only after the outbox transition succeeds.
A clock or correlation failure leaves the sequence available for a valid frame.

## Host evidence

`tests/host/critical_alert_ack_rejection_policy_tests.cpp` covers eight groups:

1. one deterministic action for all eight reasons;
2. the exact normal retry-backoff boundary;
3. retryable rejection becoming terminal at the attempt limit;
4. typed terminal correlation/reason/attempt evidence;
5. a late retryable rejection cancelling a currently prepared send;
6. stopped, reason-none, and mismatched direct calls remaining atomic;
7. retry remaining subject to original maximum lifetime; and
8. one successfully applied rejection consuming its replay sequence once.

The full OpenGauge host matrix passes. The policy, affected ingress, and outbox
suites each repeated 100 times with zero failures.

## Remaining gates

- add redacted operator-visible presentation and rate-limited diagnostics;
- persist outbox and replay state coherently if restart survival is required;
- define the target task/adapter ownership and concurrency boundary; and
- validate authenticated physical delivery, delayed/reordered responses,
  restart, channel change, and repeater behavior.
