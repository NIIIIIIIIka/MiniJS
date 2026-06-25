#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "minijs/interpreter.h"
#include "minijs/lexer.h"
#include "minijs/parser.h"
#include "minijs/token.h"

#ifndef MINIJS_VERSION
#define MINIJS_VERSION "unknown"
#endif

namespace {
constexpr int ExitUsageError = 64;
constexpr int ExitInputError = 74;

void printUsage(std::ostream& output) {
  output << "Usage: minijs <file>\n"
         << "       minijs --tokens <file>\n"
         << "       minijs --ast <file>\n"
         << "       minijs --run <file>\n"
         << "       minijs --help\n"
         << "       minijs --version\n";
}

std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("cannot open file: " + path);
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  if (file.bad()) {
    throw std::runtime_error("cannot read file: " + path);
  }

  return buffer.str();
}

void printTokens(std::string_view source) {
  minijs::Lexer lexer(source);

  while (true) {
    const minijs::Token token = lexer.nextToken();
    std::cout << token.location.line << ':' << token.location.column << ' '
              << minijs::tokenTypeName(token.type) << " \"" << token.lexeme << "\"\n";

    if (token.type == minijs::TokenType::Eof) {
      break;
    }
  }

  for (const minijs::Diagnostic& diagnostic : lexer.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }
}

bool printAst(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  std::cout << minijs::formatProgram(program) << '\n';
  return true;
}

bool runProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  minijs::Interpreter interpreter;
  const minijs::Value result = interpreter.interpret(program);
  std::cout << result.toString() << '\n';
  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2 && argc != 3) {
    std::cerr << "error: expected one argument, or --tokens plus a file\n";
    printUsage(std::cerr);
    return ExitUsageError;
  }

  const std::string argument = argv[1];

  if (argc == 3 && argument != "--tokens" && argument != "--ast" && argument != "--run") {
    std::cerr << "error: unknown two-argument command: " << argument << '\n';
    printUsage(std::cerr);
    return ExitUsageError;
  }

  if (argument == "--tokens") {
    if (argc != 3) {
      std::cerr << "error: --tokens expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      printTokens(readFile(argv[2]));
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--ast") {
    if (argc != 3) {
      std::cerr << "error: --ast expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return printAst(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--run") {
    if (argc != 3) {
      std::cerr << "error: --run expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return runProgram(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--help" || argument == "-h") {
    printUsage(std::cout);
    return 0;
  }

  if (argument == "--version") {
    std::cout << "MiniJSVM " << MINIJS_VERSION << '\n';
    return 0;
  }

  if (!argument.empty() && argument.front() == '-') {
    std::cerr << "error: unknown option: " << argument << '\n';
    printUsage(std::cerr);
    return ExitUsageError;
  }

  try {
    const std::string source = readFile(argument);

    // Lexer and parser execution will replace this temporary behavior.
    std::cout << source;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return ExitInputError;
  }
}
