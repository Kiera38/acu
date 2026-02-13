#include <iostream>
#include <string>

#include "parser/lexer.h"
#include "source.h"

int main() {
    try {
        // Test the lexer with a simple program
        acu::Source source;
        source.module_name = "test";
        source.path = "test.acu";
        source.content = "func main() { var x = 42 }";

        acu::parser::Lexer lexer(source);

        std::cout << "Lexing the test program...\n";
        acu::parser::Token token = lexer.next_token();
        int count = 1;
        while (token.type != acu::parser::TokenType::EndOfFile) {
            std::cout << "Token " << count++ << ": "
                      << acu::parser::token_to_string(token) << '\n';

            // Limit to prevent infinite loop in case of error
            if (count > 15) break;
            token = lexer.next_token();
        }

        std::cout << "Lexer test completed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << '\n';
        return 1;
    }

    return 0;
}
