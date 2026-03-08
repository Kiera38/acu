#include <iostream>
#include <string>

#include "errors.h"
#include "parser/parser.h"
#include "semanal/semanal.h"
#include "source.h"

int main() {
    acu::Source source;
    source.module_name = "test";
    source.path = "test.acu";
    source.content = R"(
func main() Int:
    var a = [10, 100, 30, 7]
    var s = "hello"
    var c = 'A'
    bubble_sort(a as Ptr[Int], 4)
    return 0


func bubble_sort(a: Ptr[Int], n: Int):
    var i = 0
    while i < n -1:
        var swapped = false
        var j = 0
        while j < n-i-1:
            if a[j] > a[j+1]:
                var t = a[j+1]
                a[j+1] = a[j]
                a[jj] = t
                swapped = true
            j+=1
        if not swapped:
            break
        i+=1
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

        std::cout << "=== IR ===\n" << acu::ir::to_string(ir_module) << '\n';

        auto analyzed =
            acu::semanal::type_analyze(ir_module, source, err_handler);
        if (err_handler.has_errors()) {
            err_handler.emit_all(source);
            return 1;
        }

        std::cout << "=== type analysis ===\n";
        for (const auto& func : analyzed.analyzed_funcs) {
            std::cout << "Func: " << analyzed.ir_module.func(func.ref).name()
                      << "\n";
            for (size_t i = 0; i < func.inst_types.size(); ++i) {
                std::cout << "  inst " << i << ": type "
                          << func.inst_types[i].index << "\n";
            }
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
