#include <iostream>
#include <string>

#include "minijs/diagnostic.h"
#include "minijs/source_location.h"
#include "test_framework.h"

void runLexerTests();
void runParserTests();
void runInterpreterTests();
void runBytecodeTests();

int main() {
  const minijs::SourceLocation defaultLocation;
  EXPECT(defaultLocation.offset == 0);
  EXPECT(defaultLocation.line == 1);
  EXPECT(defaultLocation.column == 1);

  const minijs::SourceLocation location{12, 3, 5};
  const minijs::Diagnostic diagnostic(location, "unexpected token");
  EXPECT(diagnostic.location == location);
  EXPECT(diagnostic.message == std::string("unexpected token"));

  runLexerTests();
  runParserTests();
  runInterpreterTests();
  runBytecodeTests();

  if (minijs::test::failures != 0) {
    std::cerr << minijs::test::failures << " test(s) failed\n";
    return 1;
  }

  std::cout << "all tests passed\n";
  return 0;
}
