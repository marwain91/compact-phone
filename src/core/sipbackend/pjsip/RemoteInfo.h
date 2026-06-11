#pragma once

#include <string>

namespace compactphone::sipbackend {

struct RemoteInfo {
    std::string uri;
    std::string displayName;
};

// Parses a SIP Address-of-Record string into its URI and display-name parts.
// Handles all three formats from RFC 3261 §20.10:
//   "Display Name" <sip:user@host>   — quoted display name + angle-bracket URI
//   Display Name <sip:user@host>     — unquoted display name + angle-bracket URI
//   <sip:user@host>                  — angle-bracket URI, no display name
//   sip:user@host                    — bare URI, no display name
//
// For angle-bracket forms: uri is the first '<'/'>' pair content; displayName
// is the text before '<' with trailing whitespace trimmed and enclosing
// double-quotes stripped. For bare-URI form: uri is the whole string and
// displayName is empty. Empty input produces empty-string fields.
RemoteInfo parseRemoteInfo(const std::string &raw);

} // namespace compactphone::sipbackend
