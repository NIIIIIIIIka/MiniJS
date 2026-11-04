#include <iostream>
#include <string>

#include "minijs/diagnostic.h"
#include "minijs/object.h"
#include "minijs/source_location.h"
#include "minijs/value.h"
#include "test_framework.h"

void runLexerTests();
void runParserTests();
void runInterpreterTests();
void runBytecodeTests();

void testGcStringValue() {
  minijs::ObjString gcString("Tom");
  const minijs::Value value(&gcString);

  EXPECT(value.isString());
  EXPECT(value.asString() == "Tom");
  EXPECT(value.toString() == "Tom");
  EXPECT(value.isTruthy());
  EXPECT(value.equals(minijs::Value(std::string("Tom"))));

  minijs::ObjString emptyString("");
  const minijs::Value empty(&emptyString);
  EXPECT(empty.isString());
  EXPECT(!empty.isTruthy());
  EXPECT(empty.equals(minijs::Value(std::string(""))));
}

int main() {
  const minijs::SourceLocation defaultLocation;
  EXPECT(defaultLocation.offset == 0);
  EXPECT(defaultLocation.line == 1);
  EXPECT(defaultLocation.column == 1);

  const minijs::SourceLocation location{12, 3, 5};
  const minijs::Diagnostic diagnostic(location, "unexpected token");
  EXPECT(diagnostic.location == location);
  EXPECT(diagnostic.message == std::string("unexpected token"));

  testGcStringValue();
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
