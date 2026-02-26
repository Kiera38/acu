#include <iostream>
#include <string>

#include "parser/parser.h"
#include "semanal/semanal.h"
#include "source.h"

int main() {
    try {
        // Test the lexer with a simple program
        acu::Source source;
        source.module_name = "test";
        source.path = "test.acu";
        source.content = R"(
func main() Int:
    var a = [10, 100, 30, 7]
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
                a[j] = t
                swapped = true
            j+=1
        if not swapped:
            break
        i+=1
)";

        auto module = acu::parser::parse(source);
        auto ir_module = acu::semanal::resolve(module);
        // AST output suppressed to avoid huge logs
        // std::cout << "AST:\n" << acu::nodes::to_string(module) << '\n';
        std::cout << "IR (functions=" << ir_module.funcs().size() << "):\n";
        try {
            std::cout << acu::ir::to_string(ir_module) << '\n';
        } catch (const std::exception& e) {
            std::cout << "IR print threw: " << e.what() << '\n';
        } catch(...) {
            std::cout << "IR print threw unknown exception\n";
        }

        std::cout << "Parser test completed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
