#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>

#include "refanal/ir.h"

namespace acu::codegen {

std::unique_ptr<llvm::Module> generate(
    llvm::LLVMContext& context, const refanal::ir::Module& module
);

}
