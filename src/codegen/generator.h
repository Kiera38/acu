#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <memory>
#include <optional>

#include "refanal/ir.h"

namespace acu::codegen {

std::unique_ptr<llvm::Module> generate(
    llvm::LLVMContext& context,
    const refanal::ir::Module& module,
    std::optional<llvm::DataLayout> layout = std::nullopt
);

void optimize(
    llvm::Module& module,
    llvm::OptimizationLevel level = llvm::OptimizationLevel::O2
);

void emit_object_file(
    llvm::Module& module,
    const std::string& filename
);

}
