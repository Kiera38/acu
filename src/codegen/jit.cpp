#include "codegen/jit.h"

#include <llvm/Support/TargetSelect.h>

namespace acu::codegen {

JIT::JIT(std::unique_ptr<llvm::orc::LLJIT> lljit) : lljit_(std::move(lljit)) {}

std::unique_ptr<JIT> JIT::create() {
    static bool initialized = false;
    if (!initialized) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        initialized = true;
    }

    auto jit_expected = llvm::orc::LLJITBuilder().create();
    if (!jit_expected) {
        llvm::consumeError(jit_expected.takeError());
        return nullptr;
    }

    return std::unique_ptr<JIT>(new JIT(std::move(*jit_expected)));
}

llvm::Error JIT::add_module(llvm::orc::ThreadSafeModule tsm) {
    return lljit_->addIRModule(std::move(tsm));
}

}
