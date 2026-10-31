// Constant folding and removal of branches that can never run.
#pragma once

#include "ast.hpp"

namespace minic {

void optimize(Program& program);

}  // namespace minic
