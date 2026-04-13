#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <utility>
#include <vector>

#include "codegen/generator.h"
#include "codegen/jit.h"
#include "errors.h"
#include "parser/nodes.h"
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir.h"
#include "refanal/ir_str.h"
#include "refanal/optimizer.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "source.h"

enum class RunMode : std::uint8_t { Jit, Compile };

struct Config {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    llvm::OptimizationLevel opt_level = llvm::OptimizationLevel::O0;
    bool show_ast = false;
    bool show_semanal = false;
    bool show_refanal = false;
    bool show_llvm = false;
    bool show_opt_llvm = false;
    RunMode mode = RunMode::Jit;
};

namespace {

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <input_files...> [options]\n"
              << "Options:\n"
              << "  -o <path>         Set output object file path\n"
              << "  -O0, -O1, -O2,\n"
              << "  -O3, -Os, -Oz     Set optimization level (default: -O0)\n"
              << "  --emit-ast        Show Ast\n"
              << "  --emit-semanal    Show Semanal IR\n"
              << "  --emit-refanal    Show Refanal IR\n"
              << "  --emit-llvm       Show LLVM IR (pre-optimization)\n"
              << "  --emit-opt-llvm   Show optimized LLVM IR\n"
              << "  --show-ir         Show all intermediate representations\n"
              << "  --compile, -c     Compile to object file (no execution)\n"
              << "  --run, --jit      Execute with JIT (default)\n"
              << "  -h, --help        Show this help message\n";
}

std::optional<Config> parse_args(std::span<char*> argv) {
    Config config;
    std::vector<std::string_view> args;
    args.reserve(argv.size() - 1);
    for (auto arg : argv.subspan(1)) {
        args.emplace_back(arg);
    }

    if (args.empty()) {
        print_help(argv[0]);
        return std::nullopt;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return std::nullopt;
        } else if (arg == "-o") {
            if (i + 1 < args.size()) {
                config.output_path = args[++i];
                config.mode = RunMode::Compile;
            } else {
                std::cerr << "Error: -o requires an output path\n";
                return std::nullopt;
            }
        } else if (arg == "-O0") {
            config.opt_level = llvm::OptimizationLevel::O0;
        } else if (arg == "-O1") {
            config.opt_level = llvm::OptimizationLevel::O1;
        } else if (arg == "-O2") {
            config.opt_level = llvm::OptimizationLevel::O2;
        } else if (arg == "-O3") {
            config.opt_level = llvm::OptimizationLevel::O3;
        } else if (arg == "-Os") {
            config.opt_level = llvm::OptimizationLevel::Os;
        } else if (arg == "-Oz") {
            config.opt_level = llvm::OptimizationLevel::Oz;
        } else if (arg == "--emit-ast") {
            config.show_ast = true;
        } else if (arg == "--emit-semanal") {
            config.show_semanal = true;
        } else if (arg == "--emit-refanal") {
            config.show_refanal = true;
        } else if (arg == "--emit-llvm") {
            config.show_llvm = true;
        } else if (arg == "--emit-opt-llvm") {
            config.show_opt_llvm = true;
        } else if (arg == "--show-ir") {
            config.show_ast = config.show_semanal = config.show_refanal =
                config.show_llvm = config.show_opt_llvm = true;
        } else if (arg == "--compile" || arg == "-c") {
            config.mode = RunMode::Compile;
        } else if (arg == "--run" || arg == "--jit") {
            config.mode = RunMode::Jit;
        } else if (arg.starts_with("-")) {
            std::cerr << "Unknown option: " << arg << "\n";
            return std::nullopt;
        } else {
            if (!config.input_path.empty()) {
                std::cerr << "Error: multiple input files specified\n";
            } else {
                config.input_path = arg;
            }
        }
    }

    if (!std::filesystem::exists(config.input_path)) {
        std::cerr << "Error: file not found: " << config.input_path << "\n";
        return std::nullopt;
    }

    if (config.mode == RunMode::Compile && config.output_path.empty()) {
        config.output_path = config.input_path.stem().string() + ".o";
    }
    return config;
}

std::optional<acu::Source> read_file(
    const std::filesystem::path& path, const std::string& package_name
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << path << "\n";
        return std::nullopt;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string module_name = path.stem().string();
    if (!package_name.empty()) {
        module_name = package_name + '.' + module_name;
    }

    return acu::Source {
        .module_name = module_name, .path = path.string(), .content = ss.str()
    };
}

std::optional<acu::nodes::Module> parse_module(
    acu::Source& source, acu::ErrorHandler& err_handler, bool show_ast
) {
    auto module = acu::parser::parse(source, err_handler);
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        return std::nullopt;
    }
    if (show_ast) {
        std::cout << "\nAST for " << source.module_name << "\n";
        std::cout << acu::nodes::to_string(module);
    }
    return module;
}

