#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

#include "project.h"

enum class RunMode : std::uint8_t { Jit, Compile };

struct Config {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    acu::OptimizationLevel opt_level = acu::OptimizationLevel::O0;
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
            config.opt_level = acu::OptimizationLevel::O0;
        } else if (arg == "-O1") {
            config.opt_level = acu::OptimizationLevel::O1;
        } else if (arg == "-O2") {
            config.opt_level = acu::OptimizationLevel::O2;
        } else if (arg == "-O3") {
            config.opt_level = acu::OptimizationLevel::O3;
        } else if (arg == "-Os") {
            config.opt_level = acu::OptimizationLevel::Os;
        } else if (arg == "-Oz") {
            config.opt_level = acu::OptimizationLevel::Oz;
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
        if (std::filesystem::is_directory(config.input_path)) {
            config.output_path = config.input_path;
        } else {
            config.output_path =
                std::filesystem::absolute(config.input_path).parent_path();
        }
    }
    return config;
}
}

int main(int argc, char** argv) {
    auto config = parse_args({argv, static_cast<std::size_t>(argc)});
    if (!config) {
        return 1;
    }

    try {
        acu::Project project(config->input_path);
        project.parse(config->show_ast);
        project.semanal(config->show_semanal);
        project.refanal(config->show_refanal);
        if (config->mode == RunMode::Compile) {
            project.codegen(
                config->show_llvm,
                config->show_opt_llvm,
                config->opt_level,
                config->output_path
            );
        } else {
            project.run_jit(
                config->show_llvm, config->show_opt_llvm, config->opt_level
            );
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
