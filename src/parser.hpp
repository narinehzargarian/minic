// Recursive-descent parser: turns tokens into a syntax tree.
#pragma once

#include <string_view>

#include "ast.hpp"

namespace minic {

Program parse(std::string_view source);

}  // namespace minic
