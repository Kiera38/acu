#pragma once

#include "errors.h"
#include "parser/nodes.h"

namespace acu::parser {
nodes::Module parse(Source& source, ErrorHandler& err_handler);
}
