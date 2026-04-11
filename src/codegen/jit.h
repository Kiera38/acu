#pragma once

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <string>

namespace acu::codegen {

class JIT {
public:
    static std::unique_ptr<JIT> create();

    llvm::Error add_module(llvm::orc::ThreadSafeModule tsm);

    llvm::Error add_module(std::unique_ptr<llvm::Module> module, std::unique_ptr<llvm::LLVMContext> context) {
        return add_module(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)));
    }

    template <typename T>
    llvm::Expected<T> lookup(const std::string& name) {
        auto sym = lljit_->lookup(name);
        if (!sym) return sym.takeError();
        return sym->toPtr<T>();
    }

    llvm::Expected<int (*)()> get_main() {
        return lookup<int (*)()>("main");
    }

    [[nodiscard]] const llvm::DataLayout& get_data_layout() const {
        return lljit_->getDataLayout();
    }

private:
    explicit JIT(std::unique_ptr<llvm::orc::LLJIT> lljit);

    std::unique_ptr<llvm::orc::LLJIT> lljit_;
};

}
