# CompactPhone Dev Container

Linux dev environment with the full build toolchain pre-installed. Use this for all code authoring, building, unit tests, and integration tests. The macOS host only comes back into play for the v0.1 acceptance test (Task 12), which needs native CoreAudio to verify the audio path.

## First-time setup

```bash
docker compose -f tools/dev/docker-compose.yml up -d --build
```

First build of the image: ~5 minutes (apt packages, CMake binary, vcpkg clone).

## Configure (first run triggers Qt/PJSIP build via vcpkg)

```bash
docker compose -f tools/dev/docker-compose.yml exec dev \
    cmake --preset linux
```

The first configure pulls Qt 6, PJSIP, OpenSSL, spdlog, and GoogleTest sources and compiles them through vcpkg. Expect **30–45 minutes**. Subsequent configures with the same vcpkg baseline restore from the `vcpkg-archives` named volume in under a minute.

## Build

```bash
docker compose -f tools/dev/docker-compose.yml exec dev \
    cmake --build --preset linux
```

## Run tests

Unit tests:

```bash
docker compose -f tools/dev/docker-compose.yml exec dev \
    ctest --test-dir build/linux --output-on-failure -L unit
```

Integration tests (once Task 6 adds the Asterisk fixture):

```bash
docker compose -f tools/dev/docker-compose.yml exec dev \
    ctest --test-dir build/linux --output-on-failure -L integration
```

## Interactive shell

```bash
docker compose -f tools/dev/docker-compose.yml exec dev bash
```

## Tear down

```bash
docker compose -f tools/dev/docker-compose.yml down
```

The `vcpkg-archives` volume persists across teardowns so the Qt/PJSIP build is reused.

To wipe the vcpkg binary cache and start fresh:

```bash
docker compose -f tools/dev/docker-compose.yml down -v
```

## Why not build natively on macOS?

You can — at acceptance time, you must, because audio device passthrough into Docker on macOS is not viable. But for day-to-day iteration the Linux container keeps the host clean (no Homebrew packages, no vcpkg checkout polluting `$HOME`, no Qt SDK install). CI builds for Windows + macOS happen in GitHub Actions, so the only macOS-host build you ever need to do locally is the final v0.1 acceptance run.
