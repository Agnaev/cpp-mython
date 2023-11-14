#include "lexer.h"
#include "test_runner_p.h"

#include <iostream>

namespace parse {
  void RunOpenLexerTests(TestRunner& tr);
}

int main() {
  try {
    TestRunner tr;
    parse::RunOpenLexerTests(tr);
    
    std::istringstream test("a = 10\nif a > 1: ##hello world\n  print 'more'\nelse:\n  print 'less'\n");

    parse::Lexer lexer(test);
    parse::Token t;
    while ((t = lexer.CurrentToken()) != parse::token_type::Eof{}) {
      std::cout << t << std::endl;
      lexer.NextToken();
    }
  }
  catch (const std::exception& e) {
    std::cerr << e.what();
    return 1;
  }
}