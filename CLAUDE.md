# CLAUDE.md — orientation for future sessions

This file gives you what the codebase itself can't: which gotchas to
expect, where the load-bearing decisions live, and which paths look
obvious-but-are-traps. Read once at session start.

## What this is

**Compact Phone** — a Qt6 + QML desktop SIP softphone using PJSIP /
PJSUA2 as the audio stack. Targets macOS, Windows, Linux. GPL-3.0-or-
later because PJSIP forces it. Solo-maintained, public repo at
`github.com/marwain91/compact-phone`. Active branch is `main`;
`v0.1-internal-alpha` keeps the granular pre-public history archived.

## Tree map (the non-obvious parts)

```
src/core/                  C++ controllers + managers — the brain
  provisioning/            Pluggable auto-provisioning (Daktela today)
src/ui/qml/                QML views; Main.qml is the window shell
tools/dev/                 Linux dev container (Dockerfile + compose)
tools/dev/triplets/        Release-only vcpkg triplets for Linux (see below)
cmake/FindPJSIP.cmake      Locates PJSIP per-platform — pkg-config on
                           Linux/macOS, env-var glob on Windows
.env.local.example         Template for remote-build VPS hostname
                           (real .env.local is gitignored)
web/docs/pages/            Markdown docs MyWebs pulls into the live site
```

## Build paths

| Target | Command | Where it runs | Why |
|---|---|---|---|
| Linux dev / CI | `make build` | Local Docker dev container | Headless, reproducible |
| macOS native | `make build-macos` | Host (needs Xcode, brew deps, native PJSIP) | Only way to produce `.app` |
| Linux on a VPS | `make remote-build` | Docker on VPS (rsync + ssh) | Heavy compiles off the laptop |
| Windows MSI | GitHub Actions only | `windows-2022` runner | No good local-on-Mac option |
| Tests | `make test` (Linux) / `ctest` (Mac) | Same context as the build | — |

## Things that surprised me; will surprise you

### vcpkg binary cache already works — but per-triplet

The default vcpkg binary cache at `~/.cache/vcpkg/archives/` is
populated and reused across builds **on the same triplet**. macOS uses
`arm64-osx`, Linux dev container uses `arm64-linux` (Apple Silicon
host) or `x64-linux` (Intel/AMD64), Windows uses `x64-windows`. None
of these caches transfer to the others.

If you see vcpkg "rebuilding Qt" unexpectedly, the cause is almost
never "no cache" — it's "your triplet's ABI hash changed". Common
triggers: `vcpkg.json` feature-list edit, vcpkg baseline bump,
overlay triplet added (the release-only fix below).

### The release-only Linux triplet

`tools/dev/triplets/{x64,arm64}-linux.cmake` shadow vcpkg's
built-ins with `set(VCPKG_BUILD_TYPE release)`. Without this, a
cold Linux build of `qtdeclarative` peaks at ~30 GB of buildtree
(Debug variant of every Qt sublibrary, with `-g3`, ~350 MB per PCH
file × 19 sublibraries). With it, peak is ~10 GB.

The Linux CMake preset wires the overlay via `VCPKG_OVERLAY_TRIPLETS`.
**macOS and Windows are NOT overlaid** — release-only on those is
unnecessary (cache works fine on Mac, Windows binary is release
anyway). Don't add overlay files for `arm64-osx` or `x64-windows`
unless someone reports a similar disk-pressure issue there.

### PJSIP is built out-of-band on every platform — there is no vcpkg port

PJSIP is not in vcpkg upstream. We build it manually:

- **Linux dev container**: `tools/dev/Dockerfile` calls `./aconfigure
  --prefix=/opt/pjproject ... && make install`. The PJSIP `.pc` file
  ends up at `/opt/pjproject/lib/pkgconfig` and our
  `FindPJSIP.cmake` picks it up via `pkg-config`.
- **macOS host**: `make pjsip-macos` does the same, installing to
  `$HOME/pjproject/`. CI's `build-macos` job inlines the same steps.
