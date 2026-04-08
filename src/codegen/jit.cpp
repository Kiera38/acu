#include "codegen/jit.h"

#include <llvm/Support/TargetSelect.h>

namespace acu::codegen {

JIT::JIT(std::unique_ptr<llvm::orc::LLJIT> lljit) : lljit_(std::move(lljit)) {}

std::unique_ptr<JIT> JIT::create() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    auto jit_expected = llvm::orc::LLJITBuilder().create();
    if (!jit_expected) return nullptr;

    return std::unique_ptr<JIT>(new JIT(std::move(*jit_expected)));
}

llvm::Error JIT::add_module(std::unique_ptr<llvm::Module> module) {
    // Note: In a production compiler, the Generator should probably 
    // work directly with ThreadSafeContext to avoid moving modules across contexts.
    // For now, we assume the module's context is compatible or managed.
    // LLVM 20 ThreadSafeModule takes ownership of the module and a context.
    
    auto tsctx = std::make_unique<llvm::LLVMContext>();
    // This is a hack because our Generator used a local context in main.cpp.
    // We should really pass ThreadSafeContext to Generator.
    return lljit_->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(tsctx)));
}

llvm::Expected<int (*)()> JIT::get_main() {
    auto sym = lljit_->lookup("main");
    if (!sym) return sym.takeError();
    return sym->toPtr<int (*)()>();
}

}
