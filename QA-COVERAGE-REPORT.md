JMA QA-COVERAGE REPORT

Root reviewed: whole repo (focus: src/core)
Test framework: GoogleTest — `make test` → `ctest --output-on-failure -L unit`
                (Docker dev container; `SipEngineLifecycle` excluded — known
                pjsua2 audio-init segfault on Linux x86_64, per CLAUDE.md)
Branch: jma/qa-coverage-2026-05-30
Suite: unit label → green for all new tests (182/183 pass; the 1 red is a
       pre-existing stale test, see PRE-EXISTING FAILURE below — NOT introduced
       here)
Coverage: tool not configured (no gcov/lcov wired) — reporting test-count delta:
          169 → 183 unit tests (+14)

SUMMARY
- Gaps found (test-gap-analyst): ~10, concentrated in stateful managers with
  zero unit test (CallManager, CallsController, AccountsManager password seam,
  LinesManager BLF) plus a few error/edge branches in tested files.
- Tests added this run: 14 across 2 files.
- Several CRITICAL gaps (CallManager / CallsController.dial / startRecording)
  are NOT cleanly unit-testable: those classes are concrete (no fakeable seam)
  and route straight into pjsua2, which segfaults headless. They need an
  interface seam or the integration suite, not this push. Left as backlog.
- The realistically unit-testable, highest-blast-radius gap — the
  AccountsManager credential (keychain) seam — is now covered. The BootConfig
  IPv6 host:port assembly and resolvePassword failure branches are covered and
  surfaced one latent bug.

TESTS ADDED
- tests/unit/test_accounts_manager_password.cpp (NEW, 8 tests) —
  src/core/AccountsManager.cpp::{add,setPassword,remove,passwordRefFor} and the
  keychain/password lifecycle — risk: CRITICAL (credential storage).
    · password stored under the account ref on add (incl. empty-password case)
    · setPassword replaces the keychain value and keeps the SAME ref
    · setPassword / remove on an unknown id return false (no crash) and resolve
      to no ref
    · remove ERASES the stored secret — no stale credential survives deletion
    · a rotated password round-trips through the DB row to a reloaded manager
      (ref compared across the manager boundary, so a password_ref column bug
      would bite)
- tests/unit/test_bootconfig.cpp (APPENDED, 6 tests) —
  src/core/BootConfig.cpp::{serverWithOptionalPort,hasPort,resolvePassword} —
  risk: HIGH (untrusted provisioning input → registrar address / secret).
    · bare IPv6 + --sip-port is bracketed → [2001:db8::1]:5060
    · pre-bracketed IPv6 with port, and host:port, ignore --sip-port (unchanged)
    · pre-bracketed IPv6 WITHOUT port is double-bracketed (characterizes the bug
      below)
    · resolvePassword degrades to empty (not throw) for a missing @file: and an
      unset @env:

All 14 graded BITES by the qa-engineer gate (no vacuous/mock-only/snapshot-only
tests); two tightenings it requested were applied before keeping them.

SUSPECTED BUGS  (a test pins behaviour that looks wrong — confirm intended)
- src/core/BootConfig.cpp::serverWithOptionalPort / hasPort —
  An IPv6 registrar supplied already-bracketed but without a port
  (`--sip-server "[2001:db8::1]" --sip-port 5060`) yields
  `[[2001:db8::1]]:5060` — a double-bracketed, unconnectable address.
  Cause: hasPort("[2001:db8::1]") is false (no "]:"), so the value falls through
  to the "contains ':' → wrap in [ ]" branch and gets bracketed a second time.
  Expected: detect a fully-bracketed host (starts "[", ends "]") and append the
  port as `[2001:db8::1]:5060`.
  Pinned by BootConfigTest.AlreadyBracketedIpv6WithoutPortIsDoubleBracketed —
  the test comment says to flip the expectation once fixed. Low real-world
  likelihood (operators rarely hand-bracket then also pass --sip-port) but it is
  a genuine address-mangling bug. Recommend filing an issue and linking it from
  the test comment (the comment currently points here, not to a ticket).

PRE-EXISTING FAILURE  (not introduced by this run; not fixed here — product/test
code is out of scope for qa-coverage)
- tests/unit/test_release_packaging.cpp ::
  ReleasePackaging.WindowsReleaseSkipsProductionArtifactWithoutSigningSecret —
  asserts release-windows.yml still emits "Skipping Windows production artifact
  because CODE_SIGN_THUMBPRINT is not configured". Per CLAUDE.md the Windows
  production gate was migrated to Azure Artifact Signing and now checks
  TRUSTED_SIGNING_PROFILE, not CODE_SIGN_THUMBPRINT — so this test is stale and
  red on main independent of this work. It should be updated to assert the new
  TRUSTED_SIGNING_PROFILE gate message (separate change).

REMAINING GAPS  (ranked backlog, not covered this run)
- [CRITICAL] src/core/CallManager.cpp (871 lines, zero unit test) — call state
  machine, unhold retry loop, transfer-status handling, grace-period cleanup.
  Needs the pure state-tracking pieces split from pjsua2 (or a seam) before they
  can be unit-tested; integration tests cover the live path but require Asterisk.
- [CRITICAL] src/core/CallsController.cpp::dial — untrusted target → dialed-URI
  resolution (wrong identity / sips→sip downgrade). Blocked: depends on concrete
  CallManager/AccountsManager (no fakeable seam) and calls into pjsua2. Extract
  an interface or a pure "resolve target" helper to make it unit-testable.
- [HIGH] src/core/CallsController.cpp::startRecording — recording file-path /
  state transition. Same blocker as dial (concrete CallManager dep).
- [HIGH] src/core/LinesManager.cpp — BLF/watched-line presence derivation. The
  PJSIP→LineState mapping lives in AccountImpl::onBuddyState (pjsua2); only the
  enum→string contract is unit-pinned today. Needs the mapping factored out of
  the pjsua2 callback to test the busy/ringing/idle/unknown transitions.
- [MEDIUM] src/core/AccountsController.cpp::applyParams — QML QVariantMap →
  sip::Account coercion (unknown enum strings, type mismatches). Currently in an
  anonymous namespace; expose it (or a thin wrapper) to test enum fallback
  without the full controller.
- [MEDIUM] src/core/BootConfig.cpp::resolvePassword "@stdin" branch — the only
  resolvePassword branch with no test (awkward: reads stdin). Deferred.
- [LOW] NetworkMonitor / TrayController / RingtonePlayer — thin Qt/OS-binding
  wrappers, low logic density; not worth chasing.

---
Branch jma/qa-coverage-2026-05-30 holds the 14 new tests + this report. Nothing
was pushed or merged, and no product code was changed. Re-run /jma:qa-coverage
to keep climbing — the highest-value next step is adding a seam to CallManager /
CallsController so the call-state and dial logic become unit-testable.
