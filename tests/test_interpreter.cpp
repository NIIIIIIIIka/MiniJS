#include <iostream>
#include <sstream>
#include <string_view>

#include "minijs/interpreter.h"
#include "minijs/parser.h"
#include "minijs/runtime_error.h"
#include "test_framework.h"

namespace {

minijs::Value run(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Interpreter interpreter;
  return interpreter.interpret(program);
}

void testExpressionEvaluation() { EXPECT(run("1 + 2 * 3;").asNumber() == 7); }

void testLetAndVariable() { EXPECT(run("let x = 10; x + 5;").asNumber() == 15); }

void testMultipleLets() { EXPECT(run("let x = 10; let y = x + 20; x + y;").asNumber() == 40); }

void testPercent() { EXPECT(run("10 % 3;").asNumber() == 1); }

void testGrouping() { EXPECT(run("(1 + 2) * 3;").asNumber() == 9); }

void testComparison() {
  EXPECT(run("1 > 2;").toString() == "false");
  EXPECT(run("2 >= 2;").toString() == "true");
}

void testEquality() {
  EXPECT(run("1 == 1;").toString() == "true");
  EXPECT(run("1 == 2;").toString() == "false");
  EXPECT(run("1 != 2;").toString() == "true");
  EXPECT(run("true == true;").toString() == "true");
  EXPECT(run("true != false;").toString() == "true");
  EXPECT(run("\"a\" == \"a\";").toString() == "true");
  EXPECT(run("\"a\" != \"b\";").toString() == "true");
  EXPECT(run("null == null;").toString() == "true");
  EXPECT(run("undefined == undefined;").toString() == "true");
  EXPECT(run("null == undefined;").toString() == "false");
}

void testReferenceEquality() {
  EXPECT(run("let a = []; let b = a; a == b;").toString() == "true");
  EXPECT(run("[] == [];").toString() == "false");
  EXPECT(run("let a = {}; let b = a; a == b;").toString() == "true");
  EXPECT(run("({}) == ({});").toString() == "false");
  EXPECT(run("function f() { return 1; } let g = f; f == g;").toString() == "true");
}

void testUnaryOperators() {
  EXPECT(run("!true;").toString() == "false");
  EXPECT(run("!false;").toString() == "true");
  EXPECT(run("!0;").toString() == "true");
  EXPECT(run("!1;").toString() == "false");
  EXPECT(run("!null;").toString() == "true");
  EXPECT(run("!undefined;").toString() == "true");
  EXPECT(run("-10;").asNumber() == -10);
}

void testLogicalOperators() {
  EXPECT(run("false && 1;").toString() == "false");
  EXPECT(run("true && 1;").asNumber() == 1);
  EXPECT(run("true || 1;").toString() == "true");
  EXPECT(run("false || 1;").asNumber() == 1);
  EXPECT(run("0 || 123;").asNumber() == 123);
  EXPECT(run("1 && 123;").asNumber() == 123);
}

void testLogicalShortCircuit() {
  EXPECT(run("false && unknown;").toString() == "false");
  EXPECT(run("true || unknown;").toString() == "true");
}

void testBooleanAndNullLiterals() {
  EXPECT(run("true;").toString() == "true");
  EXPECT(run("false;").toString() == "false");
  EXPECT(run("null;").isNull());
  EXPECT(run("undefined;").isUndefined());
  EXPECT(run("undefined;").toString() == "undefined");
}

void testUndefinedIsFalsy() { EXPECT(run("if (undefined) 1; else 2;").asNumber() == 2); }

void testStringLiteral() {
  EXPECT(run("\"Tom\";").toString() == "Tom");
  EXPECT(run("\"\";").toString() == "");
}

void testStringConcatenation() {
  EXPECT(run("\"hello \" + \"world\";").toString() == "hello world");
  EXPECT(run("\"age: \" + 18;").toString() == "age: 18");
  EXPECT(run("18 + \" years\";").toString() == "18 years");
}

void testIfTrueBranch() { EXPECT(run("if (1) 10; else 20;").asNumber() == 10); }

void testIfFalseBranch() { EXPECT(run("if (0) 10; else 20;").asNumber() == 20); }

void testIfWithBooleanAndNullConditions() {
  EXPECT(run("if (true) 10; else 20;").asNumber() == 10);
  EXPECT(run("if (false) 10; else 20;").asNumber() == 20);
  EXPECT(run("if (null) 10; else 20;").asNumber() == 20);
}

void testIfWithVariableCondition() {
  EXPECT(run("let x = 10; if (x > 5) x + 1; else x - 1;").asNumber() == 11);
}

void testIfWithoutElse() { EXPECT(run("if (0) 10;").toString() == "null"); }

void testBlock() { EXPECT(run("{ let x = 10; x + 1; }").asNumber() == 11); }

void testIfWithBlockBranches() {
  EXPECT(run("let x = 10; if (x > 5) { x + 1; } else { x - 1; }").asNumber() == 11);
}

void testBlockScopeShadowsOuterVariable() {
  EXPECT(run("let x = 1; { let x = 2; x; } x;").asNumber() == 1);
}

void testBlockScopeDoesNotLeakVariable() {
  try {
    run("{ let y = 10; } y;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: y");
  }
}

void testBlockAssignsOuterVariable() { EXPECT(run("let x = 1; { x = 3; } x;").asNumber() == 3); }

void testAssignment() { EXPECT(run("let x = 1; x = x + 2; x;").asNumber() == 3); }

void testArrayLiteralAndIndex() {
  EXPECT(run("let a = [1, 2, 3]; a[0];").asNumber() == 1);
  EXPECT(run("let a = [1 + 2, 4 * 5]; a[0] + a[1];").asNumber() == 23);
  EXPECT(run("[10, 20, 30][1];").asNumber() == 20);
}

void testArrayElementAssignment() {
  EXPECT(run("let a = [1, 2, 3]; a[1] = 10; a[1];").asNumber() == 10);
}

void testArrayReferenceSemantics() {
  EXPECT(run("let a = [1, 2, 3]; let b = a; b[0] = 99; a[0];").asNumber() == 99);
}

void testObjectLiteralAndGet() {
  EXPECT(run("let p = { age: 18, score: 100 }; p.age;").asNumber() == 18);
  EXPECT(run("let p = {}; p;").toString() == "[object Object]");
}

void testObjectStringProperty() {
  EXPECT(run("let p = { name: \"Tom\", age: 18 }; p.name;").toString() == "Tom");
}

void testObjectPropertyAssignment() {
  EXPECT(run("let p = { age: 18 }; p.age = 20; p.age;").asNumber() == 20);
}

void testObjectReferenceSemantics() {
  EXPECT(run("let p = { age: 18 }; let q = p; q.age = 30; p.age;").asNumber() == 30);
}

void testUndefinedProperty() {
  try {
    run("let p = { age: 18 }; p.name;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined property: name");
  }
}

void testGetPropertyFromNonObject() {
  try {
    run("1.age;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an object");
  }
}

void testArrayIndexOutOfBounds() {
  try {
    run("let a = [1]; a[1];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array index out of bounds");
  }
}

void testArrayIndexMustBeInteger() {
  try {
    run("let a = [1]; a[0.5];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: array index must be a non-negative integer");
  }
}

void testIndexNonArrayValue() {
  try {
    run("1[0];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an array");
  }
}

void testWhileLoop() { EXPECT(run("let i = 0; while (i < 3) { i = i + 1; } i;").asNumber() == 3); }

void testPrint() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = run("print(1 + 2);");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "3\n");
  EXPECT(result.isNull());
}