- **Windows runner**: `.github/workflows/ci.yml` `build-windows`
  downloads PJSIP and msbuilds `pjproject-vs14.sln` against the
  v143 toolset. `PJSIP_ROOT` env var is exported; `FindPJSIP.cmake`
  globs `.lib` files from the per-component lib dirs.

Watch out for: a stale `windows-pjsip` feature in `vcpkg.json` (now
removed) referenced a non-existent vcpkg port. Don't add it back.

### `libb2` needs `autoconf-archive` — both in the dev container and on macOS/Linux CI runners

vcpkg's `libb2` portfile runs `autoreconf`, which needs `AX_*` m4
macros that ship in the `autoconf-archive` package — NOT the base
`autoconf`. Missing this manifests as a cryptic libb2 BUILD_FAILED.

- `tools/dev/Dockerfile` apt-installs it
- `.github/workflows/ci.yml`'s macOS job `brew install`s it
- Linux CI job builds the dev container so it inherits the Dockerfile fix

If you add a new build environment, install `autoconf-archive` or
expect libb2 to fail on first cold install.

### gitleaks-action v2 needs `GITHUB_TOKEN`, not `GH_TOKEN`

`.github/workflows/secret-scan.yml` passes `GITHUB_TOKEN`. The action
silently ignores `GH_TOKEN` and fails on every PR with "GITHUB_TOKEN
is now required to scan pull requests" if you change it.

### Default ssh + non-interactive => first-time host key rejection

`ssh almalinux@new-host hostname` (with a command argument)
non-interactively rejects unknown host keys without prompting. Run
`ssh almalinux@new-host` (no command) once interactively to accept
the key, then non-interactive commands work. The `make remote-*`
targets fall into this trap; the troubleshooting note in
`tools/dev/remote-builds.md` covers it.

### Disk-pressure trap on shared VPSes

A cold Linux PJSIP+Qt build on a tight-disk VPS hits ENOSPC during
qtdeclarative if you don't use the release-only triplet. vcpkg keeps
the debug buildtree resident while the release variant builds, so
peak disk is debug + release simultaneous (~22 GB combined for
qtdeclarative alone).

If you're remote-building somewhere that might be production-shared,
warn before launching; check `df -h` first.

## Branch + PR conventions

- **`main` is protected** — direct pushes blocked, force-push blocked,
  deletion blocked. PR is the only way in.
- **Squash-merge is the only merge method.** Repo-level setting +
  ruleset-level `pull_request.allowed_merge_methods: ["squash"]`
  enforce this redundantly.
- **`required_linear_history`** keeps `main` a straight line.
- **`delete_branch_on_merge: true`** auto-deletes feature branches.
- PR title becomes the squash commit subject; PR body becomes the
  commit body. Write both with that in mind.
- **No required reviews** today (solo project). If a second
  maintainer joins, bump `required_approving_review_count` to 1.

When chaining work: stack PRs by basing one feature branch on
another. After the base merges, GitHub rebases the stacked PR onto
main automatically (or you do `git rebase main` and force-push).

## Things I avoid

- **Self-hosted GitHub Actions runners on this public repo.** Fork
  PRs can execute arbitrary code on the runner host — known supply-
  chain risk. The `make remote-*` flow uses plain SSH from the
  maintainer's machine instead.
- **Force-pushing `main`** — the protection ruleset blocks it; don't
  try to work around with admin bypass.
- **`--no-verify` on commits** — gitleaks pre-commit hook is the
  last line of defence against accidentally pushing secrets to a
  public repo. Always run it.

## Where to find more

- `tools/dev/remote-builds.md` — daily workflow for VPS offload
- `tools/dev/triplets/README.md` — why the release-only triplet, what
  we lose and don't lose
- `web/docs/pages/*.md` — user-facing docs (configuration,
  provisioning, troubleshooting, FAQ)
- `Makefile` — every target has a `## help` line; `make help` lists them
