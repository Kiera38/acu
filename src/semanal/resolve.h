#pragma once
#include "context.h"
#include "project.h"

namespace acu::semanal {
ModuleContext resolve(const nodes::Module& module, Project& project);
void resolve_inner(
    const nodes::Module& module, ModuleContext& context, Project& project
);
}