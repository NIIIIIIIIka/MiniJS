#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "minijs/compiler.h"
#include "minijs/disassembler.h"
#include "minijs/interpreter.h"
#include "minijs/lexer.h"
#include "minijs/parser.h"
#include "minijs/token.h"
#include "minijs/vm.h"

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
         << "       minijs --interp <file>\n"
         << "       minijs --bytecode <file>\n"
         << "       minijs --ic-stats <file>\n"
         << "       minijs --benchmark <file>\n"
         << "       minijs --benchmark-compare <file>\n"
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

bool printBytecode(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  minijs::Compiler compiler;
  const minijs::Chunk chunk = compiler.compileProgram(program);
  std::cout << minijs::disassembleChunk(chunk);
  return true;
}

bool runVmProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  minijs::Compiler compiler;
  const minijs::Chunk chunk = compiler.compileProgram(program);
  minijs::VM vm;
  const minijs::Value result = vm.run(chunk);
  if (!result.isNull()) {
    std::cout << result.toString() << '\n';
  }
  return true;
}

void printInlineCacheStats(const minijs::PropertyInlineCacheStats& stats) {
  std::cout << "get property: hits=" << stats.hits << " misses=" << stats.misses
            << " updates=" << stats.updates << " bypasses=" << stats.bypasses << '\n'
            << "set property: hits=" << stats.setHits << " misses=" << stats.setMisses
            << " updates=" << stats.setUpdates << " bypasses=" << stats.setBypasses
            << '\n'
            << "method call: hits=" << stats.methodCallHits
            << " misses=" << stats.methodCallMisses << " updates=" << stats.methodCallUpdates
            << " bypasses=" << stats.methodCallBypasses << '\n'
            << "method dispatch: instance=" << stats.methodCallInstanceDispatches
            << " static=" << stats.methodCallStaticDispatches
            << " array_push=" << stats.methodCallArrayPushes
            << " array_pop=" << stats.methodCallArrayPops
            << " errors=" << stats.methodCallDispatchErrors << '\n';
}

struct BenchmarkResult {
  minijs::Value result;
  long long compileMicros = 0;
  long long runMicros = 0;
  minijs::PropertyInlineCacheStats stats;
};

BenchmarkResult runBenchmark(const minijs::Program& program, bool inlineCachesEnabled) {
  const auto compileStart = std::chrono::steady_clock::now();
  minijs::Compiler compiler;
  const minijs::Chunk chunk = compiler.compileProgram(program);
  const auto compileEnd = std::chrono::steady_clock::now();

  minijs::VM vm;
  vm.setInlineCachesEnabled(inlineCachesEnabled);
  const auto runStart = std::chrono::steady_clock::now();
  const minijs::Value result = vm.run(chunk);
  const auto runEnd = std::chrono::steady_clock::now();

  BenchmarkResult benchmark;
  benchmark.result = result;
  benchmark.compileMicros =
      std::chrono::duration_cast<std::chrono::microseconds>(compileEnd - compileStart).count();
  benchmark.runMicros =
      std::chrono::duration_cast<std::chrono::microseconds>(runEnd - runStart).count();
  benchmark.stats = vm.propertyInlineCacheStats();
  return benchmark;
}

bool runVmProgramWithIcStats(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  minijs::Compiler compiler;
  const minijs::Chunk chunk = compiler.compileProgram(program);
  minijs::VM vm;
  const minijs::Value result = vm.run(chunk);
  if (!result.isNull()) {
    std::cout << "result: " << result.toString() << '\n';
  }
  printInlineCacheStats(vm.propertyInlineCacheStats());
  return true;
}

bool benchmarkVmProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  const BenchmarkResult benchmark = runBenchmark(program, true);

  if (!benchmark.result.isNull()) {
    std::cout << "result: " << benchmark.result.toString() << '\n';
  }
  std::cout << "compile_us: " << benchmark.compileMicros << '\n'
            << "run_us: " << benchmark.runMicros << '\n';
  printInlineCacheStats(benchmark.stats);
  return true;
}

bool benchmarkCompareVmProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  for (const minijs::Diagnostic& diagnostic : parser.diagnostics()) {
    std::cerr << diagnostic.location.line << ':' << diagnostic.location.column
              << ": error: " << diagnostic.message << '\n';
  }

  if (!parser.diagnostics().empty()) {
    return false;
  }

  const BenchmarkResult withIc = runBenchmark(program, true);
  const BenchmarkResult withoutIc = runBenchmark(program, false);

  if (!withIc.result.equals(withoutIc.result)) {
    std::cerr << "error: benchmark result changed when inline caches were disabled\n";
    return false;
  }

  if (!withIc.result.isNull()) {
    std::cout << "result: " << withIc.result.toString() << '\n';
  }

  std::cout << "with_ic_compile_us: " << withIc.compileMicros << '\n'
            << "with_ic_run_us: " << withIc.runMicros << '\n'
            << "without_ic_compile_us: " << withoutIc.compileMicros << '\n'
            << "without_ic_run_us: " << withoutIc.runMicros << '\n';
  if (withIc.runMicros > 0 && withoutIc.runMicros > 0) {
    const double speedup =
        static_cast<double>(withoutIc.runMicros) / static_cast<double>(withIc.runMicros);
    std::cout << "speedup: " << speedup << "x\n";
  } else {
    std::cout << "speedup: n/a\n";
  }

  std::cout << "with_ic:\n";
  printInlineCacheStats(withIc.stats);
  std::cout << "without_ic:\n";
  printInlineCacheStats(withoutIc.stats);
  return true;
}

bool runInterpreterProgram(std::string_view source) {
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
  if (!result.isNull()) {
    std::cout << result.toString() << '\n';
  }
  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2 && argc != 3) {
    std::cerr << "error: expected one argument, or an option plus a file\n";
    printUsage(std::cerr);
    return ExitUsageError;
  }

  const std::string argument = argv[1];

  if (argc == 3 && argument != "--tokens" && argument != "--ast" && argument != "--run" &&
      argument != "--interp" && argument != "--bytecode" && argument != "--ic-stats" &&
      argument != "--benchmark" && argument != "--benchmark-compare") {
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
      return runVmProgram(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--interp") {
    if (argc != 3) {
      std::cerr << "error: --interp expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return runInterpreterProgram(readFile(argv[2])) ? 0 : ExitInputError;
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

  if (argument == "--bytecode") {
    if (argc != 3) {
      std::cerr << "error: --bytecode expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return printBytecode(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--ic-stats") {
    if (argc != 3) {
      std::cerr << "error: --ic-stats expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return runVmProgramWithIcStats(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--benchmark") {
    if (argc != 3) {
      std::cerr << "error: --benchmark expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return benchmarkVmProgram(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (argument == "--benchmark-compare") {
    if (argc != 3) {
      std::cerr << "error: --benchmark-compare expects a file\n";
      printUsage(std::cerr);
      return ExitUsageError;
    }

    try {
      return benchmarkCompareVmProgram(readFile(argv[2])) ? 0 : ExitInputError;
    } catch (const std::exception& error) {
      std::cerr << "error: " << error.what() << '\n';
      return ExitInputError;
    }
  }

  if (!argument.empty() && argument.front() == '-') {
    std::cerr << "error: unknown option: " << argument << '\n';
    printUsage(std::cerr);
    return ExitUsageError;
  }

  try {
    return runVmProgram(readFile(argument)) ? 0 : ExitInputError;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return ExitInputError;
  }
}
