JMA QA-COVERAGE REPORT (round 2)

Root reviewed: whole repo (focus: src/core, src/core/provisioning)
Test framework: GoogleTest — `make test` → `ctest --output-on-failure -L unit`
                (Docker dev container; `SipEngineLifecycle` excluded — known
                pjsua2 audio-init segfault on Linux x86_64, per CLAUDE.md)
Branch: jma/qa-coverage-2026-05-30-2  (second same-day run; the first-run branch
        was already merged + deleted, so this one is suffixed -2)
Suite: unit label → GREEN, 194/194 pass (10 new tests, 0 failures, no regressions)
Coverage: tool not configured (no gcov/lcov wired) — reporting test-count delta:
          184 → 194 unit tests (+10)

SUMMARY
- This is the SECOND pass. The tree is in genuinely good shape: the enum/string
  converters, Database migrations, UpdateChecker, the six list models, manager
  CRUD round-trips, SipUri, keychain, ContactImporter, and the AccountsManager
  credential seam (added in round 1) are all covered.
- Gaps found by test-gap-analyst: 4 unit-testable (2× MEDIUM in DaktelaProvider,
  2× LOW in AccountsManager persistence) + 1 blocked (CallsModel.refresh, needs a
  snapshot seam — not a test-only change).
- Tests added this run: 10 across 2 files (both appended to existing test units;
  no CMakeLists change needed).
- All 10 graded APPROVE / BITES by the qa-engineer gate; one suggested adjacent
  case (empty-name fallthrough) was added before keeping them.
- No suspected bugs surfaced this round — every pinned behaviour looked correct.

TESTS ADDED
- tests/unit/test_daktela_provider.cpp (+8) —
  src/core/provisioning/DaktelaProvider.cpp buildAccountParams (mapSrtp/mapDtmf/
  mapTransport) and extractExtensionName — risk: MEDIUM (untrusted provisioning
  input → media-security posture and which SIP identity is provisioned).
    · explicit srtp:false → "disabled" even on TLS (false != optional)
    · explicit srtp:true → "required" even on UDP (bool wins over transport)
    · dtmfmode "info"/"sip-info" → info; "inband" → inband
    · transport "TCP"/"Tls" case-folded to tcp/tls
    · extension object resolved via the extension_sip_device alt-key
    · empty "name" falls through to the next key (the !isEmpty() guard)
    · numeric "name" coerced to its integer string ("1001"), not dropped
    · non-object whoim result → empty + error set
- tests/unit/test_accounts_manager_update.cpp (+2) —
  src/core/AccountsManager.cpp add()/update() persistence round-trips — risk: LOW
  (data integrity; zrtpEnabled is media-encryption-relevant). Proven via a second
  AccountsManager cold-loading from the shared in-memory SQLite DB (not a cache
  re-read).
    · add() round-trips zrtpEnabled + codecs + authRealm (omitted by the existing
      full-account round-trip)
    · update() round-trips transport/srtpMode/allowUntrustedCert/zrtpEnabled/
      codecs/authRealm (the update bind path was only proven for 3 fields)

SUSPECTED BUGS  (a test pinned behaviour that looks wrong — confirm intended)
- none this run.

REMAINING GAPS  (ranked backlog, not covered this run)
- [HIGH] src/models/CallsModel.cpp::refresh — the most complex untested model
  logic (incremental remove/insert/in-place-diff with per-role dataChanged).
  Blocked: CallsModel holds a concrete sip::CallManager* and calls snapshot();
  CallManager's ctor pulls in pjsua2. Suggested SEPARATE refactor (not a
  test-only change): extract a pure `CallSnapshotSource { virtual
  std::vector<CallEntry> snapshot() const = 0; }`, have CallsModel hold that
  interface, then a fake source feeding scripted snapshots makes the diff
  unit-testable (row removed on id disappear, in-place dataChanged only for
  changed roles, append on new id) with zero pjsua2.
- [CRITICAL] src/core/CallManager.cpp — call state machine / unhold retry /
  transfer cleanup. Still needs a seam to split pure state-tracking from pjsua2.
- [CRITICAL] src/core/CallsController.cpp::dial / [HIGH] startRecording —
  untrusted target → dialed-URI resolution and recording file-path. Blocked on
  the same concrete-CallManager dependency.
- [HIGH] src/core/LinesManager.cpp — BLF presence derivation lives in a pjsua2
  callback (AccountImpl::onBuddyState); only the enum→string contract is pinned.
- [MEDIUM] src/core/AccountsController.cpp::applyParams — QVariantMap→Account
  enum coercion; currently file-static (anonymous namespace), expose a thin
  wrapper to test enum fallback.
- [LOW] src/core/BootConfig.cpp::resolvePassword "@stdin" branch — only
  resolvePassword branch still untested (reads stdin; awkward).

---
Branch jma/qa-coverage-2026-05-30-2 holds the 10 new tests + this report. Nothing
was pushed or merged, and no product code was changed. The highest-value next
step is the CallSnapshotSource seam, which unlocks unit testing of both
CallsModel.refresh and (with sibling seams) the CallManager/CallsController
call logic. Re-run /jma:qa-coverage to keep climbing.