std::vector<std::string_view> split_package_name(
    const std::string& package_name
) {
    if(package_name.empty()) {
        return {};
    }
    std::vector<std::string_view> name;
    for (auto name_part : package_name | std::views::split('.')) {
        name.emplace_back(name_part);
    }
    return name;
}

std::optional<acu::semanal::AnalyzedPackage> semanal(
    const std::string& package_name,
    const std::vector<acu::nodes::Module>& modules,
    const std::vector<acu::Source>& sources,
    acu::ErrorHandler& err_handler,
    bool show_semanal
) {
    std::vector<acu::semanal::ModuleInfo> mod_infos;
    mod_infos.reserve(modules.size());
    for (size_t i = 0; i < modules.size(); ++i) {
        mod_infos.push_back({.source = &sources[i], .module = &modules[i]});
    }

    auto ir_package = acu::semanal::resolve(
        split_package_name(package_name), mod_infos, err_handler
    );
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        return std::nullopt;
    }
    if (show_semanal) {
        std::cout << "\nSEMANAL IR\n";
        std::cout << acu::ir::to_string(ir_package);
    }

    auto analyzed = acu::semanal::type_analyze(ir_package, err_handler);
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        return std::nullopt;
    }
    return analyzed;
}

std::optional<acu::refanal::ir::Module> refanal(
    acu::semanal::AnalyzedPackage& analyzed,
    const std::vector<acu::Source>& sources,
    acu::ErrorHandler& err_handler,
    bool show_refanal
) {
    auto refanal_module = acu::refanal::generate(analyzed);
    acu::refanal::optimize(refanal_module, analyzed, err_handler);

    if (err_handler.has_errors()) {
        err_handler.emit_all();
        return std::nullopt;
    }

    if (show_refanal) {
        std::cout << "\nREFANAL IR\n";
        std::cout << acu::refanal::to_string(refanal_module, analyzed) << "\n";
    }
    return refanal_module;
}

std::unique_ptr<llvm::Module> generate_llvm(
    llvm::LLVMContext& context,
    const acu::refanal::ir::Module& module,
    const Config& config,
    std::optional<llvm::DataLayout> layout
) {
    auto llvm_module =
        acu::codegen::generate(context, module, std::move(layout));
    if (config.show_llvm) {
        std::cout << "\nLLVM IR\n";
        llvm_module->print(llvm::outs(), nullptr);
    }

    acu::codegen::optimize(*llvm_module, config.opt_level);
    if (config.show_opt_llvm) {
        std::cout << "\nOPTIMIZED LLVM IR\n";
        llvm_module->print(llvm::outs(), nullptr);
    }
    return llvm_module;
}

void run_jit(const acu::refanal::ir::Module& module, const Config& config) {
    std::cout << "\nJIT EXECUTION\n";
    auto jit = acu::codegen::JIT::create();
    if (!jit) {
        std::cerr << "Failed to create JIT\n";
        return;
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto llvm_module =
        generate_llvm(*context, module, config, jit->get_data_layout());

    if (auto err =
            jit->add_module(std::move(llvm_module), std::move(context))) {
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
}

void codegen(const acu::refanal::ir::Module& module, const Config& config) {
    if (config.mode == RunMode::Compile) {
        auto context = std::make_unique<llvm::LLVMContext>();
        auto llvm_module =
            generate_llvm(*context, module, config, std::nullopt);
        acu::codegen::emit_object_file(
            *llvm_module, config.output_path.string()
        );
        if (config.show_refanal || config.show_llvm || config.show_opt_llvm) {
            std::cout << "\nObject file emitted to " << config.output_path
                      << "\n";
        }
    } else {
        run_jit(module, config);
    }
}
}

int main(int argc, char** argv) {
    auto config = parse_args({argv, static_cast<std::size_t>(argc)});
    if (!config) {
        return 1;
    }

    std::vector<acu::Source> sources;
    auto [package_path, package_name] =
        [&] -> std::pair<std::filesystem::path, std::string> {
        if (std::filesystem::is_directory(config->input_path)) {
            return {config->input_path, config->input_path.stem().string()};
        } else {
            return {config->input_path.parent_path(), ""};
        }
    }();
    for (const auto& path : package_path) {
        if (path.extension().string() != ".acu") {
            continue;
        }
        auto source = read_file(path, package_name);
        if (!source) {
            return 1;
        }
        sources.push_back(std::move(*source));
    }

    acu::ErrorHandler err_handler;

    try {
        std::vector<acu::nodes::Module> modules;
        for (auto& source : sources) {
            auto module = parse_module(source, err_handler, config->show_ast);
            if (!module) {
                return 1;
            }
            modules.push_back(std::move(*module));
        }

        auto analyzed = semanal(
            package_name, modules, sources, err_handler, config->show_semanal
        );
        if (!analyzed) {
            return 1;
        }

        auto refanal_module =
            refanal(*analyzed, sources, err_handler, config->show_refanal);
        if (!refanal_module) {
            return 1;
        }

        codegen(*refanal_module, *config);

    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
