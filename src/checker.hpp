// Semantic checks: names, scopes, call arity, and loop control. Also assigns stack slots.
#pragma once

#include "ast.hpp"

namespace minic {

constexpr int kMaxParams = 8;  // The ARM64 calling convention passes eight arguments in registers.

void check(Program& program);

}  // namespace minic