void testPrintString() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = run("print(\"Tom\");");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "Tom\n");
  EXPECT(result.isNull());
}

void testBuiltinFunctionCanBeAssigned() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = run("let p = print; p(\"hello\");");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "hello\n");
  EXPECT(result.isNull());
}

void testUnknownFunction() {
  try {
    run("unknown(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined function: unknown");
  }
}

void testPrintArity() {
  try {
    run("print();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: print expects 1 argument");
  }
}

void testHasBuiltinObjectProperty() {
  EXPECT(run("let p = { name: \"Tom\", age: 18 }; has(p, \"name\");").toString() == "true");
  EXPECT(run("let p = { name: \"Tom\" }; has(p, \"score\");").toString() == "false");
}

void testHasBuiltinArrayAndStringProperty() {
  EXPECT(run("let a = [1, 2, 3]; has(a, \"length\");").toString() == "true");
  EXPECT(run("let a = [1, 2, 3]; has(a, \"push\");").toString() == "true");
  EXPECT(run("let a = [1, 2, 3]; has(a, \"pop\");").toString() == "true");
  EXPECT(run("let a = [1, 2, 3]; has(a, \"0\");").toString() == "false");
  EXPECT(run("has(\"Tom\", \"length\");").toString() == "true");
  EXPECT(run("has(1, \"length\");").toString() == "false");
}

