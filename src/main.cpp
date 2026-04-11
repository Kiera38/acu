#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

#include "errors.h"
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir_str.h"
#include "refanal/optimizer.h"
#include "semanal/semanal.h"
#include "source.h"
#include "codegen/generator.h"
#include "codegen/jit.h"

int main(int argc, char** argv) {
    acu::Source source;
    source.module_name = "test";
    source.path = "test.acu";
    source.content = R"(
func main() Int:
    let a = 10
    let b = 20
    let c = 30
    let res = a * (b + c)
    let p = &a
    let value = p.*
    return res
)";

    acu::ErrorHandler err_handler;

    try {
        auto module = acu::parser::parse(source, err_handler);
        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
            return 1;
        }

        auto ir_module = acu::semanal::resolve(module, err_handler);
        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
            return 1;
        }

        auto analyzed =
            acu::semanal::type_analyze(ir_module, source, err_handler);
        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
            return 1;
        }
        auto refanal_module = acu::refanal::generate(analyzed);
        acu::refanal::optimize(refanal_module, analyzed, err_handler);

        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
            // return 1; // Don't return, let's see the IR if possible
        }

        std::cout << "\n=== REFANAL IR ===\n";
        std::cout << acu::refanal::to_string(refanal_module, analyzed) << "\n";

        llvm::LLVMContext context;
        auto llvm_module = acu::codegen::generate(context, refanal_module);

        std::cout << "\n=== LLVM IR ===\n";
        llvm_module->print(llvm::outs(), nullptr);

        std::cout << "\n=== OPTIMIZING LLVM IR ===\n";
        acu::codegen::optimize(*llvm_module, llvm::OptimizationLevel::O3);
        std::cout << "\n=== OPTIMIZED LLVM IR ===\n";
        llvm_module->print(llvm::outs(), nullptr);

        std::cout << "\n=== EMITTING OBJECT FILE ===\n";
        acu::codegen::emit_object_file(*llvm_module, "test.o");
        std::cout << "Object file emitted to test.o\n";

        std::cout << "\n=== JIT EXECUTION ===\n";
        auto jit = acu::codegen::JIT::create();
        if (jit) {
            if (auto err = jit->add_module(std::move(llvm_module))) {
                std::cerr << "Failed to add module to JIT\n";
            } else {
                auto main_func = jit->get_main();
                if (main_func) {
                    std::cout << "Running main()...\n";
                    int result = (*main_func)();
                    std::cout << "main() returned: " << result << "\n";
                } else {
                    std::cerr << "Failed to find main function in JIT\n";
                }
            }
        } else {
            std::cerr << "Failed to create JIT\n";
        }

    } catch (const std::exception& e) {
        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
        } else {
            std::cerr << "Exception caught: " << e.what() << '\n';
        }
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
