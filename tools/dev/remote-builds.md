# Remote builds — offload heavy compiles to a VPS

The local `make build` runs inside a Linux dev container on your laptop.
For long compiles (Qt rebuild after a vcpkg feature change, full clean
build, etc.) the container can run on a remote Linux VPS instead.
The flow stays identical from the developer's perspective — you just
prefix the make target with `remote-`:

```
make build           # build in local Docker
make remote-build    # build in Docker on $REMOTE_BUILD_HOST
```

## One-time setup

1. **Provision the VPS.** Any Linux box with `docker` + `docker compose`
   installed and an `sshd` you can reach with key auth. No agents, no
   GitHub runners — just plain SSH.

2. **Copy the env template and fill in your host.** `.env.local` is
   gitignored so the actual hostname never lands in the public repo:

   ```bash
   cp .env.local.example .env.local
   $EDITOR .env.local      # set REMOTE_BUILD_HOST=user@vps.example.com
   ```

   Optional: also set `REMOTE_BUILD_PATH` if you want a non-default
   workspace location on the VPS (default `/srv/compactphone-build`).

3. **Verify connectivity.**

   ```bash
   make remote-status
   ```

   First run will print `(workspace not synced yet)` — that's normal.
   Any SSH or permission error surfaces here.

## Daily flow

```bash
make remote-up        # rsync source + bring up dev container on VPS
make remote-build     # rsync deltas + cmake build remotely
make remote-test      # rsync deltas + ctest remotely
make remote-shell     # interactive shell inside the remote dev container
make remote-down      # stop the remote dev container
```

Every command does an incremental `rsync` first, so local edits land on
the VPS in seconds. The remote `build/` directory is **never** clobbered
by sync — incremental compiles work as you'd expect.

## What's *not* synced (deliberately)

- `build/`, `build-*/` — VPS keeps its own build tree
- `vcpkg_installed/` — vcpkg manages this; remote has its own cache
- `.git/` — VPS doesn't need history, only working tree
- `.env.local` — host-specific secrets stay host-local
- `.vscode/`, `.idea/`, `.DS_Store` — IDE/OS noise

If you need to ship something exotic, edit `_RSYNC_EXCLUDES` in the
Makefile.

## Public-repo safety

- The actual hostname lives in `.env.local`, gitignored.
- `.gitignore` includes `!.env.local.example` so the **template** stays
  tracked while `.env.local` itself never can be.
- Every `remote-*` target gates on `REMOTE_BUILD_HOST` being set, so a
  fresh `git clone` cannot accidentally reach anyone's box.
- We use plain SSH, not self-hosted GitHub Actions runners. Self-hosted
  runners on a public repo are a known supply-chain risk (fork PRs can
  execute arbitrary code on your runner); this setup sidesteps that
  entirely because only you push deltas, not arbitrary contributors.

## When to use remote vs local

| Task | Use |
|------|-----|
| Edit + incremental Linux build | local `make build` |
| Cold Linux build (clean tree) | `make remote-build` |
| Qt rebuild after vcpkg feature change | `make remote-build` |
| Full test suite | either; `remote-test` is faster on a beefy VPS |
| macOS `.app` build for visual testing | `make build-macos` (host-only — needs Apple toolchain) |
| Windows installer | `release-windows.yml` workflow (GitHub Actions) |

The remote path can't produce a macOS `.app` (no Apple toolchain on
Linux). For that, keep using `make build-macos` natively.

## Troubleshooting

**`Permission denied (publickey)`** — your `~/.ssh/config` doesn't have
the host or your key isn't on the VPS. Test with `ssh $REMOTE_BUILD_HOST`
directly.

**`docker: command not found`** on the VPS — install Docker first
(`curl -fsSL https://get.docker.com | sh`); the make targets assume it
exists.

**Slow first build** — vcpkg's binary cache on the VPS is empty on
first run, so Qt builds from source (~20 min on a 6-core VPS with
the release-only triplet, longer on smaller boxes). Subsequent builds
hit the cache and finish in under a minute for our delta. One-time
tax per VPS.

**Out of disk on the VPS** — the build tree + vcpkg cache + Qt
buildtrees peak around 12–15 GB during a cold first build, thanks
to the release-only Linux triplet at `tools/dev/triplets/`. Plan
for at least 25 GB free to leave comfortable headroom. Without the
triplet (legacy default debug+release), peak was ~22 GB and full
builds hit ENOSPC on smaller boxes.
