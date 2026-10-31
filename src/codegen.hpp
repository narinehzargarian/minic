// Generates ARM64 assembly for macOS.
#pragma once

#include <string>

#include "ast.hpp"

namespace minic {

std::string generate(const Program& program);

}  // namespace minic
