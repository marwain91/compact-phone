# Contributing to Compact Phone

Thanks for considering a contribution. This project is GPL-3.0-or-later
and welcomes patches, bug reports, and translation help.

## Reporting bugs

File issues at https://github.com/marwain91/compact-phone/issues with:

- Compact Phone version (Help → About).
- OS and version (`uname -a` on macOS/Linux, `winver` on Windows).
- SIP server type if applicable (Asterisk 20, FreeSWITCH 1.10, etc.).
- Steps to reproduce.
- Diagnostics export — open Settings → Advanced → **Export diagnostics**
  and attach the resulting file. Account passwords are redacted.

For crashes, please also attach the latest entry from the
`~/Library/Application Support/Compact Phone/.sentry-native/` (macOS)
or equivalent path. If crash reporting is opt-in enabled, the report
likely already reached Sentry — mention the time of the crash.

## Building from source

See `README.md` for the per-platform build commands. The TL;DR for Linux:

```sh
make up       # start dev container + Asterisk fixture
make build    # configure + build inside the container
make test     # run unit + integration tests
```

For macOS native builds:

```sh
make pjsip-macos       # one-time
make build-macos
make run-macos
```

## Pull requests

1. Branch off `main`.
2. Keep PRs small and focused. Bundle one logical change per PR.
3. Add tests. The bar: unit tests for any new module under `src/core/`,
   integration tests for any feature touching the SIP layer.
4. Run `make test` before pushing. CI runs the same on every PR.
5. Use Conventional Commits-ish messages (`feat:`, `fix:`, `test:`,
   `refactor:`, `docs:`). Look at `git log` for examples.
6. By submitting a PR, you affirm your contribution is licensed under
   GPL-3.0-or-later and that you have the right to license it.

## Coding style

- C++17, no exceptions in normal control flow (PJSIP wrappers are
  exception-safe by virtue of `try {} catch (const pj::Error &)`).
- Qt6, prefer `QStringLiteral` for constant strings.
- Snake_case for SQL columns, camelCase for C++ / QML identifiers.
- 4-space indent, no tabs.
- Files end with a newline.
- Don't add comments that just paraphrase the code; the codebase prefers
  brief "why this is non-obvious" notes only.

## Translations

The source language is English (US). Translation files live under
`src/i18n/`. To regenerate strings from QML/C++:

```sh
lupdate src/ -ts src/i18n/compactphone_<locale>.ts
```

A Weblate/Crowdin mirror is on the roadmap; until then, PRs against the
`.ts` files are welcome.

## Security

See `SECURITY.md` for the responsible-disclosure process.

## Code of Conduct

This project follows the
[Contributor Covenant v2.1](https://www.contributor-covenant.org/version/2/1/code_of_conduct/).
Be excellent to each other.
