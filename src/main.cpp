#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "project.h"

namespace {
enum class RunMode : std::uint8_t { Jit, Compile };

struct Config {
    std::vector<std::filesystem::path> search_paths;
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    acu::OptimizationLevel opt_level = acu::OptimizationLevel::O0;
    bool show_ast = false;
    bool show_refanal = false;
    bool show_llvm = false;
    bool show_opt_llvm = false;
    RunMode mode = RunMode::Jit;
};

constexpr std::string_view help = R"(
Options:
  -o <path>         Set output object file path
  -I <path>         Add search path
  -O0, -O1, -O2,
  -O3, -Os, -Oz     Set optimization level (default: -O0)
  --emit-ast        Show Ast
  --emit-refanal    Show Refanal IR
  --emit-llvm       Show LLVM IR (pre-optimization)
  --emit-opt-llvm   Show optimized LLVM IR
  --show-ir         Show all intermediate representations
  --compile, -c     Compile to object file (no execution)
  --run, --jit      Execute with JIT (default)
  -h, --help        Show this help message
)";

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <input_files...> [options]";
    std::cout << help;
}

std::optional<Config> parse_args(std::span<char*> argv) {
    bool has_err = false;
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
                has_err = true;
            }
        } else if (arg == "-I") {
            if (i + 1 < args.size()) {
                config.search_paths.push_back(args[++i]);
            } else {
                std::cerr << "Error: -I requires an search path\n";
                has_err = true;
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
        } else if (arg == "--emit-refanal") {
            config.show_refanal = true;
        } else if (arg == "--emit-llvm") {
            config.show_llvm = true;
        } else if (arg == "--emit-opt-llvm") {
            config.show_opt_llvm = true;
        } else if (arg == "--show-ir") {
            config.show_ast = config.show_refanal = config.show_llvm =
                config.show_opt_llvm = true;
        } else if (arg == "--compile" || arg == "-c") {
            config.mode = RunMode::Compile;
        } else if (arg == "--run" || arg == "--jit") {
            config.mode = RunMode::Jit;
        } else if (arg.starts_with("-")) {
            std::cerr << "Unknown option: " << arg << "\n";
            has_err = true;
        } else {
            if (!config.input_path.empty()) {
                has_err = true;
            } else {
                config.input_path = arg;
            }
        }
    }
    if (config.search_paths.empty()) {
        config.search_paths.push_back(std::filesystem::current_path());
    }
    for (const auto& path : config.search_paths) {
        if (!std::filesystem::exists(path)) {
            std::cerr << "Error: search path '" << path << "' does not exist\n";
            has_err = true;
        }
        if (!std::filesystem::is_directory(path)) {
            std::cerr << "Error: search path '" << path
                      << "' is not a directory\n";
            has_err = true;
        }
    }

    if (!std::filesystem::exists(config.input_path)) {
        std::cerr << "Error: file not found: " << config.input_path << "\n";
        has_err = true;
    }

    if (config.mode == RunMode::Compile && config.output_path.empty()) {
        config.output_path =
            std::filesystem::absolute(config.input_path).parent_path();
    }
    if (has_err) return std::nullopt;
    return config;
}
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    auto config = parse_args({argv, static_cast<std::size_t>(argc)});
    if (!config) {
        return 1;
    }

    try {
        acu::Project project(config->search_paths);
        project.analyze(config->input_path);

        // project.refanal(config->show_refanal);
        // if (config->mode == RunMode::Compile) {
        //     project.codegen(
        //         config->show_llvm,
        //         config->show_opt_llvm,
        //         config->opt_level,
        //         config->output_path
        //     );
        // } else {
        //     project.run_jit(
        //         config->show_llvm, config->show_opt_llvm, config->opt_level
        //     );
        // }
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
