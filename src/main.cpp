#include <iostream>
#include <string>

#include "parser/parser.h"
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
    var swapped
    var i = 0
    while i < n -1:
        swapped = 0
        var j = 0
        while j < n-i-1:
            if a[j] > a[j+1]:
                var t = a[j+1]
                a[j+1] = a[j]
                a[j] = t
                swapped = 1
            j+=1
        if not swapped:
            break
        i+=1
)";

        auto module = acu::parser::parse(source);
        std::cout << acu::nodes::to_string(module) << '\n';

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
