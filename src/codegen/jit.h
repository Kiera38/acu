#pragma once

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/Module.h>

namespace acu::codegen {

class JIT {
public:
    static std::unique_ptr<JIT> create();

    llvm::Error add_module(std::unique_ptr<llvm::Module> module);
    llvm::Expected<int (*)()> get_main();

private:
    JIT(std::unique_ptr<llvm::orc::LLJIT> lljit);

    std::unique_ptr<llvm::orc::LLJIT> lljit_;
};

}
