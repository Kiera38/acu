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
        acu::Token token;
        int count = 0;
        do {
            token = lexer.next_token();
            std::cout << "Token " << count++ << ": " << static_cast<int>(token.type);
            
            // Print the token type name for easier debugging
            switch(token.type) {
                case acu::TokenType::Func: std::cout << " (FUNC)"; break;
                case acu::TokenType::Var: std::cout << " (VAR)"; break;
                case acu::TokenType::Identifier: std::cout << " (IDENTIFIER)"; break;
                case acu::TokenType::Integer: std::cout << " (INTEGER)"; break;
                case acu::TokenType::Equal: std::cout << " (=)"; break;
                case acu::TokenType::EndOfFile: std::cout << " (EOF)"; break;
                case acu::TokenType::LParen: std::cout << " (LPAREN)"; break;
                case acu::TokenType::RParen: std::cout << " (RPAREN)"; break;
                case acu::TokenType::LBrace: std::cout << " (LBRACE)"; break;
                case acu::TokenType::RBrace: std::cout << " (RBRACE)"; break;
                default: std::cout << " (OTHER)"; break;
            }
            std::cout << std::endl;
            
            // Limit to prevent infinite loop in case of error
            if (count > 15) break;
        } while (token.type != acu::TokenType::EndOfFile);

        std::cout << "Lexer test completed successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught!" << std::endl;
        return 1;
    }

    return 0;
}