void testHasBuiltinArity() {
  try {
    run("has({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has expects 2 arguments");
  }
}

void testHasBuiltinKeyMustBeString() {
  try {
    run("has({}, 1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has key must be a string");
  }
}

void testKeysBuiltinObject() {
  EXPECT(run("let p = { name: \"Tom\", age: 18 }; keys(p).length;").asNumber() == 2);
  EXPECT(run("let ks = keys({ name: \"Tom\", age: 18 }); ks.length == 2 && ks[0] != ks[1];")
             .toString() == "true");
}

void testKeysBuiltinArrayAndString() {
  EXPECT(run("keys([1, 2]).length;").asNumber() == 3);
  EXPECT(run("let ks = keys([1, 2]); has([1, 2], ks[0]) && has([1, 2], ks[1]) && "
             "has([1, 2], ks[2]);")
             .toString() == "true");
  EXPECT(run("keys(\"Tom\").length;").asNumber() == 1);
  EXPECT(run("keys(1).length;").asNumber() == 0);
}

void testKeysBuiltinArity() {
  try {
    run("keys();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: keys expects 1 argument");
  }
}

void testDelBuiltinObjectProperty() {
  EXPECT(
      run("let p = { name: \"Tom\", age: 18 }; del(p, \"name\"); has(p, \"name\");").toString() ==
      "false");
  EXPECT(run("let p = { name: \"Tom\" }; del(p, \"score\");").toString() == "false");
  EXPECT(run("let p = { name: \"Tom\" }; del(p, \"name\");").toString() == "true");
  EXPECT(run("let p = { name: \"Tom\", age: 18 }; del(p, \"name\"); keys(p).length;").asNumber() ==
         1);
}

void testDelBuiltinNonObject() {
  EXPECT(run("del([1, 2], \"length\");").toString() == "false");
  EXPECT(run("del(\"Tom\", \"length\");").toString() == "false");
  EXPECT(run("del(1, \"x\");").toString() == "false");
}

void testDelBuiltinArity() {
  try {
    run("del({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del expects 2 arguments");
  }
}

void testDelBuiltinKeyMustBeString() {
  try {
    run("del({}, 1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del key must be a string");
  }
}

void testFunctionCall() {
  EXPECT(run("function add(a, b) { return a + b; } add(1, 2);").asNumber() == 3);
}

void testFunctionCallCanUseOuterVariable() {
  EXPECT(run("let base = 10; function add(x) { return base + x; } add(5);").asNumber() == 15);
}

void testFunctionArity() {
  try {
    run("function add(a, b) { a + b; } add(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function add expects 2 arguments");
  }
}

void testNonCallableValue() {
  try {
    run("let x = 1; x();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: x is not callable");
  }
}

void testReturnFromFunction() {
  EXPECT(run("function add(a, b) { return a + b; } add(1, 2);").asNumber() == 3);
}

void testBareReturnFromFunction() { EXPECT(run("function f() { return; } f();").isUndefined()); }

void testFunctionWithoutReturnReturnsUndefined() {
  EXPECT(run("function f() { let x = 1; } f();").isUndefined());
  EXPECT(run("function f() { 1; } f();").isUndefined());
}

void testReturnValueStillWorks() { EXPECT(run("function f() { return 1; } f();").asNumber() == 1); }

void testEarlyReturnFromFunction() {
  EXPECT(run("function test(x) { if (x > 0) { return 1; } return 2; } test(10);").asNumber() == 1);
}

void testRecursiveFunction() {
  EXPECT(run("function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } fact(5);")
             .asNumber() == 120);
}

void testCallReturnedFunctionDirectly() {
  EXPECT(run("function makeOne() {"
             "  function one() { return 1; }"
             "  return one;"
             "}"
             "makeOne()();")
             .asNumber() == 1);
}

void testClosureKeepsOuterVariable() {
  EXPECT(run("function makeCounter() {"
             "  let count = 0;"
             "  function next() {"
             "    count = count + 1;"
             "    return count;"
             "  }"
             "  return next;"
             "}"
             "let c = makeCounter();"
             "c();"
             "c();")
             .asNumber() == 2);
}

