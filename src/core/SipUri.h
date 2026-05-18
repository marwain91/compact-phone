#pragma once

#include "Account.h"

#include <string>

namespace compactphone::sip {

std::string normalizeSipTarget(const std::string &target,
                               const std::string &domain,
                               Transport transport);

} // namespace compactphone::sip
