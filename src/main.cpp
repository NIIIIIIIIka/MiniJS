#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef MINIJS_VERSION
#define MINIJS_VERSION "unknown"
#endif

namespace
{
constexpr int ExitUsageError = 64;
constexpr int ExitInputError = 74;

void printUsage(std::ostream& output)
{
    output << "Usage: minijs <file>\n"
           << "       minijs --help\n"
           << "       minijs --version\n";
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("cannot open file: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (file.bad())
    {
        throw std::runtime_error("cannot read file: " + path);
    }

    return buffer.str();
}
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "error: expected exactly one argument\n";
        printUsage(std::cerr);
        return ExitUsageError;
    }

    const std::string argument = argv[1];

    if (argument == "--help" || argument == "-h")
    {
        printUsage(std::cout);
        return 0;
    }

    if (argument == "--version")
    {
        std::cout << "MiniJSVM " << MINIJS_VERSION << '\n';
        return 0;
    }

    if (!argument.empty() && argument.front() == '-')
    {
        std::cerr << "error: unknown option: " << argument << '\n';
        printUsage(std::cerr);
        return ExitUsageError;
    }

    try
    {
        const std::string source = readFile(argument);

        // Lexer and parser execution will replace this temporary behavior.
        std::cout << source;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return ExitInputError;
    }
}
