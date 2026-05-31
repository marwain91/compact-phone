#pragma once

namespace compactphone::notice {

// Auto-dismiss durations (ms) for transient notice / snackbar messages,
// grouped by intent. Longer-lived messages are more important / more likely
// to need reading. Values are the originals previously inlined at the call
// sites — only the names are new.
inline constexpr int kBrief     = 2500; // transient status ("Checking for updates…")
inline constexpr int kShort     = 3000; // minor info ("No update download available")
inline constexpr int kDefault   = 4000; // success / confirmation (the postNotice default)
inline constexpr int kError     = 5000; // a failure the user should notice
inline constexpr int kWarning   = 6000; // network / registration warnings
inline constexpr int kImportant = 8000; // an offer worth pausing on (update available)

} // namespace compactphone::notice