void testReturnOutsideFunction() {
  try {
    run("return 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testUndefinedVariable() {
  try {
    run("x + 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testDivisionByZero() {
  try {
    run("10 / 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testModuloByZero() {
  try {
    run("10 % 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: modulo by zero");
  }
}

void testAssignUndefinedVariable() {
  try {
    run("x = 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}
void testArrayLengthProperty() {
  EXPECT(run("let a = [1, 2, 3]; a.length;").asNumber() == 3);
  EXPECT(run("[1, 2].length;").asNumber() == 2);
}

void testStringLengthProperty() {
  EXPECT(run("let s = \"hello\"; s.length;").asNumber() == 5);
  EXPECT(run("\"Tom\".length;").asNumber() == 3);
}

void testObjectOwnLengthProperty() {
  EXPECT(run("let p = { length: 10 }; p.length;").asNumber() == 10);
}

void testArrayPush() {
  EXPECT(run("let a = [1, 2]; a.push(3); a.length;").asNumber() == 3);
  EXPECT(run("let a = []; a.push(1); a.push(2); a[0] + a[1];").asNumber() == 3);
  EXPECT(run("let a = []; a.push(10);").asNumber() == 1);
  EXPECT(run("let a = []; a.push(1 + 2); a[0];").asNumber() == 3);
}

void testArrayPop() {
  EXPECT(run("let a = [1, 2]; a.pop();").asNumber() == 2);
  EXPECT(run("let a = [1, 2]; a.pop(); a.length;").asNumber() == 1);
  EXPECT(run("let a = []; a.pop();").isUndefined());
  EXPECT(run("let a = []; a.pop();").toString() == "undefined");
}

void testArrayPushArity() {
  try {
    run("let a = []; a.push();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array.push expects 1 argument");
  }
}

void testArrayPopArity() {
  try {
    run("let a = []; a.pop(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array.pop expects 0 arguments");
  }
}

void testMethodCallOnNonArray() {
  try {
    run("1.push(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value has no method: push");
  }
}

void testUnknownArrayMethod() {
  try {
    run("let a = []; a.unknown();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined method: unknown");
  }
}
}  // namespace

void runInterpreterTests() {
  testExpressionEvaluation();
  testLetAndVariable();
  testMultipleLets();
  testPercent();
  testGrouping();
  testComparison();
  testEquality();
  testReferenceEquality();
  testUnaryOperators();
  testLogicalOperators();
  testLogicalShortCircuit();
  testBooleanAndNullLiterals();
  testUndefinedIsFalsy();
  testStringLiteral();
  testStringConcatenation();
  testIfTrueBranch();
  testIfFalseBranch();
  testIfWithBooleanAndNullConditions();
  testIfWithVariableCondition();
  testIfWithoutElse();
  testBlock();
  testIfWithBlockBranches();
  testBlockScopeShadowsOuterVariable();
  testBlockScopeDoesNotLeakVariable();
  testBlockAssignsOuterVariable();
  testAssignment();
  testArrayLiteralAndIndex();
  testArrayElementAssignment();
  testArrayReferenceSemantics();
  testObjectLiteralAndGet();
  testObjectStringProperty();
  testObjectPropertyAssignment();
  testObjectReferenceSemantics();
  testUndefinedProperty();
  testGetPropertyFromNonObject();
  testArrayIndexOutOfBounds();
  testArrayIndexMustBeInteger();
  testIndexNonArrayValue();
  testWhileLoop();
  testPrint();
  testPrintString();
  testBuiltinFunctionCanBeAssigned();
  testUnknownFunction();
  testPrintArity();
  testHasBuiltinObjectProperty();
  testHasBuiltinArrayAndStringProperty();
  testHasBuiltinArity();
  testHasBuiltinKeyMustBeString();
  testKeysBuiltinObject();
  testKeysBuiltinArrayAndString();
  testKeysBuiltinArity();
  testDelBuiltinObjectProperty();
  testDelBuiltinNonObject();
  testDelBuiltinArity();
  testDelBuiltinKeyMustBeString();
  testFunctionCall();
  testFunctionCallCanUseOuterVariable();
  testFunctionArity();
  testNonCallableValue();
  testReturnFromFunction();
  testBareReturnFromFunction();
  testFunctionWithoutReturnReturnsUndefined();
  testReturnValueStillWorks();
  testEarlyReturnFromFunction();
  testRecursiveFunction();
  testCallReturnedFunctionDirectly();
  testClosureKeepsOuterVariable();
  testReturnOutsideFunction();
  testUndefinedVariable();
  testDivisionByZero();
  testModuloByZero();
  testAssignUndefinedVariable();
  testArrayLengthProperty();
  testStringLengthProperty();
  testObjectOwnLengthProperty();
  testArrayPush();
  testArrayPop();
  testArrayPushArity();
  testArrayPopArity();
  testMethodCallOnNonArray();
  testUnknownArrayMethod();
}
