# Security policy

## Supported versions

Compact Phone is on a rolling release; the latest tagged release is the
only supported version. Security fixes are not back-ported to older tags.

## Reporting a vulnerability

**Do not file public issues for security bugs.** Please report privately
to:

- Email: **security@compactphone.eu** (PGP key fingerprint:
  `TBD — populate before public launch`)
- GitHub Security Advisories: use the **Security → Report a
  vulnerability** button on the repository.

Include:

- Compact Phone version + platform.
- A clear description of the vulnerability.
- A proof-of-concept if you have one.
- Whether you believe the bug is exploitable remotely or only locally.

## Disclosure timeline

- **Day 0** — report received; acknowledgement within 48 hours.
- **Day 1–14** — investigation, reproducing, drafting a fix.
- **Day 15–45** — coordinated release of fix + advisory. We aim to
  publish within 45 days; high-severity bugs may move faster.

If you don't hear back within 5 business days, please follow up — your
report may have been caught by a spam filter.

## Scope

In scope:

- The Compact Phone application binary on macOS / Windows / Linux.
- The provisioning / config-file loader.
- The SIP signaling and media handling (anything we directly link or
  wrap from PJSIP).
- Stored credentials (file keychain, native keychain shims).

Out of scope:

- Vulnerabilities in PJSIP itself — report to the
  [PJSIP project](https://github.com/pjsip/pjproject/security).
- Vulnerabilities in Qt — report to
  [the Qt Project](https://www.qt.io/security).
- Social engineering / phishing of project maintainers.
- Denial of service that requires an attacker on the local SIP signaling
  path; the SIP stack inherits PJSIP's assumptions about adversarial
  proxies.
