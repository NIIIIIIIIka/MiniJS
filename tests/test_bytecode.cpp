#include <iostream>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "minijs/baseline_compiler.h"
#include "minijs/baseline_code.h"
#include "minijs/baseline_runtime.h"
#include "minijs/bytecode_decoder.h"
#include "minijs/bytecode_function.h"
#include "minijs/compiler.h"
#include "minijs/disassembler.h"
#include "minijs/parser.h"
#include "minijs/runtime_error.h"
#include "minijs/vm.h"
#include "test_framework.h"

namespace {

bool nativeBaselineBackendAvailable() {
#if defined(__aarch64__) || defined(_M_ARM64) || \
    (defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)))
  return true;
#else
  return false;
#endif
}

minijs::Value runBytecode(std::string_view source) {
  minijs::Parser parser(source);
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  return vm.run(chunk);
}

minijs::Value runBytecodeProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  return vm.run(chunk);
}

minijs::Value runBytecodeProgramOnVm(minijs::VM& vm, std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  return vm.run(chunk);
}

void baselineAbiAddEntry(minijs::BaselineFrame* frame) {
  if (!minijsBaselineGetLocal(frame, 0)) {
    return;
  }
  if (!minijsBaselineGetLocal(frame, 1)) {
    return;
  }
  if (!minijsBaselineAdd(frame)) {
    return;
  }
  minijsBaselineReturn(frame);
}

void baselineAbiConstantEntry(minijs::BaselineFrame* frame) {
  if (!minijsBaselinePushConstant(frame, 0)) {
    return;
  }
  if (!minijsBaselineSetLocal(frame, 0)) {
    return;
  }
  minijsBaselineReturn(frame);
}

void baselineAbiPushEntry(minijs::BaselineFrame* frame) {
  const minijs::Value value(9.0);
  if (!minijsBaselinePush(frame, &value)) {
    return;
  }
  minijsBaselineReturn(frame);
}

void baselineAbiDivisionByZeroEntry(minijs::BaselineFrame* frame) {
  if (!minijsBaselineGetLocal(frame, 0)) {
    return;
  }
  if (!minijsBaselineGetLocal(frame, 1)) {
    return;
  }
  minijsBaselineDiv(frame);
}

void baselineAbiInvalidLocalEntry(minijs::BaselineFrame* frame) {
  minijsBaselineGetLocal(frame, 99);
}

void baselineAbiMutatesAfterReturnEntry(minijs::BaselineFrame* frame) {
  const minijs::Value value(1.0);
  if (!minijsBaselinePush(frame, &value)) {
    return;
  }
  if (!minijsBaselineReturn(frame)) {
    return;
  }
  minijsBaselinePush(frame, &value);
}

void testChunkInlineCacheStartsEmptyAfterCopyOrMove() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Return);

  const std::size_t propertySlot = chunk.addFeedbackSlot(minijs::FeedbackKind::GetProperty);
  minijs::PropertyInlineCache& original = chunk.feedbackSlot(propertySlot).property;
  original.size = 1;
  original.entries[0].slot = 3;
  chunk.feedbackSlot(propertySlot).state = minijs::FeedbackState::Monomorphic;
  const std::size_t methodSlot = chunk.addFeedbackSlot(minijs::FeedbackKind::MethodCall);
  minijs::MethodInlineCache& originalMethod = chunk.feedbackSlot(methodSlot).method;
  originalMethod.size = 1;
  chunk.feedbackSlot(methodSlot).state = minijs::FeedbackState::Polymorphic;

  minijs::Chunk copied(chunk);
  EXPECT(copied.feedbackSlot(propertySlot).property.size == 0);
  EXPECT(copied.feedbackSlot(methodSlot).method.size == 0);
  EXPECT(copied.feedbackSlot(propertySlot).state == minijs::FeedbackState::Uninitialized);
  EXPECT(copied.feedbackSlot(methodSlot).state == minijs::FeedbackState::Uninitialized);

  minijs::Chunk assigned;
  assigned = chunk;
  EXPECT(assigned.feedbackSlot(propertySlot).property.size == 0);
  EXPECT(assigned.feedbackSlot(methodSlot).method.size == 0);
  EXPECT(assigned.feedbackSlot(propertySlot).state == minijs::FeedbackState::Uninitialized);
  EXPECT(assigned.feedbackSlot(methodSlot).state == minijs::FeedbackState::Uninitialized);

  minijs::Chunk moved(std::move(chunk));
  EXPECT(moved.feedbackSlot(propertySlot).property.size == 0);
  EXPECT(moved.feedbackSlot(methodSlot).method.size == 0);
  EXPECT(moved.feedbackSlot(propertySlot).state == minijs::FeedbackState::Uninitialized);
  EXPECT(moved.feedbackSlot(methodSlot).state == minijs::FeedbackState::Uninitialized);

  const std::size_t copiedPropertySlot = copied.addFeedbackSlot(minijs::FeedbackKind::SetProperty);
  minijs::PropertyInlineCache& copiedCache = copied.feedbackSlot(copiedPropertySlot).property;
  copiedCache.size = 1;
  copiedCache.entries[0].slot = 1;
  copied.feedbackSlot(copiedPropertySlot).state = minijs::FeedbackState::Megamorphic;
  const std::size_t copiedMethodSlot = copied.addFeedbackSlot(minijs::FeedbackKind::MethodCall);
  minijs::MethodInlineCache& copiedMethodCache = copied.feedbackSlot(copiedMethodSlot).method;
  copiedMethodCache.size = 1;
  copied.feedbackSlot(copiedMethodSlot).state = minijs::FeedbackState::Monomorphic;

  minijs::Chunk moveAssigned;
  moveAssigned = std::move(copied);
  EXPECT(moveAssigned.feedbackSlot(copiedPropertySlot).property.size == 0);
  EXPECT(moveAssigned.feedbackSlot(copiedMethodSlot).method.size == 0);
  EXPECT(moveAssigned.feedbackSlot(copiedPropertySlot).state ==
         minijs::FeedbackState::Uninitialized);
  EXPECT(moveAssigned.feedbackSlot(copiedMethodSlot).state ==
         minijs::FeedbackState::Uninitialized);
}

void testCompileNumberExpression() { EXPECT(runBytecode("42;").asNumber() == 42); }

void testCompileArithmeticExpression() {
  EXPECT(runBytecode("1 + 2 * 3;").asNumber() == 7);
  EXPECT(runBytecode("(1 + 2) * 3;").asNumber() == 9);
}

void testCompileUnaryMinus() { EXPECT(runBytecode("-10;").asNumber() == -10); }

void testCompileModuloExpression() { EXPECT(runBytecode("10 % 3;").asNumber() == 1); }

void testCompileBooleanNullUndefinedLiterals() {
  EXPECT(runBytecode("true;").toString() == "true");
  EXPECT(runBytecode("false;").toString() == "false");
  EXPECT(runBytecode("null;").isNull());
  EXPECT(runBytecode("undefined;").isUndefined());
}

void testCompileStringLiteral() { EXPECT(runBytecode("\"Tom\";").toString() == "Tom"); }

void testBytecodeStringConstantUsesGcObject() {
  minijs::Parser parser("\"Tom\";");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  EXPECT(vm.objectCount() == 0);
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeNonStringConstantDoesNotAllocateGcObject() {
  minijs::Parser parser("42;");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  EXPECT(vm.run(chunk).asNumber() == 42);
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeGcCollectsUnreachableStringConstant() {
  minijs::Parser parser("\"Tom\";");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 1);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeGcKeepsGlobalString() {
  minijs::Parser parser("let name = \"Tom\"; name;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 1);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcKeepsStringInGlobalObjectField() {
  minijs::Parser parser("let person = { name: \"Tom\" }; person;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeAutoGcCollectsUnreachableStrings() {
  minijs::Parser parser("\"s0\";"
                        "\"s1\";"
                        "\"s2\";"
                        "\"s3\";"
                        "\"s4\";"
                        "\"s5\";"
                        "\"s6\";"
                        "\"s7\";"
                        "\"s8\";"
                        "\"s9\";");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "s9");
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeAutoGcKeepsReachableGlobalStrings() {
  minijs::Parser parser("let s0 = \"s0\";"
                        "let s1 = \"s1\";"
                        "let s2 = \"s2\";"
                        "let s3 = \"s3\";"
                        "let s4 = \"s4\";"
                        "let s5 = \"s5\";"
                        "let s6 = \"s6\";"
                        "let s7 = \"s7\";"
                        "let s8 = \"s8\";"
                        "s0;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "s0");
  EXPECT(vm.objectCount() == 9);
}

void testBytecodeArrayLiteralUsesGcObject() {
  minijs::Parser parser("[1, 2, 3];");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "[1, 2, 3]");
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcKeepsStringInGcArray() {
  minijs::Parser parser("let names = [\"Tom\"]; names;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "[Tom]");
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeAutoGcKeepsArrayElementsDuringAllocation() {
  minijs::Parser parser("let s0 = \"s0\";"
                        "let s1 = \"s1\";"
                        "let s2 = \"s2\";"
                        "let s3 = \"s3\";"
                        "let s4 = \"s4\";"
                        "let s5 = \"s5\";"
                        "let s6 = \"s6\";"
                        "let s7 = \"s7\";"
                        "let s8 = \"s8\";"
                        "let s9 = \"s9\";"
                        "let s10 = \"s10\";"
                        "let s11 = \"s11\";"
                        "let s12 = \"s12\";"
                        "let s13 = \"s13\";"
                        "let s14 = \"s14\";"
                        "let names = [\"Tom\"];"
                        "names;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "[Tom]");
  EXPECT(vm.objectCount() == 17);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 17);
}

void testBytecodeGcCollectsUnreachableArrayAndString() {
  minijs::Parser parser("[\"Tom\"]; undefined;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeVmRootShapeIsInternalObject() {
  minijs::VM vm;

  EXPECT(vm.debugHasRootObjectShape());
  EXPECT(vm.objectCount() == 0);

  vm.collectGarbage();

  EXPECT(vm.debugHasRootObjectShape());
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeObjectLiteralUsesGcObject() {
  minijs::Parser parser("{ age: 18 };");
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compile(*expression);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "[object Object]");
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcKeepsStringInGcObject() {
  minijs::Parser parser("let person = { name: \"Tom\" }; person;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeGcKeepsDynamicShapeSlotValue() {
  minijs::Parser parser("let p = {};"
                        "p.name = \"Tom\";"
                        "p;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeGcKeepsDictionaryModeObjectValue() {
  minijs::Parser parser("let p = { old: 1 };"
                        "del(p, \"old\");"
                        "p.name = \"Tom\";"
                        "p;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.debugGlobalObjectUsesDictionary("p"));
  EXPECT(vm.objectCount() == 3);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeGcCollectsUnreachableObjectAndString() {
  minijs::Parser parser("let person = { name: \"Tom\" }; person = undefined; person;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeAutoGcKeepsObjectPropertiesDuringAllocation() {
  minijs::Parser parser("let s0 = \"s0\";"
                        "let s1 = \"s1\";"
                        "let s2 = \"s2\";"
                        "let s3 = \"s3\";"
                        "let s4 = \"s4\";"
                        "let s5 = \"s5\";"
                        "let s6 = \"s6\";"
                        "let s7 = \"s7\";"
                        "let s8 = \"s8\";"
                        "let s9 = \"s9\";"
                        "let s10 = \"s10\";"
                        "let s11 = \"s11\";"
                        "let s12 = \"s12\";"
                        "let s13 = \"s13\";"
                        "let s14 = \"s14\";"
                        "let person = { name: \"Tom\" };"
                        "person.name;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 17);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 17);
}

void testBytecodeGcMarksNestedObjectGraph() {
  minijs::Parser parser("let value = { names: [\"Tom\"] }; value;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 3);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 3);
}

void testBytecodeManualGcIsIdempotent() {
  {
    minijs::Parser parser("let keep = { name: \"Tom\", values: [\"x\"] }; keep;");
    minijs::Program program = parser.parseProgram();

    EXPECT(parser.diagnostics().empty());

    minijs::Compiler compiler;
    minijs::Chunk chunk = compiler.compileProgram(program);

    minijs::VM vm;
    EXPECT(vm.run(chunk).isObject());
    EXPECT(vm.objectCount() == 4);

    vm.collectGarbage();
    EXPECT(vm.objectCount() == 4);

    vm.collectGarbage();
    EXPECT(vm.objectCount() == 4);
  }

  {
    minijs::Parser parser("{ let temp = { name: \"Tom\" }; } undefined;");
    minijs::Program program = parser.parseProgram();

    EXPECT(parser.diagnostics().empty());

    minijs::Compiler compiler;
    minijs::Chunk chunk = compiler.compileProgram(program);

    minijs::VM vm;
    EXPECT(vm.run(chunk).isUndefined());
    EXPECT(vm.objectCount() == 2);

    vm.collectGarbage();
    EXPECT(vm.objectCount() == 0);

    vm.collectGarbage();
    EXPECT(vm.objectCount() == 0);
  }
}

void testBytecodeAutoGcPressureKeepsReachableObjectGraph() {
  minijs::Parser parser("let keep = { name: \"Tom\", values: [1, 2, 3] };"
                        "let i = 0;"
                        "while (i < 40) {"
                        "  [i, \"tmp\", { value: i }];"
                        "  i = i + 1;"
                        "}"
                        "keep.name;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 3);
}

void testBytecodeAutoGcPressureKeepsClosureClassGraph() {
  minijs::Parser parser("function makeBox() {"
                        "  let prefix = \"hi\";"
                        "  class Box { get() { return prefix; } }"
                        "  return Box;"
                        "}"
                        "let Box = makeBox();"
                        "let b = Box();"
                        "let i = 0;"
                        "while (i < 40) {"
                        "  [i, \"tmp\", { value: i }];"
                        "  i = i + 1;"
                        "}"
                        "b.get();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "hi");

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 8);
}

void testBytecodeGcMarksClosedUpvalueValue() {
  minijs::Parser parser("function make() {"
                        "  let name = \"Tom\";"
                        "  function get() { return name; }"
                        "  return get;"
                        "}"
                        "let get = make();"
                        "get();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 6);
}

void testBytecodeClosureReturnsCompatibleValue() {
  minijs::Parser parser("function get() { return \"Tom\"; } get;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeClosure());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeGlobalClosureKeepsGcObjectGraph() {
  minijs::Parser parser("let saved = undefined;"
                        "{ function get() { return \"Tom\"; } saved = get; }"
                        "saved;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeClosure());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}

void testBytecodeGcCollectsUnreachableClosure() {
  minijs::Parser parser("{ function get() { return \"Tom\"; } } undefined;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeUpvalueReturnsCompatibleClosureValue() {
  minijs::Parser parser("function make() {"
                        "  let name = \"Tom\";"
                        "  function get() { return name; }"
                        "  return get;"
                        "}"
                        "let get = make();"
                        "get;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeClosure());
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 6);
}

void testBytecodeGcCollectsUnreachableUpvalue() {
  minijs::Parser parser("{"
                        "  function make() {"
                        "    let name = \"Tom\";"
                        "    function get() { return name; }"
                        "    return get;"
                        "  }"
                        "  make();"
                        "}"
                        "undefined;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeGcMarksClassMethodClosureUpvalues() {
  minijs::Parser parser("function makeBox() {"
                        "  let prefix = \"hi\";"
                        "  class Box { get() { return prefix; } }"
                        "  return Box;"
                        "}"
                        "let Box = makeBox();"
                        "let b = Box();"
                        "b.get();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "hi");
  EXPECT(vm.objectCount() == 8);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 8);
}

void testBytecodeClassReturnsCompatibleValue() {
  minijs::Parser parser("class Box {} Box;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeClass());
  EXPECT(vm.objectCount() == 1);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcCollectsUnreachableClass() {
  minijs::Parser parser("{ class Box {} } undefined;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 1);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeClassInstanceReturnsCompatibleValue() {
  minijs::Parser parser("class Box {} Box();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeInstance());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcKeepsStringInInstanceField() {
  minijs::Parser parser("class Box {}"
                        "let b = Box();"
                        "b.name = \"Tom\";"
                        "b;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeInstance());
  EXPECT(vm.objectCount() == 3);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 3);
}

void testBytecodeGcCollectsUnreachableInstanceAndField() {
  minijs::Parser parser("class Box {}"
                        "let b = Box();"
                        "b.name = \"Tom\";"
                        "b = undefined;"
                        "b;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());
  EXPECT(vm.objectCount() == 3);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 1);
}

void testBytecodeGcBoundMethodKeepsReceiverInstance() {
  minijs::Parser parser("class Box { get() { return this.name; } }"
                        "let b = Box();"
                        "b.name = \"Tom\";"
                        "let get = b.get;"
                        "get();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 6);
}

void testBytecodeBoundMethodReturnsCompatibleValue() {
  minijs::Parser parser("class Box { get() { return this.name; } }"
                        "let b = Box();"
                        "b.name = \"Tom\";"
                        "b.get;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeBoundMethod());
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 5);
}

void testBytecodeGcBoundMethodKeepsReceiverAfterOriginalVariableCleared() {
  minijs::Parser parser("class Box { get() { return this.name; } }"
                        "let b = Box();"
                        "b.name = \"Tom\";"
                        "let get = b.get;"
                        "b = undefined;"
                        "get();");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).toString() == "Tom");
  EXPECT(vm.objectCount() == 6);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 6);
}

void testBytecodeGcMarksInstanceFieldObjectGraph() {
  minijs::Parser parser("class Box {}"
                        "let b = Box();"
                        "b.value = { names: [\"Tom\"] };"
                        "b;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isBytecodeInstance());
  EXPECT(vm.objectCount() == 5);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 5);
}

void testCompileStringConcatenation() {
  EXPECT(runBytecode("\"hello \" + \"world\";").toString() == "hello world");
  EXPECT(runBytecode("\"age: \" + 18;").toString() == "age: 18");
  EXPECT(runBytecode("18 + \" years\";").toString() == "18 years");
}

void testCompileComparisonExpressions() {
  EXPECT(runBytecode("1 < 2;").toString() == "true");
  EXPECT(runBytecode("1 <= 2;").toString() == "true");
  EXPECT(runBytecode("2 > 1;").toString() == "true");
  EXPECT(runBytecode("2 >= 1;").toString() == "true");
  EXPECT(runBytecode("1 == 1;").toString() == "true");
  EXPECT(runBytecode("1 != 2;").toString() == "true");
  EXPECT(runBytecode("1 > 2;").toString() == "false");
  EXPECT(runBytecode("2 <= 1;").toString() == "false");
}

void testCompileLogicalNot() {
  EXPECT(runBytecode("!false;").toString() == "true");
  EXPECT(runBytecode("!true;").toString() == "false");
  EXPECT(runBytecode("!0;").toString() == "true");
  EXPECT(runBytecode("!1;").toString() == "false");
}

void testCompileLogicalAndShortCircuit() {
  EXPECT(runBytecodeProgram("false && unknown;").toString() == "false");
  EXPECT(runBytecodeProgram("true && 42;").asNumber() == 42);
}

void testCompileLogicalOrShortCircuit() {
  EXPECT(runBytecodeProgram("true || unknown;").toString() == "true");
  EXPECT(runBytecodeProgram("false || 42;").asNumber() == 42);
}

void testCompileLogicalReturnsOperandValue() {
  EXPECT(runBytecodeProgram("0 || 2;").asNumber() == 2);
  EXPECT(runBytecodeProgram("1 || 2;").asNumber() == 1);
  EXPECT(runBytecodeProgram("0 && 2;").asNumber() == 0);
  EXPECT(runBytecodeProgram("1 && 2;").asNumber() == 2);
}

void testCompileLogicalWithBuiltins() {
  EXPECT(runBytecodeProgram(
             "let p = { name: \"Tom\", age: 18 };"
             "let ks = keys(p);"
             "len(ks) == 2 && has(p, ks[0]) && has(p, ks[1]);")
             .toString() == "true");
}

minijs::Chunk compileExpression(std::string_view source) {
  minijs::Parser parser(source);
  minijs::ExprPtr expression = parser.parse();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  return compiler.compile(*expression);
}

minijs::Chunk compileProgram(std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  return compiler.compileProgram(program);
}

minijs::DecodedInstruction findDecodedInstruction(const minijs::Chunk& chunk,
                                                  minijs::Opcode opcode) {
  std::size_t offset = 0;
  while (offset < chunk.code().size()) {
    minijs::DecodedInstruction instruction = minijs::decodeInstruction(chunk, offset);
    if (instruction.opcode == opcode) {
      return instruction;
    }
    offset = instruction.nextOffset;
  }

  EXPECT(false);
  return {};
}

std::shared_ptr<minijs::BytecodeFunction> lastBytecodeFunctionNamed(
    const minijs::Chunk& chunk, std::string_view name) {
  std::shared_ptr<minijs::BytecodeFunction> result;
  for (const minijs::Value& constant : chunk.constants()) {
    if (constant.isBytecodeFunction() && constant.asBytecodeFunction()->name == name) {
      result = constant.asBytecodeFunction();
    }
  }
  return result;
}

void testDecodeInstructionReadsFeedbackOperands() {
  const minijs::Chunk chunk = compileProgram("let p = { age: 18 };"
                                             "p.age = 20;"
                                             "p.age;"
                                             "let a = [];"
                                             "a.push(1);");

  const minijs::DecodedInstruction get =
      findDecodedInstruction(chunk, minijs::Opcode::GetProperty);
  EXPECT(get.operandCount == 2);
  EXPECT(get.nextOffset == get.offset + 3);
  EXPECT(get.operands[1] < chunk.feedbackSlots().size());
  EXPECT(chunk.feedbackSlots()[get.operands[1]].kind == minijs::FeedbackKind::GetProperty);

  const minijs::DecodedInstruction set =
      findDecodedInstruction(chunk, minijs::Opcode::SetProperty);
  EXPECT(set.operandCount == 2);
  EXPECT(set.nextOffset == set.offset + 3);
  EXPECT(set.operands[1] < chunk.feedbackSlots().size());
  EXPECT(chunk.feedbackSlots()[set.operands[1]].kind == minijs::FeedbackKind::SetProperty);

  const minijs::DecodedInstruction call =
      findDecodedInstruction(chunk, minijs::Opcode::MethodCall);
  EXPECT(call.operandCount == 3);
  EXPECT(call.nextOffset == call.offset + 4);
  EXPECT(call.operands[2] < chunk.feedbackSlots().size());
  EXPECT(chunk.feedbackSlots()[call.operands[2]].kind == minijs::FeedbackKind::MethodCall);
}

void testDecodeInstructionReadsSuperCallWithoutFeedbackOperand() {
  const minijs::Chunk chunk =
      compileProgram("class Parent { speak() { return \"parent\"; } }"
                     "class Child < Parent { speak() { return super.speak(); } }"
                     "Child().speak();");

  const auto childSpeak = lastBytecodeFunctionNamed(chunk, "speak");
  EXPECT(childSpeak != nullptr);

  const minijs::DecodedInstruction superCall =
      findDecodedInstruction(childSpeak->chunk, minijs::Opcode::SuperCall);
  EXPECT(superCall.operandCount == 2);
  EXPECT(superCall.nextOffset == superCall.offset + 3);
  EXPECT(childSpeak->chunk.feedbackSlots().empty());
}

void testVerifyBytecodeAcceptsCompiledChunk() {
  const minijs::Chunk chunk = compileProgram("let p = { age: 18 };"
                                             "p.age = p.age + 1;"
                                             "p.age;");

  const minijs::BytecodeVerificationResult result = minijs::verifyBytecode(chunk);
  EXPECT(result.valid);
  EXPECT(result.error.empty());
  EXPECT(!result.instructionOffsets.empty());
}

void testVerifyBytecodeRejectsTruncatedInstruction() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Constant);

  const minijs::BytecodeVerificationResult result = minijs::verifyBytecode(chunk);
  EXPECT(!result.valid);
  EXPECT(result.error.find("out of bounds") != std::string::npos);
}

void testVerifyBytecodeRejectsMismatchedFeedbackSlotKind() {
  minijs::Chunk chunk;
  const std::uint8_t nameIndex =
      static_cast<std::uint8_t>(chunk.addConstant(minijs::Value(std::string("age"))));
  const std::uint8_t feedbackSlot =
      static_cast<std::uint8_t>(chunk.addFeedbackSlot(minijs::FeedbackKind::SetProperty));

  chunk.writeOpcode(minijs::Opcode::GetProperty);
  chunk.writeByte(nameIndex);
  chunk.writeByte(feedbackSlot);
  chunk.writeOpcode(minijs::Opcode::Return);

  const minijs::BytecodeVerificationResult result = minijs::verifyBytecode(chunk);
  EXPECT(!result.valid);
  EXPECT(result.error.find("feedback slot kind mismatch") != std::string::npos);
}

void testVerifyBytecodeRejectsJumpIntoOperandBytes() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Jump);
  chunk.writeByte(0);
  chunk.writeByte(1);
  chunk.writeOpcode(minijs::Opcode::Return);

  const minijs::BytecodeVerificationResult result = minijs::verifyBytecode(chunk);
  EXPECT(!result.valid);
  EXPECT(result.error.find("jump target is not an instruction") != std::string::npos);
}

void testDisassembleArithmeticExpression() {
  const minijs::Chunk chunk = compileExpression("1 + 2 * 3;");

  const std::string expected =
      "0000 OP_CONSTANT 0 1\n"
      "0002 OP_CONSTANT 1 2\n"
      "0004 OP_CONSTANT 2 3\n"
      "0006 OP_MUL\n"
      "0007 OP_ADD\n"
      "0008 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleUnaryMinus() {
  const minijs::Chunk chunk = compileExpression("-10;");

  const std::string expected =
      "0000 OP_CONSTANT 0 10\n"
      "0002 OP_NEGATE\n"
      "0003 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleStringLiteral() {
  const minijs::Chunk chunk = compileExpression("\"Tom\";");

  const std::string expected =
      "0000 OP_CONSTANT 0 Tom\n"
      "0002 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleBooleanNullUndefinedLiterals() {
  EXPECT(minijs::disassembleChunk(compileExpression("true;")) ==
         "0000 OP_CONSTANT 0 true\n"
         "0002 OP_RETURN\n");
  EXPECT(minijs::disassembleChunk(compileExpression("null;")) ==
         "0000 OP_CONSTANT 0 null\n"
         "0002 OP_RETURN\n");
  EXPECT(minijs::disassembleChunk(compileExpression("undefined;")) ==
         "0000 OP_CONSTANT 0 undefined\n"
         "0002 OP_RETURN\n");
}

void testDisassembleLogicalAndJump() {
  const minijs::Chunk chunk = compileExpression("true && 42;");

  const std::string expected =
      "0000 OP_CONSTANT 0 true\n"
      "0002 OP_JUMP_IF_FALSE 2 -> 8\n"
      "0005 OP_POP\n"
      "0006 OP_CONSTANT 1 42\n"
      "0008 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleLogicalOrJump() {
  const minijs::Chunk chunk = compileExpression("true || 42;");

  const std::string expected =
      "0000 OP_CONSTANT 0 true\n"
      "0002 OP_JUMP_IF_FALSE 2 -> 8\n"
      "0005 OP_JUMP 5 -> 11\n"
      "0008 OP_POP\n"
      "0009 OP_CONSTANT 1 42\n"
      "0011 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleClosureCapturesOuterLocal() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 10;"
                     "  function inner() {"
                     "    return x;"
                     "  }"
                     "  return inner;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const std::string outerBytecode = minijs::disassembleChunk(outer->chunk);

  EXPECT(outerBytecode.find("OP_CONSTANT 0 10") != std::string::npos);
  EXPECT(outerBytecode.find("OP_CLOSURE 1 <function inner>") != std::string::npos);
  EXPECT(outerBytecode.find("| local 0") != std::string::npos);
  EXPECT(outerBytecode.find("OP_GET_LOCAL 1") != std::string::npos);

  const auto inner = outer->chunk.constant(1).asBytecodeFunction();
  const std::string innerBytecode = minijs::disassembleChunk(inner->chunk);

  EXPECT(innerBytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
  EXPECT(innerBytecode.find("OP_RETURN") != std::string::npos);
}

void testDisassembleClosureCapturesThroughUpvalue() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 10;"
                     "  function middle() {"
                     "    function inner() {"
                     "      return x;"
                     "    }"
                     "    return inner;"
                     "  }"
                     "  return middle;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const std::string outerBytecode = minijs::disassembleChunk(outer->chunk);
  EXPECT(outerBytecode.find("OP_CLOSURE 1 <function middle>") != std::string::npos);
  EXPECT(outerBytecode.find("| local 0") != std::string::npos);

  const auto middle = outer->chunk.constant(1).asBytecodeFunction();
  const std::string middleBytecode = minijs::disassembleChunk(middle->chunk);
  EXPECT(middleBytecode.find("OP_CLOSURE 0 <function inner>") != std::string::npos);
  EXPECT(middleBytecode.find("| upvalue 0") != std::string::npos);

  const auto inner = middle->chunk.constant(0).asBytecodeFunction();
  const std::string innerBytecode = minijs::disassembleChunk(inner->chunk);
  EXPECT(innerBytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
}

void testDisassembleClosureSetsUpvalue() {
  const minijs::Chunk script =
      compileProgram("function outer() {"
                     "  let x = 0;"
                     "  function add() {"
                     "    x = x + 1;"
                     "    return x;"
                     "  }"
                     "  return add;"
                     "}"
                     "outer();");

  const auto outer = script.constant(0).asBytecodeFunction();
  const auto add = outer->chunk.constant(1).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(add->chunk);

  EXPECT(bytecode.find("OP_GET_UPVALUE 0") != std::string::npos);
  EXPECT(bytecode.find("OP_SET_UPVALUE 0") != std::string::npos);
}

void testDisassembleClosureClosesBlockLocal() {
  const minijs::Chunk script =
      compileProgram("function make() {"
                     "  let get = undefined;"
                     "  {"
                     "    let x = 42;"
                     "    function inner() { return x; }"
                     "    get = inner;"
                     "  }"
                     "  return get;"
                     "}"
                     "make();");

  const auto make = script.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(make->chunk);

  EXPECT(bytecode.find("OP_CLOSURE 2 <function inner>") != std::string::npos);
  EXPECT(bytecode.find("| local 1") != std::string::npos);
  EXPECT(bytecode.find("OP_CLOSE_UPVALUE") != std::string::npos);
}

void testDisassembleClassMethod() {
  const minijs::Chunk chunk = compileProgram("class Box { get() { return 123; } } Box;");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_CLASS") != std::string::npos);
  EXPECT(bytecode.find("Box") != std::string::npos);
  EXPECT(bytecode.find("OP_CLOSURE") != std::string::npos);
  EXPECT(bytecode.find("<function get>") != std::string::npos);
  EXPECT(bytecode.find("OP_METHOD") != std::string::npos);
  EXPECT(bytecode.find("get") != std::string::npos);
}

void testDisassembleClassStaticMethod() {
  const minijs::Chunk chunk =
      compileProgram("class Box { static make() { return 123; } } Box;");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_CLASS") != std::string::npos);
  EXPECT(bytecode.find("<function make>") != std::string::npos);
  EXPECT(bytecode.find("OP_STATIC_METHOD") != std::string::npos);
  EXPECT(bytecode.find("make") != std::string::npos);
}

void testDisassembleClassInheritance() {
  const minijs::Chunk chunk =
      compileProgram("class Animal {} class Dog < Animal { speak() { return \"woof\"; } } Dog;");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_CLASS") != std::string::npos);
  EXPECT(bytecode.find("Animal") != std::string::npos);
  EXPECT(bytecode.find("Dog") != std::string::npos);
  EXPECT(bytecode.find("OP_INHERIT") != std::string::npos);
  EXPECT(bytecode.find("OP_METHOD") != std::string::npos);
  EXPECT(bytecode.find("speak") != std::string::npos);
}

void testDisassembleSuperMethodCall() {
  const minijs::Chunk chunk =
      compileProgram("class Animal { speak() { return \"animal\"; } }"
                     "class Dog < Animal { speak() { return super.speak(); } }"
                     "Dog().speak();");
  const std::string bytecode = minijs::disassembleChunk(chunk);

  EXPECT(bytecode.find("OP_INHERIT") != std::string::npos);
  EXPECT(bytecode.find("speak") != std::string::npos);

  const auto dogSpeak = lastBytecodeFunctionNamed(chunk, "speak");
  EXPECT(dogSpeak != nullptr);
  const std::string methodBytecode = minijs::disassembleChunk(dogSpeak->chunk);
  EXPECT(methodBytecode.find("OP_SUPER_CALL") != std::string::npos);
  EXPECT(methodBytecode.find("speak") != std::string::npos);
  EXPECT(methodBytecode.find("feedback=") == std::string::npos);
  EXPECT(methodBytecode.find("OP_RETURN") != std::string::npos);
}

void testDisassembleBreakClosesUpvalueBeforeJump() {
  const minijs::Chunk chunk =
      compileProgram("let saved = undefined;"
                     "while (true) {"
                     "  {"
                     "    let x = 42;"
                     "    function get() { return x; }"
                     "    saved = get;"
                     "    break;"
                     "  }"
                     "}"
                     "saved();");

  const std::string bytecode = minijs::disassembleChunk(chunk);
  const std::size_t closePosition = bytecode.find("OP_CLOSE_UPVALUE");
  const std::size_t breakJumpPosition = bytecode.find("OP_JUMP ", closePosition);

  EXPECT(closePosition != std::string::npos);
  EXPECT(breakJumpPosition != std::string::npos);
  EXPECT(closePosition < breakJumpPosition);
}

void testDisassembleLocalRecursionGetsCurrentClosure() {
  const minijs::Chunk script =
      compileProgram("function makeFact() {"
                     "  function fact(n) {"
                     "    if (n <= 1) return 1;"
                     "    return n * fact(n - 1);"
                     "  }"
                     "  return fact;"
                     "}"
                     "makeFact();");

  const auto makeFact = script.constant(0).asBytecodeFunction();
  const auto fact = makeFact->chunk.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(fact->chunk);

  EXPECT(bytecode.find("OP_GET_CURRENT_CLOSURE") != std::string::npos);
  EXPECT(bytecode.find("OP_CALL 1") != std::string::npos);
  EXPECT(bytecode.find("OP_GET_GLOBAL") == std::string::npos);
}

void testDisassembleParameterShadowsCurrentClosure() {
  const minijs::Chunk script =
      compileProgram("function test(test) {"
                     "  return test;"
                     "}"
                     "test(42);");

  const auto test = script.constant(0).asBytecodeFunction();
  const std::string bytecode = minijs::disassembleChunk(test->chunk);

  EXPECT(bytecode.find("OP_GET_LOCAL 0") != std::string::npos);
  EXPECT(bytecode.find("OP_GET_CURRENT_CLOSURE") == std::string::npos);
}

void testDisassembleWhileBreakJump() {
  const minijs::Chunk chunk = compileProgram("let i = 0; while (true) { break; } i;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 i\n"
      "0004 OP_CONSTANT 2 true\n"
      "0006 OP_JUMP_IF_FALSE 6 -> 16\n"
      "0009 OP_POP\n"
      "0010 OP_JUMP 10 -> 17\n"
      "0013 OP_LOOP 13 -> 4\n"
      "0016 OP_POP\n"
      "0017 OP_GET_GLOBAL 3 i\n"
      "0019 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleWhileContinueLoop() {
  const minijs::Chunk chunk = compileProgram("let i = 0; while (true) { continue; } i;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 i\n"
      "0004 OP_CONSTANT 2 true\n"
      "0006 OP_JUMP_IF_FALSE 6 -> 16\n"
      "0009 OP_POP\n"
      "0010 OP_LOOP 10 -> 4\n"
      "0013 OP_LOOP 13 -> 4\n"
      "0016 OP_POP\n"
      "0017 OP_GET_GLOBAL 3 i\n"
      "0019 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleForContinueJumpsToIncrement() {
  const minijs::Chunk chunk =
      compileProgram("let sum = 0; for (let i = 0; i < 2; i = i + 1) { continue; } sum;");

  const std::string expected =
      "0000 OP_CONSTANT 0 0\n"
      "0002 OP_DEFINE_GLOBAL 1 sum\n"
      "0004 OP_CONSTANT 2 0\n"
      "0006 OP_DEFINE_GLOBAL 3 i\n"
      "0008 OP_GET_GLOBAL 4 i\n"
      "0010 OP_CONSTANT 5 2\n"
      "0012 OP_LESS\n"
      "0013 OP_JUMP_IF_FALSE 13 -> 31\n"
      "0016 OP_POP\n"
      "0017 OP_JUMP 17 -> 20\n"
      "0020 OP_GET_GLOBAL 6 i\n"
      "0022 OP_CONSTANT 7 1\n"
      "0024 OP_ADD\n"
      "0025 OP_SET_GLOBAL 8 i\n"
      "0027 OP_POP\n"
      "0028 OP_LOOP 28 -> 8\n"
      "0031 OP_POP\n"
      "0032 OP_GET_GLOBAL 9 sum\n"
      "0034 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testCompileGlobalLet() { EXPECT(runBytecodeProgram("let x = 10; x;").asNumber() == 10); }

void testCompileGlobalExpressionUsesVariable() {
  EXPECT(runBytecodeProgram("let x = 10; x + 20;").asNumber() == 30);
}

void testCompileGlobalAssignment() {
  EXPECT(runBytecodeProgram("let x = 1; x = x + 2; x;").asNumber() == 3);
}

void testCompileIfElseStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (true) {"
                            "  x = 1;"
                            "} else {"
                            "  x = 2;"
                            "}"
                            "x;")
             .asNumber() == 1);

  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (false) {"
                            "  x = 1;"
                            "} else {"
                            "  x = 2;"
                            "}"
                            "x;")
             .asNumber() == 2);
}

void testCompileIfWithoutElseStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (true) {"
                            "  x = 1;"
                            "}"
                            "x;")
             .asNumber() == 1);

  EXPECT(runBytecodeProgram("let x = 0;"
                            "if (false) {"
                            "  x = 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileStatement() {
  EXPECT(runBytecodeProgram("let x = 3;"
                            "while (x > 0) {"
                            "  x = x - 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileSkippedStatement() {
  EXPECT(runBytecodeProgram("let x = 0;"
                            "while (x > 0) {"
                            "  x = x - 1;"
                            "}"
                            "x;")
             .asNumber() == 0);
}

void testCompileWhileWithNestedIfStatement() {
  EXPECT(runBytecodeProgram("let x = 4;"
                            "let y = 0;"
                            "while (x > 0) {"
                            "  if (x > 2) {"
                            "    y = y + 10;"
                            "  } else {"
                            "    y = y + 1;"
                            "  }"
                            "  x = x - 1;"
                            "}"
                            "y;")
             .asNumber() == 22);
}

void testCompileForStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 1; i <= 3; i = i + 1) {"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 6);
}

void testCompileForSkippedStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 5; i <= 3; i = i + 1) {"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 0);
}

void testCompileForWithoutInitializerStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "for (; i < 3; i = i + 1) {"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileWhileBreakStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (true) {"
                            "  i = i + 1;"
                            "  if (i == 3) break;"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileWhileContinueStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "let sum = 0;"
                            "while (i < 5) {"
                            "  i = i + 1;"
                            "  if (i == 3) continue;"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 12);
}

void testCompileForBreakStatement() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "for (; true; i = i + 1) {"
                            "  if (i == 3) break;"
                            "}"
                            "i;")
             .asNumber() == 3);
}

void testCompileForContinueStatement() {
  EXPECT(runBytecodeProgram("let sum = 0;"
                            "for (let i = 0; i < 5; i = i + 1) {"
                            "  if (i == 3) continue;"
                            "  sum = sum + i;"
                            "}"
                            "sum;")
             .asNumber() == 7);
}

void testCompileBreakContinuePopBlockLocals() {
  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (i < 3) {"
                            "  {"
                            "    let temp = i;"
                            "    i = i + 1;"
                            "    continue;"
                            "  }"
                            "}"
                            "i;")
             .asNumber() == 3);

  EXPECT(runBytecodeProgram("let i = 0;"
                            "while (true) {"
                            "  {"
                            "    let temp = 10;"
                            "    i = i + temp;"
                            "    break;"
                            "  }"
                            "}"
                            "i;")
             .asNumber() == 10);
}

void testCompileBreakOutsideLoop() {
  try {
    runBytecodeProgram("break;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: break outside loop");
  }
}

void testCompileContinueOutsideLoop() {
  try {
    runBytecodeProgram("continue;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: continue outside loop");
  }
}

void testBytecodeFunctionDoesNotInheritOuterBreakContext() {
  try {
    runBytecodeProgram("while (true) {"
                       "  function stop() {"
                       "    break;"
                       "  }"
                       "  break;"
                       "}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: break outside loop");
  }
}

void testBytecodeFunctionDoesNotInheritOuterContinueContext() {
  try {
    runBytecodeProgram("while (true) {"
                       "  function next() {"
                       "    continue;"
                       "  }"
                       "  break;"
                       "}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: continue outside loop");
  }
}

void testBytecodeFunctionUsesOwnLoopContext() {
  EXPECT(runBytecodeProgram("function count() {"
                            "  let i = 0;"
                            "  while (true) {"
                            "    i = i + 1;"
                            "    if (i == 3) break;"
                            "  }"
                            "  return i;"
                            "}"
                            "count();")
             .asNumber() == 3);
}

void testCompileBlockLocalVariable() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  result = x;"
                            "}"
                            "result;")
             .asNumber() == 1);
}

void testCompileNestedBlockLocalVariables() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  {"
                            "    let y = 2;"
                            "    result = x + y;"
                            "  }"
                            "}"
                            "result;")
             .asNumber() == 3);
}

void testCompileLocalAssignment() {
  EXPECT(runBytecodeProgram("let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  x = x + 2;"
                            "  result = x;"
                            "}"
                            "result;")
             .asNumber() == 3);
}

void testCompileLocalShadowsGlobal() {
  EXPECT(runBytecodeProgram("let x = 10;"
                            "let result = 0;"
                            "{"
                            "  let x = 1;"
                            "  result = x;"
                            "}"
                            "x + result;")
             .asNumber() == 11);
}

void testCompileDuplicateLocalDeclaration() {
  try {
    runBytecodeProgram("{ let x = 1; let x = 2; }");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: variable already declared in this scope: x");
  }
}

void testCompileBytecodeFunctionCall() {
  EXPECT(runBytecodeProgram("function add(a, b) {"
                            "  return a + b;"
                            "}"
                            "add(1, 2);")
             .asNumber() == 3);
}

void testCompileBytecodeFunctionReturnsArgument() {
  EXPECT(runBytecodeProgram("function id(x) {"
                            "  return x;"
                            "}"
                            "id(42);")
             .asNumber() == 42);
}

void testCompileBytecodeFunctionParametersUseLocalSlots() {
  EXPECT(runBytecodeProgram("function pick(a, b, c) {"
                            "  return b;"
                            "}"
                            "pick(10, 20, 30);")
             .asNumber() == 20);
}

void testCompileBytecodeFunctionParametersAndLocalsUseSlots() {
  EXPECT(runBytecodeProgram("function addWithLocal(a, b) {"
                            "  let c = a + b;"
                            "  return c;"
                            "}"
                            "addWithLocal(10, 20);")
             .asNumber() == 30);
}

void testCompileBytecodeFunctionParameterRedeclaration() {
  try {
    runBytecodeProgram(
        "function f(a) {"
        "  let a = 2;"
        "  return a;"
        "}"
        "f(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: variable already declared in this scope: a");
  }
}

void testBytecodeParameterShadowsCurrentFunctionName() {
  EXPECT(runBytecodeProgram("function test(test) {"
                            "  return test;"
                            "}"
                            "test(42);")
             .asNumber() == 42);
}

void testBytecodeLocalShadowsCurrentFunctionName() {
  EXPECT(runBytecodeProgram("function get() {"
                            "  let get = 10;"
                            "  return get;"
                            "}"
                            "get();")
             .asNumber() == 10);
}

void testCompileBytecodeFunctionReadsGlobalVariable() {
  EXPECT(runBytecodeProgram("let base = 10;"
                            "function addBase(x) {"
                            "  return x + base;"
                            "}"
                            "addBase(5);")
             .asNumber() == 15);
}

void testCompileBytecodeFunctionAssignsGlobalVariable() {
  EXPECT(runBytecodeProgram("let total = 1;"
                            "function add(x) {"
                            "  total = total + x;"
                            "}"
                            "add(4);"
                            "total;")
             .asNumber() == 5);
}

void testBytecodeCallReturnedFunctionDirectly() {
  EXPECT(runBytecodeProgram("function makeOne() {"
                            "  function one() { return 1; }"
                            "  return one;"
                            "}"
                            "makeOne()();")
             .asNumber() == 1);
}

void testBytecodeClassMethodCall() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  get() { return 123; }"
                            "}"
                            "let b = Box();"
                            "b.get();")
             .asNumber() == 123);
}

void testBytecodeClassBoundMethodCall() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  get() { return 123; }"
                            "}"
                            "let b = Box();"
                            "let get = b.get;"
                            "get();")
             .asNumber() == 123);
}

void testBytecodeClassStaticMethodCall() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  static make(value) { return value + 1; }"
                            "}"
                            "Box.make(41);")
             .asNumber() == 42);
}

void testBytecodeClassStaticFactoryMethod() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  static create(value) { return Box(value); }"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "Box.create(123).get();")
             .asNumber() == 123);
}

void testBytecodeClassStaticAndInstanceMethodsAreSeparate() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  static name() { return \"class\"; }"
                            "  name() { return \"instance\"; }"
                            "}"
                            "Box.name() + \" \" + Box().name();")
             .toString() == "class instance");
}

void testBytecodeClassInheritsStaticMethod() {
  EXPECT(runBytecodeProgram("class Parent { static make() { return \"parent\"; } }"
                            "class Child < Parent {}"
                            "Child.make();")
             .toString() == "parent");
}

void testBytecodeClassOverridesStaticMethod() {
  EXPECT(runBytecodeProgram("class Parent { static name() { return \"parent\"; } }"
                            "class Child < Parent { static name() { return \"child\"; } }"
                            "Child.name();")
             .toString() == "child");
}

void testBytecodeClassInstanceDoesNotSeeStaticMethod() {
  EXPECT(runBytecodeProgram("class Box { static make() { return 1; } }"
                            "Box().make;")
             .isUndefined());
}

void testBytecodeClassInstanceCannotCallStaticMethod() {
  try {
    runBytecodeProgram("class Box { static make() { return 1; } }"
                       "Box().make();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value has no method: make");
  }
}

void testBytecodeClassStaticMethodCannotUseThis() {
  try {
    runBytecodeProgram("class Box { static get() { return this.value; } }"
                       "Box.get();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: this outside method");
  }
}

void testBytecodeClassStaticMethodArity() {
  try {
    runBytecodeProgram("class Box { static make(value) { return value; } }"
                       "Box.make();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function make expects 1 arguments");
  }
}

void testBytecodeClassMethodThisField() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  set(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box();"
                            "b.set(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInstanceFieldsAreIndependent() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  set(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let a = Box();"
                            "let b = Box();"
                            "a.set(1);"
                            "b.set(2);"
                            "a.get() + b.get();")
             .asNumber() == 3);
}

void testBytecodeClassFieldCanShadowMethod() {
  EXPECT(runBytecodeProgram("class Box { get() { return \"method\"; } }"
                            "let b = Box();"
                            "b.get = \"field\";"
                            "b.get;")
             .toString() == "field");
}

void testBytecodeClassMethodVisibleAfterDeletingShadowField() {
  EXPECT(runBytecodeProgram("class Box { get() { return \"method\"; } }"
                            "let b = Box();"
                            "b.get = \"field\";"
                            "del(b, \"get\");"
                            "b.get();")
             .toString() == "method");
}

void testBytecodeClassFieldCanShadowInheritedMethod() {
  EXPECT(runBytecodeProgram("class Parent { get() { return \"parent\"; } }"
                            "class Child < Parent {}"
                            "let child = Child();"
                            "child.get = \"field\";"
                            "child.get;")
             .toString() == "field");
}

void testBytecodeClassInitInitializesInstance() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInitReturnValueIsIgnored() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; return 999; }"
                            "  get() { return this.value; }"
                            "}"
                            "let b = Box(42);"
                            "b.get();")
             .asNumber() == 42);
}

void testBytecodeClassInitReturnsInstance() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init() { return 999; }"
                            "  get() { return 123; }"
                            "}"
                            "Box().get();")
             .asNumber() == 123);
}

void testBytecodeClassInitInstancesAreIndependent() {
  EXPECT(runBytecodeProgram("class Box {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "let a = Box(1);"
                            "let b = Box(2);"
                            "a.get() + b.get();")
             .asNumber() == 3);
}

void testBytecodeClassMethodCallsAnotherMethod() {
  EXPECT(runBytecodeProgram("class Counter {"
                            "  init(value) { this.value = value; }"
                            "  inc() { this.value = this.value + 1; }"
                            "  next() { this.inc(); return this.value; }"
                            "}"
                            "let c = Counter(10);"
                            "c.next();")
             .asNumber() == 11);
}

void testBytecodeClassInheritsMethod() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal {}"
                            "Dog().speak();")
             .toString() == "animal");
}

void testBytecodeClassOverridesInheritedMethod() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal { speak() { return \"dog\"; } }"
                            "Dog().speak();")
             .toString() == "dog");
}

void testBytecodeClassInheritsInit() {
  EXPECT(runBytecodeProgram("class Parent {"
                            "  init(value) { this.value = value; }"
                            "  get() { return this.value; }"
                            "}"
                            "class Child < Parent {}"
                            "Child(123).get();")
             .asNumber() == 123);
}

void testBytecodeClassSuperclassMustBeClass() {
  try {
    runBytecodeProgram("let Parent = 123; class Child < Parent {}");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: superclass must be a class");
  }
}

void testBytecodeClassSuperMethodCall() {
  EXPECT(runBytecodeProgram("class Animal { speak() { return \"animal\"; } }"
                            "class Dog < Animal { speak() { return super.speak() + \" dog\"; } }"
                            "Dog().speak();")
             .toString() == "animal dog");
}

void testBytecodeClassSuperMethodUsesCurrentReceiver() {
  EXPECT(runBytecodeProgram("class Parent { value() { return this.name; } }"
                            "class Child < Parent { value() { return super.value() + \" child\"; } }"
                            "let c = Child();"
                            "c.name = \"receiver\";"
                            "c.value();")
             .toString() == "receiver child");
}

void testBytecodeClassSuperAcrossMultipleLevels() {
  EXPECT(runBytecodeProgram("class A { name() { return \"A\"; } }"
                            "class B < A { name() { return super.name() + \"B\"; } }"
                            "class C < B { name() { return super.name() + \"C\"; } }"
                            "C().name();")
             .toString() == "ABC");
}

void testBytecodeClassBoundMethodKeepsSuperBinding() {
  EXPECT(runBytecodeProgram("class A { name() { return \"A\"; } }"
                            "class B < A { name() { return super.name() + \"B\"; } }"
                            "let b = B();"
                            "let f = b.name;"
                            "f();")
             .toString() == "AB");
}

void testBytecodeClassSuperInitInitializesReceiver() {
  EXPECT(runBytecodeProgram("class A { init(value) { this.value = value; } }"
                            "class B < A {"
                            "  init(value) { super.init(value + 1); }"
                            "  get() { return this.value; }"
                            "}"
                            "B(41).get();")
             .asNumber() == 42);
}

void testBytecodeClassSuperMissingMethod() {
  try {
    runBytecodeProgram("class Animal {}"
                       "class Dog < Animal { speak() { return super.missing(); } }"
                       "Dog().speak();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: superclass has no method: missing");
  }
}

void testBytecodeClassSuperOutsideSubclassMethod() {
  try {
    runBytecodeProgram("class Box { get() { return super.get(); } } Box().get();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: super outside subclass method");
  }
}

void testBytecodeSuperOutsideMethod() {
  try {
    runBytecodeProgram("super.get();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: super outside subclass method");
  }
}

void testBytecodeThisOutsideMethod() {
  try {
    runBytecodeProgram("this.name;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: this outside method");
  }
}

void testBytecodeClassMethodUsesDefinitionClosure() {
  EXPECT(runBytecodeProgram("let x = \"global\";"
                            "class Box { get() { return x; } }"
                            "function test() {"
                            "  let x = \"local\";"
                            "  let b = Box();"
                            "  return b.get();"
                            "}"
                            "test();")
             .toString() == "global");
}

void testBytecodeClassBoundMethodUsesDefinitionClosure() {
  EXPECT(runBytecodeProgram("let x = \"global\";"
                            "class Box { get() { return x; } }"
                            "function test(fn) {"
                            "  let x = \"local\";"
                            "  return fn();"
                            "}"
                            "let b = Box();"
                            "test(b.get);")
             .toString() == "global");
}

void testBytecodeClassCallArity() {
  try {
    runBytecodeProgram("class Box {} Box(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: class Box expects 0 arguments");
  }
}

void testBytecodeClassInitArity() {
  try {
    runBytecodeProgram("class Box { init(value) { this.value = value; } } Box();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method init expects 1 arguments");
  }
}

void testBytecodeClassUnknownMethod() {
  try {
    runBytecodeProgram("class Box {} let b = Box(); b.missing();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value has no method: missing");
  }
}

void testBytecodeClassMissingFieldReturnsUndefined() {
  EXPECT(runBytecodeProgram("class Box {} let b = Box(); b.name;").isUndefined());
}

void testBytecodeClassMethodArity() {
  try {
    runBytecodeProgram("class Box { set(value) { this.value = value; } }"
                       "let b = Box();"
                       "b.set();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method set expects 1 arguments");
  }
}

void testBytecodeClassBoundMethodArity() {
  try {
    runBytecodeProgram("class Box { set(value) { this.value = value; } }"
                       "let b = Box();"
                       "let set = b.set;"
                       "set();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method set expects 1 arguments");
  }
}

void testBytecodeClosureKeepsOuterVariable() {
  EXPECT(runBytecodeProgram("function makeCounter() {"
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

void testBytecodeClosureInstancesAreIndependent() {
  EXPECT(runBytecodeProgram("function makeCounter() {"
                            "  let count = 0;"
                            "  function next() {"
                            "    count = count + 1;"
                            "    return count;"
                            "  }"
                            "  return next;"
                            "}"
                            "let a = makeCounter();"
                            "let b = makeCounter();"
                            "a();"
                            "a();"
                            "b();")
             .asNumber() == 1);
}

void testBytecodeClosuresShareCapturedVariable() {
  EXPECT(runBytecodeProgram("function makePair() {"
                            "  let x = 0;"
                            "  function add() {"
                            "    x = x + 1;"
                            "  }"
                            "  function get() {"
                            "    return x;"
                            "  }"
                            "  return [add, get];"
                            "}"
                            "let pair = makePair();"
                            "pair[0]();"
                            "pair[1]();")
             .asNumber() == 1);
}

void testBytecodeSharedUpvalueObservesMultipleWrites() {
  EXPECT(runBytecodeProgram("function makePair() {"
                            "  let x = 0;"
                            "  function add() {"
                            "    x = x + 1;"
                            "  }"
                            "  function get() {"
                            "    return x;"
                            "  }"
                            "  return [add, get];"
                            "}"
                            "let pair = makePair();"
                            "pair[0]();"
                            "pair[0]();"
                            "pair[1]();")
             .asNumber() == 2);
}

void testBytecodeClosureCapturesThroughMultipleLevels() {
  EXPECT(runBytecodeProgram("function outer() {"
                            "  let x = 10;"
                            "  function middle() {"
                            "    function inner() {"
                            "      return x;"
                            "    }"
                            "    return inner;"
                            "  }"
                            "  return middle;"
                            "}"
                            "let m = outer();"
                            "let i = m();"
                            "i();")
             .asNumber() == 10);
}

void testBytecodeClosureCapturesBlockLocal() {
  EXPECT(runBytecodeProgram("function make() {"
                            "  let get = undefined;"
                            "  {"
                            "    let x = 42;"
                            "    function inner() { return x; }"
                            "    get = inner;"
                            "  }"
                            "  return get;"
                            "}"
                            "let get = make();"
                            "get();")
             .asNumber() == 42);
}

void testBytecodeBreakClosesCapturedLocal() {
  EXPECT(runBytecodeProgram("let saved = undefined;"
                            "while (true) {"
                            "  {"
                            "    let x = 42;"
                            "    function get() { return x; }"
                            "    saved = get;"
                            "    break;"
                            "  }"
                            "}"
                            "saved();")
             .asNumber() == 42);
}

void testBytecodeContinueClosesCapturedLocal() {
  EXPECT(runBytecodeProgram("let saved = undefined;"
                            "let i = 0;"
                            "while (i < 1) {"
                            "  {"
                            "    let x = 99;"
                            "    function get() { return x; }"
                            "    saved = get;"
                            "    i = i + 1;"
                            "    continue;"
                            "  }"
                            "}"
                            "saved();")
             .asNumber() == 99);
}

void testCompileBytecodeFunctionWithoutReturn() {
  EXPECT(runBytecodeProgram("function noReturn() {"
                            "  let x = 1;"
                            "}"
                            "noReturn();")
             .isUndefined());
}

void testBytecodeFunctionArityMismatch() {
  try {
    runBytecodeProgram(
        "function add(a, b) {"
        "  return a + b;"
        "}"
        "add(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function add expects 2 arguments");
  }
}

void testCompileBytecodeFunctionLocalVariable() {
  EXPECT(runBytecodeProgram("function addOne(x) {"
                            "  let y = x + 1;"
                            "  return y;"
                            "}"
                            "addOne(41);")
             .asNumber() == 42);
}

void testCompileBytecodeRecursiveFunction() {
  EXPECT(runBytecodeProgram("function fact(n) {"
                            "  if (n <= 1) {"
                            "    return 1;"
                            "  }"
                            "  return n * fact(n - 1);"
                            "}"
                            "fact(5);")
             .asNumber() == 120);
}

void testCompileBytecodeRecursiveFibonacci() {
  EXPECT(runBytecodeProgram("function fib(n) {"
                            "  if (n <= 1) {"
                            "    return n;"
                            "  }"
                            "  return fib(n - 1) + fib(n - 2);"
                            "}"
                            "fib(6);")
             .asNumber() == 8);
}

void testBytecodeReturnedLocalFunctionCanRecurse() {
  EXPECT(runBytecodeProgram("function makeFact() {"
                            "  function fact(n) {"
                            "    if (n <= 1) {"
                            "      return 1;"
                            "    }"
                            "    return n * fact(n - 1);"
                            "  }"
                            "  return fact;"
                            "}"
                            "let fact = makeFact();"
                            "fact(5);")
             .asNumber() == 120);
}

void testBytecodeReturnOutsideFunction() {
  try {
    runBytecodeProgram("return 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testBytecodeReturnInsideTopLevelBlock() {
  try {
    runBytecodeProgram("{ return 1; }");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: return outside function");
  }
}

void testBytecodeCallNonFunction() {
  try {
    runBytecodeProgram(
        "let x = 1;"
        "x();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not callable");
  }
}

void testBytecodePrintBuiltin() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = runBytecodeProgram("print(1 + 2);");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "3\n");
  EXPECT(result.isNull());
}

void testBytecodeBuiltinFunctionCanBeAssigned() {
  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

  const minijs::Value result = runBytecodeProgram(
      "let p = print;"
      "p(\"hello\");");

  std::cout.rdbuf(previous);
  EXPECT(output.str() == "hello\n");
  EXPECT(result.isNull());
}

void testBytecodeGcKeepsBuiltinNativeFunction() {
  minijs::VM vm;
  EXPECT(vm.run(minijs::Compiler().compileProgram(minijs::Parser("print;").parseProgram()))
             .isNativeFunction());
  EXPECT(vm.objectCount() == 0);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);

  std::ostringstream output;
  std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
  EXPECT(vm.run(minijs::Compiler().compileProgram(minijs::Parser("print(\"hello\");").parseProgram()))
             .isNull());
  std::cout.rdbuf(previous);
  EXPECT(output.str() == "hello\n");
}

void testBytecodePrintArity() {
  try {
    runBytecodeProgram("print();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: print expects 1 argument");
  }
}

void testBytecodeClockBuiltin() { EXPECT(runBytecodeProgram("clock();").asNumber() == 0); }

void testBytecodeClockArity() {
  try {
    runBytecodeProgram("clock(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: clock expects 0 arguments");
  }
}

void testBytecodeLenArrayBuiltin() {
  EXPECT(runBytecodeProgram("len([1, 2, 3]);").asNumber() == 3);
}

void testBytecodeLenStringBuiltin() { EXPECT(runBytecodeProgram("len(\"Tom\");").asNumber() == 3); }

void testBytecodeLenObjectBuiltin() {
  EXPECT(runBytecodeProgram("len({ name: \"Tom\", age: 18 });").asNumber() == 2);
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "len(p);")
             .asNumber() == 1);
}

void testBytecodeLenInstanceBuiltin() {
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.x = 1;"
                            "b.y = 2;"
                            "len(b);")
             .asNumber() == 2);
}

void testBytecodeLenArity() {
  try {
    runBytecodeProgram("len();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: len expects 1 argument");
  }
}

void testBytecodeLenTypeError() {
  try {
    runBytecodeProgram("len(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: len expects string, array, object, or instance");
  }
}

void testBytecodeTypeOfPrimitiveBuiltins() {
  EXPECT(runBytecodeProgram("typeOf(1);").toString() == "number");
  EXPECT(runBytecodeProgram("typeOf(true);").toString() == "boolean");
  EXPECT(runBytecodeProgram("typeOf(null);").toString() == "null");
  EXPECT(runBytecodeProgram("typeOf(undefined);").toString() == "undefined");
  EXPECT(runBytecodeProgram("typeOf(\"Tom\");").toString() == "string");
}

void testBytecodeTypeOfContainers() {
  EXPECT(runBytecodeProgram("typeOf([1, 2]);").toString() == "array");
  EXPECT(runBytecodeProgram("typeOf({ age: 18 });").toString() == "object");
  EXPECT(runBytecodeProgram("class Box {} typeOf(Box);").toString() == "class");
  EXPECT(runBytecodeProgram("class Box {} typeOf(Box());").toString() == "instance");
}

void testBytecodeTypeOfFunctions() {
  EXPECT(runBytecodeProgram("typeOf(print);").toString() == "builtin");
  EXPECT(runBytecodeProgram("function add(a, b) {"
                            "  return a + b;"
                            "}"
                            "typeOf(add);")
             .toString() == "function");
}

void testBytecodeTypeOfArity() {
  try {
    runBytecodeProgram("typeOf();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: typeOf expects 1 argument");
  }
}

void testBytecodeHasBuiltinObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "has(p, \"name\");")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "has(p, \"score\");")
             .toString() == "false");
}

void testBytecodeHasBuiltinInstanceField() {
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.name = \"Tom\";"
                            "has(b, \"name\");")
             .toString() == "true");
  EXPECT(runBytecodeProgram("class Box { get() { return 1; } }"
                            "let b = Box();"
                            "has(b, \"get\");")
             .toString() == "false");
}

void testBytecodeHasBuiltinArrayAndStringProperty() {
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"length\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"push\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"pop\");").toString() == "true");
  EXPECT(runBytecodeProgram("has([1, 2, 3], \"0\");").toString() == "false");
  EXPECT(runBytecodeProgram("has(\"Tom\", \"length\");").toString() == "true");
  EXPECT(runBytecodeProgram("has(1, \"length\");").toString() == "false");
}

void testBytecodeHasArity() {
  try {
    runBytecodeProgram("has({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has expects 2 arguments");
  }
}

void testBytecodeHasKeyMustBeString() {
  try {
    runBytecodeProgram("has({}, 1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: has key must be a string");
  }
}

void testBytecodeDelBuiltinObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "has(p, \"age\");")
             .toString() == "false");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");")
             .toString() == "true");
}

void testBytecodeDelBuiltinInstanceField() {
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.name = \"Tom\";"
                            "del(b, \"name\");")
             .toString() == "true");
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.name = \"Tom\";"
                            "del(b, \"name\");"
                            "has(b, \"name\");")
             .toString() == "false");
  EXPECT(runBytecodeProgram("class Box { get() { return 1; } }"
                            "let b = Box();"
                            "del(b, \"get\");")
             .toString() == "false");
}

void testBytecodeDelBuiltinMissingOrNonObject() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "del(p, \"age\");")
             .toString() == "false");
  EXPECT(runBytecodeProgram("del(123, \"age\");").toString() == "false");
}

void testBytecodeDelArity() {
  try {
    runBytecodeProgram("del({});");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del expects 2 arguments");
  }
}

void testBytecodeDelKeyMustBeString() {
  try {
    runBytecodeProgram("del({ name: \"Tom\" }, 123);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: del key must be a string");
  }
}

void testBytecodeKeysBuiltinObject() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "len(keys(p));")
             .asNumber() == 2);

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "has(p, ks[0]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "has(p, ks[1]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "let ks = keys(p);"
                            "ks[0] != ks[1];")
             .toString() == "true");
}

void testBytecodeGcCollectsTemporaryKeyStringsAfterKeysBuiltin() {
  minijs::Parser parser("let p = { name: \"Tom\", age: 18 };"
                        "keys(p);"
                        "p = undefined;"
                        "undefined;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isUndefined());

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 0);
}

void testBytecodeKeysBuiltinInstanceFields() {
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.name = \"Tom\";"
                            "b.age = 18;"
                            "len(keys(b));")
             .asNumber() == 2);
  EXPECT(runBytecodeProgram("class Box {}"
                            "let b = Box();"
                            "b.name = \"Tom\";"
                            "let ks = keys(b);"
                            "len(ks) == 1 && has(b, ks[0]);")
             .toString() == "true");
}

void testBytecodeKeysBuiltinArrayAndString() {
  EXPECT(runBytecodeProgram("len(keys([1, 2]));").asNumber() == 3);
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[0]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[1]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("let ks = keys([1, 2]);"
                            "has([1, 2], ks[2]);")
             .toString() == "true");
  EXPECT(runBytecodeProgram("len(keys(\"Tom\"));").asNumber() == 1);
  EXPECT(runBytecodeProgram("len(keys(1));").asNumber() == 0);
}

void testBytecodeKeysArity() {
  try {
    runBytecodeProgram("keys();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: keys expects 1 argument");
  }
}

void testBytecodeUndefinedGlobal() {
  try {
    runBytecodeProgram("x;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testBytecodeAssignUndefinedGlobal() {
  try {
    runBytecodeProgram("x = 1;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: undefined variable: x");
  }
}

void testBytecodeProgramWithoutFinalExpressionReturnsNull() {
  EXPECT(runBytecodeProgram("let x = 10;").isNull());
}

void testDisassembleGlobalLetExpression() {
  const minijs::Chunk chunk = compileProgram("let x = 10; x + 20;");

  const std::string expected =
      "0000 OP_CONSTANT 0 10\n"
      "0002 OP_DEFINE_GLOBAL 1 x\n"
      "0004 OP_GET_GLOBAL 2 x\n"
      "0006 OP_CONSTANT 3 20\n"
      "0008 OP_ADD\n"
      "0009 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleGlobalAssignment() {
  const minijs::Chunk chunk = compileProgram("let x = 1; x = x + 2; x;");

  const std::string expected =
      "0000 OP_CONSTANT 0 1\n"
      "0002 OP_DEFINE_GLOBAL 1 x\n"
      "0004 OP_GET_GLOBAL 2 x\n"
      "0006 OP_CONSTANT 3 2\n"
      "0008 OP_ADD\n"
      "0009 OP_SET_GLOBAL 4 x\n"
      "0011 OP_POP\n"
      "0012 OP_GET_GLOBAL 5 x\n"
      "0014 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testBytecodeDivisionByZero() {
  try {
    runBytecode("10 / 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testBytecodeModuloByZero() {
  try {
    runBytecode("10 % 0;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: modulo by zero");
  }
}

void testCompileArrayLiteralIndex() { EXPECT(runBytecode("[1, 2, 3][1];").asNumber() == 2); }

void testCompileArrayVariableIndex() {
  EXPECT(runBytecodeProgram("let a = [10, 20, 30];"
                            "a[2];")
             .asNumber() == 30);
}

void testCompileArrayExpressionElements() {
  EXPECT(runBytecodeProgram("let x = 10;"
                            "[x, x + 1][1];")
             .asNumber() == 11);
}

void testCompileArrayIndexAssignment() {
  EXPECT(runBytecodeProgram("let a = [10, 20];"
                            "a[1] = 99;"
                            "a[1];")
             .asNumber() == 99);
}

void testCompileArrayReferenceSemantics() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "let b = a;"
                            "b[0] = 99;"
                            "a[0];")
             .asNumber() == 99);
}

void testBytecodeArrayIndexOutOfBounds() {
  try {
    runBytecode("[1][1];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: array index out of bounds");
  }
}

void testBytecodeArrayIndexMustBeInteger() {
  try {
    runBytecode("[1][0.5];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: array index must be a non-negative integer");
  }
}

void testBytecodeIndexNonArrayValue() {
  try {
    runBytecode("1[0];");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an array");
  }
}

void testCompileObjectLiteralProperty() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age;")
             .asNumber() == 18);
}

void testCompileObjectExpressionProperty() {
  EXPECT(runBytecodeProgram("let age = 18;"
                            "let p = { name: \"Tom\", age: age + 1 };"
                            "p.age;")
             .asNumber() == 19);
}

void testCompileMissingObjectProperty() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "p.name;")
             .isUndefined());
}

void testBytecodeGetPropertyFromNonObject() {
  try {
    runBytecode("1.age;");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not an object");
  }
}

void testCompileObjectPropertyAssignment() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "p.age = 20;"
                            "p.age;")
             .asNumber() == 20);
}

void testBytecodeObjectPropertyHelpersPreserveBehavior() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age = p.age + 1;"
                            "p.city = \"Shanghai\";"
                            "has(p, \"name\") && has(p, \"city\") && p.age == 19 && len(p) == 3;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "p.age = 20;"
                            "has(p, \"age\") && p.age == 20 && len(p) == 2;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" }; p.missing;").isUndefined());
}

void testBytecodeShapeModeObjectPropertiesPreserveSemantics() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age = 19;"
                            "p.city = \"Shanghai\";"
                            "p.name == \"Tom\" && p.age == 19 && p.city == \"Shanghai\" && "
                            "has(p, \"name\") && has(p, \"city\") && len(p) == 3;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "p.name = \"Jerry\";"
                            "p.name == \"Jerry\" && len(p) == 1;")
             .toString() == "true");
}

void testBytecodeGetPropertyInlineCacheHitsRepeatedSameShapeAccess() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\" };"
                             "let i = 0;"
                             "let value = \"\";"
                             "while (i < 3) {"
                             "  value = p.name;"
                             "  i = i + 1;"
                             "}"
                             "value;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.toString() == "Tom");
  EXPECT(stats.misses == 1);
  EXPECT(stats.updates == 1);
  EXPECT(stats.hits >= 2);
  EXPECT(stats.bypasses == 0);
}

void testBytecodeGetPropertyInlineCacheMissesAndUpdatesOnShapeChange() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\" };"
                             "let i = 0;"
                             "let value = \"\";"
                             "while (i < 3) {"
                             "  value = p.name;"
                             "  if (i == 0) {"
                             "    p = { name: \"Jerry\", age: 18 };"
                             "  }"
                             "  i = i + 1;"
                             "}"
                             "value;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.toString() == "Jerry");
  EXPECT(stats.misses == 2);
  EXPECT(stats.updates == 2);
  EXPECT(stats.hits == 1);
  EXPECT(stats.bypasses == 0);
}

void testBytecodeGetPropertyInlineCacheCachesMultipleShapesAtSameAccessSite() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "function read(object) { return object.name; }"
                             "let first = { name: \"A\", extra: 1 };"
                             "let second = { extra: 2, name: \"B\" };"
                             "read(first) + read(second) + read(first) + read(second);");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.toString() == "ABAB");
  EXPECT(stats.misses == 2);
  EXPECT(stats.updates == 2);
  EXPECT(stats.hits == 2);
  EXPECT(stats.bypasses == 0);
}

void testBytecodeGetPropertyInlineCacheBecomesMegamorphicAfterCapacity() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm, R"(
        function read(object) {
          return object.name;
        }

        let a = { name: 1 };
        let b = { x: 0, name: 2 };
        let c = { x: 0, y: 0, name: 3 };
        let d = { x: 0, y: 0, z: 0, name: 4 };
        let e = { x: 0, y: 0, z: 0, w: 0, name: 5 };

        read(a) + read(b) + read(c) + read(d) + read(a) + read(e) + read(e);
      )");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 21);
  EXPECT(stats.misses == 5);
  EXPECT(stats.updates == 4);
  EXPECT(stats.hits == 1);
  EXPECT(stats.bypasses == 1);
}

void testBytecodeGetPropertyInlineCacheBypassesDictionaryMode() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\", age: 18 };"
                             "del(p, \"age\");"
                             "let i = 0;"
                             "let value = \"\";"
                             "while (i < 3) {"
                             "  value = p.name;"
                             "  i = i + 1;"
                             "}"
                             "value;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.toString() == "Tom");
  EXPECT(stats.misses == 0);
  EXPECT(stats.updates == 0);
  EXPECT(stats.hits == 0);
  EXPECT(stats.bypasses == 3);
}

void testBytecodeGetPropertyInlineCacheDoesNotUpdateMissingProperty() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\" };"
                             "let i = 0;"
                             "let value = null;"
                             "while (i < 3) {"
                             "  value = p.missing;"
                             "  i = i + 1;"
                             "}"
                             "value;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.isUndefined());
  EXPECT(stats.misses == 3);
  EXPECT(stats.updates == 0);
  EXPECT(stats.hits == 0);
  EXPECT(stats.bypasses == 0);
}

void testBytecodeSetPropertyInlineCacheHitsRepeatedExistingPropertyAssignment() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\" };"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  p.name = i;"
                             "  i = i + 1;"
                             "}"
                             "p.name;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 2);
  EXPECT(stats.setMisses == 1);
  EXPECT(stats.setUpdates == 1);
  EXPECT(stats.setHits == 2);
  EXPECT(stats.setBypasses == 0);
}

void testBytecodeSetPropertyInlineCacheUpdatesAfterAddingProperty() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = {};"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  p.name = i;"
                             "  i = i + 1;"
                             "}"
                             "p.name;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 2);
  EXPECT(stats.setMisses == 1);
  EXPECT(stats.setUpdates == 1);
  EXPECT(stats.setHits == 2);
  EXPECT(stats.setBypasses == 0);
}

void testBytecodeSetPropertyInlineCacheMissesAndUpdatesOnShapeChange() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let a = { name: \"Tom\" };"
                             "let b = { age: 18, name: \"Ada\" };"
                             "let current = a;"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  current.name = i;"
                             "  if (i == 0) {"
                             "    current = b;"
                             "  }"
                             "  i = i + 1;"
                             "}"
                             "b.name;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 2);
  EXPECT(stats.setMisses == 2);
  EXPECT(stats.setUpdates == 2);
  EXPECT(stats.setHits == 1);
  EXPECT(stats.setBypasses == 0);
}

void testBytecodeSetPropertyInlineCacheCachesMultipleShapesAtSameAccessSite() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "function write(object, value) { object.name = value; }"
                             "let first = { name: 0, extra: 1 };"
                             "let second = { extra: 0, name: 0 };"
                             "write(first, 1);"
                             "write(second, 2);"
                             "write(first, 3);"
                             "write(second, 4);"
                             "first.name + second.name;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 7);
  EXPECT(stats.setMisses == 2);
  EXPECT(stats.setUpdates == 2);
  EXPECT(stats.setHits == 2);
  EXPECT(stats.setBypasses == 0);
}

void testBytecodeSetPropertyInlineCacheBecomesMegamorphicAfterCapacity() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm, R"(
        function write(object, value) {
          object.name = value;
          return value;
        }

        let a = { name: 0 };
        let b = { x: 0, name: 0 };
        let c = { x: 0, y: 0, name: 0 };
        let d = { x: 0, y: 0, z: 0, name: 0 };
        let e = { x: 0, y: 0, z: 0, w: 0, name: 0 };

        write(a, 1) + write(b, 2) + write(c, 3) + write(d, 4) + write(a, 5) +
            write(e, 6) + write(e, 7);
      )");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 28);
  EXPECT(stats.setMisses == 5);
  EXPECT(stats.setUpdates == 4);
  EXPECT(stats.setHits == 1);
  EXPECT(stats.setBypasses == 1);
}

void testBytecodePolymorphicPropertyInlineCacheSurvivesManualGc() {
  minijs::VM vm;
  const minijs::Value first = runBytecodeProgramOnVm(vm, R"(
    function read(object) {
      return object.name;
    }

    function write(object, value) {
      object.name = value;
      return value;
    }

    let first = { name: 1, extra: 10 };
    let second = { extra: 20, name: 2 };

    read(first);
    read(second);
    write(first, 3);
    write(second, 4);
  )");
  EXPECT(first.asNumber() == 4);

  vm.collectGarbage();
  vm.debugResetPropertyInlineCacheStats();

  const minijs::Value second = runBytecodeProgramOnVm(
      vm, "read(first) + read(second); write(first, 5) + write(second, 6);");
  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(second.asNumber() == 11);
  EXPECT(stats.hits == 2);
  EXPECT(stats.misses == 0);
  EXPECT(stats.updates == 0);
  EXPECT(stats.setHits == 2);
  EXPECT(stats.setMisses == 0);
  EXPECT(stats.setUpdates == 0);
  EXPECT(stats.bypasses == 0);
  EXPECT(stats.setBypasses == 0);
}

void testBytecodeSetPropertyInlineCacheBypassesDictionaryMode() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "let p = { name: \"Tom\", age: 18 };"
                             "del(p, \"age\");"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  p.name = i;"
                             "  i = i + 1;"
                             "}"
                             "p.name;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 2);
  EXPECT(stats.setMisses == 0);
  EXPECT(stats.setUpdates == 0);
  EXPECT(stats.setHits == 0);
  EXPECT(stats.setBypasses == 3);
}

void testBytecodeShapeSetKeepsAssignedValuesDuringGcPressure() {
  EXPECT(runBytecodeProgram("let p = {};"
                            "p.name = \"Tom\";"
                            "p.a0 = \"a0\";"
                            "p.a1 = \"a1\";"
                            "p.a2 = \"a2\";"
                            "p.a3 = \"a3\";"
                            "p.a4 = \"a4\";"
                            "p.a5 = \"a5\";"
                            "p.a6 = \"a6\";"
                            "p.a7 = \"a7\";"
                            "p.name;")
             .toString() == "Tom");
}

void testBytecodeObjectDeleteFallsBackToDictionarySemantics() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "p.city = \"Shanghai\";"
                            "p.name == \"Tom\" && p.city == \"Shanghai\" && "
                            "!has(p, \"age\") && len(p) == 2;")
             .toString() == "true");
}

void testBytecodeObjectsWithSamePropertyOrderShareShape() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "a.y = 2;"
                         "let b = {};"
                         "b.x = 3;"
                         "b.y = 4;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") == vm.debugGlobalObjectShape("b"));
}

void testBytecodeObjectsWithDifferentPropertyOrderUseDifferentShapes() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "a.y = 2;"
                         "let b = {};"
                         "b.y = 3;"
                         "b.x = 4;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("b") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") != vm.debugGlobalObjectShape("b"));
}

void testBytecodeReassigningExistingPropertyKeepsShape() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "let b = {};"
                         "b.x = 2;"
                         "a.x = 3;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") == vm.debugGlobalObjectShape("b"));
}

void testBytecodeDeletingPropertySwitchesObjectToDictionaryMode() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let p = { name: \"Tom\", age: 18 };"
                         "del(p, \"age\");"
                         "true;");

  EXPECT(vm.debugGlobalObjectUsesDictionary("p"));
}

void testCompileObjectReferenceSemantics() {
  EXPECT(runBytecodeProgram("let p = { age: 18 };"
                            "let q = p;"
                            "q.age = 20;"
                            "p.age;")
             .asNumber() == 20);
}

void testBytecodeArrayLengthProperty() {
  EXPECT(runBytecodeProgram("[1, 2, 3].length;").asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [1, 2, 3];"
                            "a.length;")
             .asNumber() == 3);
}

void testBytecodeStringLengthProperty() {
  EXPECT(runBytecodeProgram("\"Tom\".length;").asNumber() == 3);
  EXPECT(runBytecodeProgram("let s = \"hello\";"
                            "s.length;")
             .asNumber() == 5);
}

void testBytecodeObjectOwnLengthProperty() {
  EXPECT(runBytecodeProgram("let p = { length: 99 };"
                            "p.length;")
             .asNumber() == 99);
}

void testBytecodeArrayUnknownProperty() {
  EXPECT(runBytecodeProgram("[1, 2].unknown;").isUndefined());
}

void testBytecodeArrayPushMethod() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.push(3);"
                            "a.length;")
             .asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(1);"
                            "a.push(2);"
                            "a[0] + a[1];")
             .asNumber() == 3);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(10);")
             .asNumber() == 1);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.push(1 + 2);"
                            "a[0];")
             .asNumber() == 3);
}

void testBytecodeArrayPopMethod() {
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.pop();")
             .asNumber() == 2);
  EXPECT(runBytecodeProgram("let a = [1, 2];"
                            "a.pop();"
                            "a.length;")
             .asNumber() == 1);
  EXPECT(runBytecodeProgram("let a = [];"
                            "a.pop();")
             .isUndefined());
}

void testBytecodeArrayPushArity() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.push();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: push expects 1 argument");
  }
}

void testBytecodeArrayPopArity() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.pop(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: pop expects 0 arguments");
  }
}

void testBytecodeMethodCallOnNonArray() {
  try {
    runBytecodeProgram("\"Tom\".push(1);");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) ==
           "RuntimeError: method call receiver is not an array");
  }
}

void testBytecodeUnknownArrayMethod() {
  try {
    runBytecodeProgram("let a = [];"
                       "a.shift();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: unknown array method: shift");
  }
}

void testBytecodeMethodCallStatsCountsClassDispatches() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "class Box {"
                             "  static make(value) { return value + 1; }"
                             "  init(value) { this.value = value; }"
                             "  get() { return this.value; }"
                             "}"
                             "Box.make(1) + Box(2).get();");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 4);
  EXPECT(stats.methodCallInstanceDispatches == 1);
  EXPECT(stats.methodCallStaticDispatches == 1);
  EXPECT(stats.methodCallArrayPushes == 0);
  EXPECT(stats.methodCallArrayPops == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallMisses == 2);
  EXPECT(stats.methodCallUpdates == 2);
  EXPECT(stats.methodCallBypasses == 0);
}

void testBytecodeMethodCallStatsCountsArrayBuiltins() {
  minijs::VM vm;
  const minijs::Value result = runBytecodeProgramOnVm(vm,
                                                     "let a = [];"
                                                     "a.push(1);"
                                                     "a.push(2);"
                                                     "a.pop();");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 2);
  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallArrayPushes == 2);
  EXPECT(stats.methodCallArrayPops == 1);
  EXPECT(stats.methodCallDispatchErrors == 0);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallMisses == 0);
  EXPECT(stats.methodCallUpdates == 0);
  EXPECT(stats.methodCallBypasses == 3);
}

void testBytecodeMethodCallStatsCountsDispatchErrors() {
  minijs::VM vm;

  try {
    runBytecodeProgramOnVm(vm,
                           "let a = [];"
                           "a.shift();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: unknown array method: shift");
  }

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallArrayPushes == 0);
  EXPECT(stats.methodCallArrayPops == 0);
  EXPECT(stats.methodCallDispatchErrors == 1);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallMisses == 0);
  EXPECT(stats.methodCallUpdates == 0);
  EXPECT(stats.methodCallBypasses == 1);
}

void testBytecodeMethodCallStatsCountsInstanceArityErrors() {
  minijs::VM vm;

  try {
    runBytecodeProgramOnVm(vm,
                           "class Box { set(value) { this.value = value; } }"
                           "let box = Box();"
                           "box.set();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: method set expects 1 arguments");
  }

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallMisses == 1);
  EXPECT(stats.methodCallUpdates == 1);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 1);
}

void testBytecodeMethodCallStatsCountsStaticArityErrors() {
  minijs::VM vm;

  try {
    runBytecodeProgramOnVm(vm,
                           "class Box { static make(value) { return value; } }"
                           "Box.make();");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: function make expects 1 arguments");
  }

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallMisses == 1);
  EXPECT(stats.methodCallUpdates == 1);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 1);
}

void testBytecodeMethodCallInlineCacheHitsRepeatedInstanceMethod() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "class Box {"
                             "  init(value) { this.value = value; }"
                             "  get() { return this.value; }"
                             "}"
                             "let box = Box(7);"
                             "let total = 0;"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  total = total + box.get();"
                             "  i = i + 1;"
                             "}"
                             "total;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 21);
  EXPECT(stats.methodCallInstanceDispatches == 3);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallHits == 2);
  EXPECT(stats.methodCallMisses == 1);
  EXPECT(stats.methodCallUpdates == 1);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodeMethodCallInlineCacheHitsRepeatedStaticMethod() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "class Box {"
                             "  static make(value) { return value + 1; }"
                             "}"
                             "let total = 0;"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  total = total + Box.make(i);"
                             "  i = i + 1;"
                             "}"
                             "total;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 6);
  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 3);
  EXPECT(stats.methodCallHits == 2);
  EXPECT(stats.methodCallMisses == 1);
  EXPECT(stats.methodCallUpdates == 1);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodeMethodCallInlineCacheHitSurvivesManualGc() {
  minijs::VM vm;
  const minijs::Value first = runBytecodeProgramOnVm(vm,
                                                    "function invoke(receiver) {"
                                                    "  return receiver.get();"
                                                    "}"
                                                    "function makeBox() {"
                                                    "  let offset = 1;"
                                                    "  class Box {"
                                                    "    init(value) { this.value = value; }"
                                                    "    get() { return this.value + offset; }"
                                                    "  }"
                                                    "  return Box;"
                                                    "}"
                                                    "let Box = makeBox();"
                                                    "let box = Box(8);"
                                                    "Box = undefined;"
                                                    "invoke(box);");
  EXPECT(first.asNumber() == 9);

  vm.collectGarbage();
  vm.debugResetPropertyInlineCacheStats();

  const minijs::Value second = runBytecodeProgramOnVm(vm, "invoke(box);");
  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(second.asNumber() == 9);
  EXPECT(stats.methodCallInstanceDispatches == 1);
  EXPECT(stats.methodCallHits == 1);
  EXPECT(stats.methodCallMisses == 0);
  EXPECT(stats.methodCallUpdates == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodePolymorphicMethodCallInlineCacheSurvivesManualGc() {
  minijs::VM vm;
  const minijs::Value first = runBytecodeProgramOnVm(vm, R"(
    function invoke(receiver) {
      return receiver.get();
    }

    function makeA() {
      let offset = 1;
      class A { get() { return offset; } }
      return A;
    }

    function makeB() {
      let offset = 10;
      class B { get() { return offset; } }
      return B;
    }

    let A = makeA();
    let B = makeB();
    let first = A();
    let second = B();
    A = undefined;
    B = undefined;
    invoke(first) + invoke(second);
  )");
  EXPECT(first.asNumber() == 11);

  vm.collectGarbage();
  vm.debugResetPropertyInlineCacheStats();

  const minijs::Value second = runBytecodeProgramOnVm(vm, "invoke(first) + invoke(second);");
  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(second.asNumber() == 11);
  EXPECT(stats.methodCallInstanceDispatches == 2);
  EXPECT(stats.methodCallHits == 2);
  EXPECT(stats.methodCallMisses == 0);
  EXPECT(stats.methodCallUpdates == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodeMethodCallInlineCacheMissesAndUpdatesOnClassChange() {
  minijs::VM vm;
  const minijs::Value result =
      runBytecodeProgramOnVm(vm,
                             "class A { value() { return 1; } }"
                             "class B { value() { return 10; } }"
                             "let current = A();"
                             "let total = 0;"
                             "let i = 0;"
                             "while (i < 3) {"
                             "  total = total + current.value();"
                             "  if (i == 0) {"
                             "    current = B();"
                             "  }"
                             "  i = i + 1;"
                             "}"
                             "total;");

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();

  EXPECT(result.asNumber() == 21);
  EXPECT(stats.methodCallInstanceDispatches == 3);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallHits == 1);
  EXPECT(stats.methodCallMisses == 2);
  EXPECT(stats.methodCallUpdates == 2);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testDisassembleArrayMethodCall() {
  const minijs::Chunk chunk = compileProgram("let a = [];"
                                             "a.push(1);");

  const std::string expected =
      "0000 OP_ARRAY 0\n"
      "0002 OP_DEFINE_GLOBAL 0 a\n"
      "0004 OP_GET_GLOBAL 1 a\n"
      "0006 OP_CONSTANT 2 1\n"
      "0008 OP_METHOD_CALL 3 push 1 feedback=0\n"
      "0012 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testDisassembleObjectLiteralProperty() {
  const minijs::Chunk chunk = compileProgram(
      "let p = { name: \"Tom\", age: 18 };"
      "p.age;");

  const std::string expected =
      "0000 OP_CONSTANT 0 Tom\n"
      "0002 OP_CONSTANT 1 18\n"
      "0004 OP_OBJECT 2 [name, age]\n"
      "0006 OP_DEFINE_GLOBAL 3 p\n"
      "0008 OP_GET_GLOBAL 4 p\n"
      "0010 OP_GET_PROPERTY 5 age feedback=0\n"
      "0013 OP_RETURN\n";

  EXPECT(minijs::disassembleChunk(chunk) == expected);
}

void testBytecodeMethodCallInlineCacheCachesMultipleReceiverClassesAtSameCallSite() {
  minijs::VM vm;
  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class A {
      get() { return 1; }
    }

    class B {
      get() { return 2; }
    }

    function read(box) {
      return box.get();
    }

    read(A()) + read(B()) + read(A()) + read(B());
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 6);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.methodCallMisses == 2);
  EXPECT(stats.methodCallUpdates == 2);
  EXPECT(stats.methodCallHits == 2);
  EXPECT(stats.methodCallInstanceDispatches == 4);
}

void testBytecodeMethodCallInlineCacheBecomesMegamorphicAfterCapacity() {
  minijs::VM vm;
  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class A { get() { return 1; } }
    class B { get() { return 2; } }
    class C { get() { return 3; } }
    class D { get() { return 4; } }
    class E { get() { return 5; } }

    function read(value) {
      return value.get();
    }

    read(A()) + read(B()) + read(C()) + read(D()) + read(A()) + read(E()) + read(E());
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 21);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.methodCallMisses == 5);
  EXPECT(stats.methodCallUpdates == 4);
  EXPECT(stats.methodCallHits == 1);
  EXPECT(stats.methodCallInstanceDispatches == 7);
  EXPECT(stats.methodCallBypasses == 1);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodeMethodCallInlineCacheCachesMultipleStaticClassesAtSameCallSite() {
  minijs::VM vm;
  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Alpha {
      static make(value) { return value + 1; }
    }

    class Beta {
      static make(value) { return value + 10; }
    }

    function read(type) {
      return type.make(1);
    }

    read(Alpha) + read(Beta) + read(Alpha) + read(Beta);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 26);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.methodCallMisses == 2);
  EXPECT(stats.methodCallUpdates == 2);
  EXPECT(stats.methodCallHits == 2);
  EXPECT(stats.methodCallInstanceDispatches == 0);
  EXPECT(stats.methodCallStaticDispatches == 4);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testBytecodeSuperCallDoesNotPolluteMethodInlineCache() {
  minijs::VM vm;
  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Parent {
      value() { return 10; }
    }

    class Child < Parent {
      value() { return super.value() + 1; }
    }

    Child().value();
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 11);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.methodCallMisses == 1);
  EXPECT(stats.methodCallUpdates == 1);
  EXPECT(stats.methodCallHits == 0);
  EXPECT(stats.methodCallInstanceDispatches == 1);
  EXPECT(stats.methodCallStaticDispatches == 0);
  EXPECT(stats.methodCallBypasses == 0);
  EXPECT(stats.methodCallDispatchErrors == 0);
}

void testJitCallThresholdCompilesSupportedFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(3);

  runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    addOne(1);
    addOne(2);
    addOne(3);
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("addOne");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 3);
  EXPECT(feedback->backedgeCount == 0);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("addOne");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
  EXPECT(code->executableMemory != nullptr);
  EXPECT(!code->bytecodeOffsetToNativeOffset.empty());
  EXPECT(vm.debugGlobalFunctionJitCompileError("addOne").empty());
}

void testJitCallCountStaysColdBelowThreshold() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(3);

  runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    addOne(1);
    addOne(2);
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("addOne");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Cold);
}

void testJitBackedgeThresholdCompilesSupportedFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(100);
  vm.setJitBackedgeThreshold(5);

  runBytecodeProgramOnVm(vm, R"(
    function loop() {
      let i = 0;
      while (i < 5) { i = i + 1; }
    }
    loop();
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("loop");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 1);
  EXPECT(feedback->backedgeCount == 5);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("loop") != nullptr);
}

void testJitDisabledDoesNotCollectHeat() {
  minijs::VM vm;
  vm.setJitCallThreshold(1);
  vm.setJitBackedgeThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function loop() {
      let i = 0;
      while (i < 3) { i = i + 1; }
    }
    loop();
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("loop");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 0);
  EXPECT(feedback->backedgeCount == 0);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Cold);
}

void testJitArityErrorDoesNotCountCall() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, "function one(value) { return value; } one();");
    EXPECT(false);
  } catch (const minijs::RuntimeError&) {
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("one");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 0);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Cold);
}

void testJitCompilesCallOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    function callAdd(value) { return addOne(value); }
    addOne(0);
    callAdd(1);
    callAdd(41);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("callAdd");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("callAdd") != nullptr);
}

void testJitCompilesMethodCallOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Counter {
      add(a, b) {
        return a + b;
      }
    }

    function callMethod(counter, a, b) {
      return counter.add(a, b);
    }

    let counter = Counter();
    counter.add(0, 0);
    callMethod(counter, 1, 2);
    callMethod(counter, 20, 22);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("callMethod");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("callMethod") != nullptr);
}

void testJitCompilesSuperCallOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Base {
      value() {
        return 40;
      }
    }

    class Child < Base {
      value() {
        return super.value() + 2;
      }
    }

    function callValue(child) {
      return child.value();
    }

    let child = Child();
    child.value();
    callValue(child);
    callValue(child);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("callValue");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("callValue") != nullptr);
}

void testJitCompilesClosureOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function makeAdder(x) {
      function add(y) {
        return x + y;
      }
      return add;
    }

    let add40 = makeAdder(40);
    add40(2);
    let add41 = makeAdder(41);
    add41(1);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("makeAdder");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("makeAdder") != nullptr);
}

void testJitCompilesClassFamilyOpcodes() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function makeChildClass() {
      class Base {
        value() {
          return 40;
        }

        static bonus() {
          return 2;
        }
      }

      class Child < Base {}
      return Child;
    }

    let First = makeChildClass();
    First().value();
    let Second = makeChildClass();
    Second().value() + Second.bonus();
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("makeChildClass");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("makeChildClass") != nullptr);
}

void testBaselineExecutorRunsArithmeticAndLocals() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function calc(a, b) {
      let c = a + b * 2;
      return c - 1;
    }
    calc(1, 2);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("calc");
  EXPECT(code != nullptr);
  EXPECT(!code->instructions.empty());
  EXPECT(code->instructions.back().opcode == minijs::Opcode::Return);

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaseline("calc", {minijs::Value(10.0), minijs::Value(6.0)});
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 21);
}

void testBaselineExecutorRunsSimpleLoop() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function sumTo(n) {
      let i = 0;
      let total = 0;
      while (i < n) {
        total = total + i;
        i = i + 1;
      }
      return total;
    }
    sumTo(1);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("sumTo");
  EXPECT(code != nullptr);

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaseline("sumTo", {minijs::Value(5.0)});
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 10);
}

void testBaselineExecutorPreservesArithmeticErrors() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function divide(a, b) { return a / b; }
    divide(1, 1);
  )");

  try {
    vm.debugExecuteGlobalFunctionBaseline("divide", {minijs::Value(1.0), minijs::Value(0.0)});
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testBaselineCompilerGeneratesNativeStubForAddFunction() {
  const minijs::Chunk chunk = compileProgram("function addOne(value) { return value + 1; }");
  const auto function = lastBytecodeFunctionNamed(chunk, "addOne");
  EXPECT(function != nullptr);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
  if (!nativeBaselineBackendAvailable()) {
    EXPECT(!result.succeeded());
    EXPECT(result.code == nullptr);
    return;
  }

  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(result.code->executableMemory != nullptr);
  EXPECT(result.code->executableMemory->size() > 0);
  EXPECT(!result.code->instructions.empty());
  EXPECT(result.code->instructions.back().opcode == minijs::Opcode::Return);

  const minijs::DecodedInstruction getLocal =
      findDecodedInstruction(function->chunk, minijs::Opcode::GetLocal);
  const minijs::DecodedInstruction constant =
      findDecodedInstruction(function->chunk, minijs::Opcode::Constant);
  EXPECT(result.code->bytecodeOffsetToNativeOffset.size() ==
         function->chunk.code().size() + 1);
  EXPECT(result.code->bytecodeOffsetToNativeOffset[getLocal.offset] !=
         minijs::kInvalidNativeOffset);
  EXPECT(result.code->bytecodeOffsetToNativeOffset[constant.offset] !=
         minijs::kInvalidNativeOffset);
  EXPECT(result.code->bytecodeOffsetToNativeOffset[function->chunk.code().size()] !=
         minijs::kInvalidNativeOffset);
  EXPECT(result.code->bytecodeOffsetToNativeOffset[function->chunk.code().size()] <
         result.code->executableMemory->size());
}

void testBaselineCompilerGeneratesNativeStubForArithmeticFunction() {
  const minijs::Chunk chunk =
      compileProgram("function calc(a, b, c, d, e) { return -(((a - b) * c) / d % e); }");
  const auto function = lastBytecodeFunctionNamed(chunk, "calc");
  EXPECT(function != nullptr);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
  if (!nativeBaselineBackendAvailable()) {
    EXPECT(!result.succeeded());
    EXPECT(result.code == nullptr);
    return;
  }

  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(result.code->executableMemory != nullptr);
  EXPECT(result.code->executableMemory->size() > 0);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Sub).opcode ==
         minijs::Opcode::Sub);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Mul).opcode ==
         minijs::Opcode::Mul);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Div).opcode ==
         minijs::Opcode::Div);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Mod).opcode ==
         minijs::Opcode::Mod);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Negate).opcode ==
         minijs::Opcode::Negate);
}

void testBaselineCompilerGeneratesNativeStubForDefineGlobalOpcode() {
  auto function = std::make_shared<minijs::BytecodeFunction>();
  function->name = "defineGlobal";

  const auto valueIndex =
      static_cast<std::uint8_t>(function->chunk.addConstant(minijs::Value(42.0)));
  const auto nameIndex =
      static_cast<std::uint8_t>(function->chunk.addConstant(minijs::Value(std::string("answer"))));

  function->chunk.writeOpcode(minijs::Opcode::Constant);
  function->chunk.writeByte(valueIndex);
  function->chunk.writeOpcode(minijs::Opcode::DefineGlobal);
  function->chunk.writeByte(nameIndex);
  function->chunk.writeOpcode(minijs::Opcode::GetGlobal);
  function->chunk.writeByte(nameIndex);
  function->chunk.writeOpcode(minijs::Opcode::Return);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
  if (!nativeBaselineBackendAvailable()) {
    EXPECT(!result.succeeded());
    EXPECT(result.code == nullptr);
    return;
  }

  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::DefineGlobal).opcode ==
         minijs::Opcode::DefineGlobal);
}

void testBaselineCompilerGeneratesNativeStubForClosureAccessOpcodes() {
  const minijs::Chunk chunk = compileProgram(R"(
    function makeCounter() {
      let value = 0;
      function inc() {
        value = value + 1;
        return value;
      }
      return inc;
    }
    makeCounter();
  )");
  const auto makeCounter = lastBytecodeFunctionNamed(chunk, "makeCounter");
  EXPECT(makeCounter != nullptr);
  const auto inc = lastBytecodeFunctionNamed(makeCounter->chunk, "inc");
  EXPECT(inc != nullptr);

  minijs::BaselineCompiler compiler;
  minijs::BaselineCompileResult result = compiler.compile(*makeCounter);
  if (!nativeBaselineBackendAvailable()) {
    EXPECT(!result.succeeded());
    EXPECT(result.code == nullptr);
    return;
  }

  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(findDecodedInstruction(makeCounter->chunk, minijs::Opcode::Closure).opcode ==
         minijs::Opcode::Closure);

  result = compiler.compile(*inc);
  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(findDecodedInstruction(inc->chunk, minijs::Opcode::GetUpvalue).opcode ==
         minijs::Opcode::GetUpvalue);
  EXPECT(findDecodedInstruction(inc->chunk, minijs::Opcode::SetUpvalue).opcode ==
         minijs::Opcode::SetUpvalue);

  const minijs::Chunk selfChunk = compileProgram(R"(
    function makeSelf() {
      function self() { return self; }
      return self;
    }
    makeSelf();
  )");
  const auto makeSelf = lastBytecodeFunctionNamed(selfChunk, "makeSelf");
  EXPECT(makeSelf != nullptr);
  const auto self = lastBytecodeFunctionNamed(makeSelf->chunk, "self");
  EXPECT(self != nullptr);
  result = compiler.compile(*self);
  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(findDecodedInstruction(self->chunk, minijs::Opcode::GetCurrentClosure).opcode ==
         minijs::Opcode::GetCurrentClosure);

  minijs::BytecodeFunction closeFunction;
  closeFunction.name = "close";
  const auto valueIndex =
      static_cast<std::uint8_t>(closeFunction.chunk.addConstant(minijs::Value(1.0)));
  closeFunction.chunk.writeOpcode(minijs::Opcode::Constant);
  closeFunction.chunk.writeByte(valueIndex);
  closeFunction.chunk.writeOpcode(minijs::Opcode::CloseUpvalue);
  closeFunction.chunk.writeOpcode(minijs::Opcode::Constant);
  closeFunction.chunk.writeByte(valueIndex);
  closeFunction.chunk.writeOpcode(minijs::Opcode::Return);

  result = compiler.compile(closeFunction);
  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
}

void testBaselineCompilerGeneratesNativeStubForClassFamilyOpcodes() {
  const minijs::Chunk chunk = compileProgram(R"(
    function makeBox() {
      class Base {
        get() { return 1; }
        static tag() { return 2; }
      }
      class Box < Base {
        value() { return 3; }
      }
      return Box;
    }
  )");
  const auto function = lastBytecodeFunctionNamed(chunk, "makeBox");
  EXPECT(function != nullptr);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
  if (!nativeBaselineBackendAvailable()) {
    EXPECT(!result.succeeded());
    EXPECT(result.code == nullptr);
    return;
  }

  EXPECT(result.succeeded());
  EXPECT(result.error.empty());
  EXPECT(result.code != nullptr);
  EXPECT(result.code->entry != nullptr);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Class).opcode ==
         minijs::Opcode::Class);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Method).opcode ==
         minijs::Opcode::Method);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::StaticMethod).opcode ==
         minijs::Opcode::StaticMethod);
  EXPECT(findDecodedInstruction(function->chunk, minijs::Opcode::Inherit).opcode ==
         minijs::Opcode::Inherit);
}

#if defined(__aarch64__) || defined(_M_ARM64) || \
    (defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)))
void testBaselineCompilerNativeEntryRunsAddFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function add(a, b) { return a + b; }
    add(1, 2);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("add");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("add", {minijs::Value(4.0), minijs::Value(8.0)},
                                                 code->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 12);
}

void testBaselineCompilerNativeEntryRunsArithmeticFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function calc(a, b, c, d, e) {
      return -(((a - b) * c) / d % e);
    }
    calc(10, 4, 3, 2, 4);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("calc");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  const minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "calc",
      {minijs::Value(10.0), minijs::Value(4.0), minijs::Value(3.0), minijs::Value(2.0),
       minijs::Value(4.0)},
      code->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == -1);
}

void testBaselineCompilerNativeEntryRunsEqualFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function equal(a, b) { return a == b; }
    equal(1, 1);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("equal");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("equal", {minijs::Value(4.0), minijs::Value(4.0)},
                                                 code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  result =
      vm.debugExecuteGlobalFunctionBaselineEntry("equal", {minijs::Value(4.0), minijs::Value(5.0)},
                                                 code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(false)));

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "equal", {minijs::Value(std::string("x")), minijs::Value(std::string("x"))}, code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));
}

void testBaselineCompilerNativeEntryRunsComparisonFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function greater(a, b) { return a > b; }
    function less(a, b) { return a < b; }
    greater(2, 1);
    less(1, 2);
  )");

  const minijs::BaselineCode* greaterCode = vm.debugGlobalFunctionBaselineCode("greater");
  EXPECT(greaterCode != nullptr);
  EXPECT(greaterCode->entry != nullptr);
  minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "greater", {minijs::Value(4.0), minijs::Value(3.0)}, greaterCode->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "greater", {minijs::Value(3.0), minijs::Value(4.0)}, greaterCode->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(false)));

  const minijs::BaselineCode* lessCode = vm.debugGlobalFunctionBaselineCode("less");
  EXPECT(lessCode != nullptr);
  EXPECT(lessCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "less", {minijs::Value(3.0), minijs::Value(4.0)}, lessCode->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "less", {minijs::Value(4.0), minijs::Value(3.0)}, lessCode->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(false)));
}

void testBaselineCompilerNativeEntryRunsNotFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function logicalNot(value) { return !value; }
    logicalNot(false);
  )");

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("logicalNot");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("logicalNot", {minijs::Value(false)},
                                                 code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  result = vm.debugExecuteGlobalFunctionBaselineEntry("logicalNot", {minijs::Value(true)},
                                                      code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(false)));

  result = vm.debugExecuteGlobalFunctionBaselineEntry("logicalNot", {minijs::Value(0.0)},
                                                      code->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));
}

void testBaselineCompilerNativeEntryRunsControlFlowFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function choose(flag) {
      if (flag) {
        return 11;
      }
      return 22;
    }
    function orValue(a, b) { return a || b; }
    function countTo(n) {
      let i = 0;
      while (i < n) {
        i = i + 1;
      }
      return i;
    }
    choose(true);
    orValue(false, 1);
    countTo(1);
  )");

  const minijs::BaselineCode* chooseCode = vm.debugGlobalFunctionBaselineCode("choose");
  EXPECT(chooseCode != nullptr);
  EXPECT(chooseCode->entry != nullptr);
  minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "choose", {minijs::Value(true)}, chooseCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 11);

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "choose", {minijs::Value(false)}, chooseCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 22);

  const minijs::BaselineCode* orCode = vm.debugGlobalFunctionBaselineCode("orValue");
  EXPECT(orCode != nullptr);
  EXPECT(orCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "orValue", {minijs::Value(false), minijs::Value(42.0)}, orCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "orValue", {minijs::Value(true), minijs::Value(42.0)}, orCode->entry);
  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::BaselineCode* countCode = vm.debugGlobalFunctionBaselineCode("countTo");
  EXPECT(countCode != nullptr);
  EXPECT(countCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "countTo", {minijs::Value(4.0)}, countCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 4);
}

void testBaselineCompilerNativeEntryRunsGlobalFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    let shared = 40;
    function readShared() { return shared + 2; }
    function writeShared(value) {
      shared = value;
      return shared;
    }
    readShared();
    writeShared(5);
  )");

  const minijs::BaselineCode* readCode = vm.debugGlobalFunctionBaselineCode("readShared");
  EXPECT(readCode != nullptr);
  EXPECT(readCode->entry != nullptr);
  minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("readShared", {}, readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 7);

  const minijs::BaselineCode* writeCode = vm.debugGlobalFunctionBaselineCode("writeShared");
  EXPECT(writeCode != nullptr);
  EXPECT(writeCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry("writeShared", {minijs::Value(12.0)},
                                                      writeCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 12);

  result = vm.debugExecuteGlobalFunctionBaselineEntry("readShared", {}, readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 14);
}

void testBaselineCompilerNativeEntryRunsArrayAndIndexFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    let saved = [1, 2];
    function makePair(a, b) { return [a, b]; }
    function readSaved(index) { return saved[index]; }
    function writeSaved(index, value) {
      saved[index] = value;
      return saved[index];
    }
    makePair(1, 2);
    readSaved(0);
    writeSaved(1, 3);
  )");

  const minijs::BaselineCode* makeCode = vm.debugGlobalFunctionBaselineCode("makePair");
  EXPECT(makeCode != nullptr);
  EXPECT(makeCode->entry != nullptr);
  minijs::Value array = vm.debugExecuteGlobalFunctionBaselineEntry(
      "makePair", {minijs::Value(4.0), minijs::Value(5.0)}, makeCode->entry);
  EXPECT(array.isArray());
  EXPECT(array.asArray().size() == 2);
  EXPECT(array.asArray()[0].asNumber() == 4);
  EXPECT(array.asArray()[1].asNumber() == 5);

  const minijs::BaselineCode* readCode = vm.debugGlobalFunctionBaselineCode("readSaved");
  EXPECT(readCode != nullptr);
  EXPECT(readCode->entry != nullptr);
  minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "readSaved", {minijs::Value(1.0)}, readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 3);

  const minijs::BaselineCode* writeCode = vm.debugGlobalFunctionBaselineCode("writeSaved");
  EXPECT(writeCode != nullptr);
  EXPECT(writeCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "writeSaved", {minijs::Value(0.0), minijs::Value(9.0)}, writeCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 9);

  result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "readSaved", {minijs::Value(0.0)}, readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 9);
}

void testBaselineCompilerNativeEntryRunsObjectAndPropertyFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    let saved = { age: 18 };
    function makeObject(value) { return { value: value, next: value + 1 }; }
    function readSaved() { return saved.age; }
    function writeSaved(value) {
      saved.age = value;
      return saved.age;
    }
    makeObject(1);
    readSaved();
    writeSaved(19);
  )");

  const minijs::BaselineCode* makeCode = vm.debugGlobalFunctionBaselineCode("makeObject");
  EXPECT(makeCode != nullptr);
  EXPECT(makeCode->entry != nullptr);
  minijs::Value object = vm.debugExecuteGlobalFunctionBaselineEntry(
      "makeObject", {minijs::Value(4.0)}, makeCode->entry);
  EXPECT(object.isObject());
  EXPECT(object.asObject().at("value").asNumber() == 4);
  EXPECT(object.asObject().at("next").asNumber() == 5);

  const minijs::BaselineCode* readCode = vm.debugGlobalFunctionBaselineCode("readSaved");
  EXPECT(readCode != nullptr);
  EXPECT(readCode->entry != nullptr);
  minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry("readSaved", {},
                                                                    readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 19);

  const minijs::BaselineCode* writeCode = vm.debugGlobalFunctionBaselineCode("writeSaved");
  EXPECT(writeCode != nullptr);
  EXPECT(writeCode->entry != nullptr);
  result = vm.debugExecuteGlobalFunctionBaselineEntry("writeSaved", {minijs::Value(21.0)},
                                                      writeCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 21);

  result = vm.debugExecuteGlobalFunctionBaselineEntry("readSaved", {}, readCode->entry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 21);
}

void testJitDispatchRunsNativeBaselineEntryForSupportedFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function add(a, b) { return a + b; }
    add(1, 2);
    add(4, 8);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 12);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("add");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("add");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsNativeBaselineEntryForEqualFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function equal(a, b) { return a == b; }
    equal(1, 1);
    equal("x", "x");
  )");

  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("equal");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("equal");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsNativeBaselineEntryForComparisonFunctions() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function greater(a, b) { return a > b; }
    greater(1, 2);
    greater(5, 3);
  )");

  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::JitFeedback* greaterFeedback = vm.debugGlobalFunctionJitFeedback("greater");
  EXPECT(greaterFeedback != nullptr);
  EXPECT(greaterFeedback->state == minijs::JitState::Compiled);
  EXPECT(greaterFeedback->baselineEntryCount == 1);

  result = runBytecodeProgramOnVm(vm, R"(
    function less(a, b) { return a < b; }
    less(2, 1);
    less(3, 5);
  )");

  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::JitFeedback* lessFeedback = vm.debugGlobalFunctionJitFeedback("less");
  EXPECT(lessFeedback != nullptr);
  EXPECT(lessFeedback->state == minijs::JitState::Compiled);
  EXPECT(lessFeedback->baselineEntryCount == 1);
}

void testJitDispatchRunsNativeBaselineEntryForNotFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function logicalNot(value) { return !value; }
    logicalNot(true);
    logicalNot("");
  )");

  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::JitFeedback* notFeedback = vm.debugGlobalFunctionJitFeedback("logicalNot");
  EXPECT(notFeedback != nullptr);
  EXPECT(notFeedback->state == minijs::JitState::Compiled);
  EXPECT(notFeedback->baselineEntryCount == 1);

  result = runBytecodeProgramOnVm(vm, R"(
    function notEqual(a, b) { return a != b; }
    notEqual(1, 1);
    notEqual(1, 2);
  )");

  EXPECT(result.isBoolean());
  EXPECT(result.equals(minijs::Value(true)));

  const minijs::JitFeedback* notEqualFeedback = vm.debugGlobalFunctionJitFeedback("notEqual");
  EXPECT(notEqualFeedback != nullptr);
  EXPECT(notEqualFeedback->state == minijs::JitState::Compiled);
  EXPECT(notEqualFeedback->baselineEntryCount == 1);
}

void testJitDispatchRunsNativeBaselineEntryForControlFlowFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function control(flag, n) {
      let total = 0;
      if (flag) {
        total = 1;
      } else {
        total = 2;
      }
      while (total < n) {
        total = total + 1;
      }
      return total;
    }
    control(false, 3);
    control(true, 4);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 4);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("control");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("control");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsNativeBaselineEntryForGlobalFunction() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    let counter = 1;
    function bump() {
      counter = counter + 1;
      return counter;
    }
    bump();
    bump();
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 3);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("bump");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("bump");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}
#endif

void testBaselineRuntimeAbiRunsArithmeticEntry() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function add(a, b) { return a + b; }");

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("add", {minijs::Value(2.0), minijs::Value(5.0)},
                                                 baselineAbiAddEntry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 7);
}

void testBaselineRuntimeAbiPushesConstantsAndSetsLocals() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function literal() { return 41; }");

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("literal", {}, baselineAbiConstantEntry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 41);
}

void testBaselineRuntimeAbiPushesValuesByPointer() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function value() { return 0; }");

  const minijs::Value result =
      vm.debugExecuteGlobalFunctionBaselineEntry("value", {}, baselineAbiPushEntry);
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 9);
}

void testBaselineRuntimeAbiTurnsHelperErrorsIntoFailedFrame() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function divide(a, b) { return a / b; }");

  try {
    vm.debugExecuteGlobalFunctionBaselineEntry(
        "divide", {minijs::Value(1.0), minijs::Value(0.0)}, baselineAbiDivisionByZeroEntry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }
}

void testBaselineRuntimeAbiRejectsInvalidLocalAccess() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function id(value) { return value; }");

  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("id", {minijs::Value(1.0)},
                                               baselineAbiInvalidLocalEntry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
}

void testBaselineRuntimeAbiRejectsMutationAfterReturn() {
  minijs::VM vm;

  runBytecodeProgramOnVm(vm, "function value() { return 0; }");

  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("value", {}, baselineAbiMutatesAfterReturnEntry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline runtime helper failed");
  }
}

void testJitDispatchRunsGlobalOpcodes() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    let total = 0;
    function bump() {
      total = total + 2;
      return total;
    }
    bump();
    bump();
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 4);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("bump");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(vm.debugGlobalFunctionBaselineCode("bump") != nullptr);
}

void testJitDispatchRunsArrayAndIndexOpcodes() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function pick(value) {
      let values = [value, value + 1];
      values[0] = values[1] + 2;
      return values[0];
    }
    pick(3);
    pick(4);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 7);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("pick");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("pick");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsObjectLiteralAndPropertyOpcodes() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function make(value) {
      let object = { value: value, next: value + 1 };
      return object.next;
    }
    make(1);
    make(2);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 3);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("make");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.misses >= 1);
  EXPECT(stats.updates >= 1);
  EXPECT(stats.hits >= 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("make");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsSetPropertyOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    let person = { age: 18 };
    function bumpAge(person) {
      person.age = person.age + 1;
      return person.age;
    }
    bumpAge(person);
    bumpAge(person);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 20);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("bumpAge");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);

  const minijs::PropertyInlineCacheStats stats = vm.debugPropertyInlineCacheStats();
  EXPECT(stats.setMisses >= 1);
  EXPECT(stats.setUpdates >= 1);
  EXPECT(stats.setHits >= 1);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("bumpAge");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
}

void testJitDispatchRunsUpvalueOpcodes() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function makeCounter() {
      let value = 0;
      function inc() {
        value = value + 1;
        return value;
      }
      return inc;
    }
    let inc = makeCounter();
    inc();
    inc();
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 2);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("inc");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("inc");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  minijs::Value entryResult =
      vm.debugExecuteGlobalFunctionBaselineEntry("inc", {}, code->entry);
  EXPECT(entryResult.isNumber());
  EXPECT(entryResult.asNumber() == 3);
}

void testJitDispatchEntersCompiledFunctionOnLaterCall() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    addOne(1);
    addOne(2);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 3);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("addOne");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchRespectsDisabledJit() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    addOne(1);
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("addOne");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 0);

  vm.setJitEnabled(false);
  const minijs::Value result = runBytecodeProgramOnVm(vm, "addOne(2);");
  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 3);

  feedback = vm.debugGlobalFunctionJitFeedback("addOne");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 1);
  EXPECT(feedback->baselineEntryCount == 0);
}

void testJitDispatchPreservesRuntimeErrors() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function divide(a, b) { return a / b; }
      divide(1, 1);
      divide(1, 0);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("divide");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchComparisonHelpersPreserveTypeErrors() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function greater(a, b) { return a > b; }
      greater(2, 1);
      greater("x", 1);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* greaterFeedback = vm.debugGlobalFunctionJitFeedback("greater");
  EXPECT(greaterFeedback != nullptr);
  EXPECT(greaterFeedback->callCount == 2);
  EXPECT(greaterFeedback->baselineEntryCount == 1);
  EXPECT(greaterFeedback->state == minijs::JitState::Compiled);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function less(a, b) { return a < b; }
      less(1, 2);
      less("x", 1);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* lessFeedback = vm.debugGlobalFunctionJitFeedback("less");
  EXPECT(lessFeedback != nullptr);
  EXPECT(lessFeedback->callCount == 2);
  EXPECT(lessFeedback->baselineEntryCount == 1);
  EXPECT(lessFeedback->state == minijs::JitState::Compiled);
}

void testBaselineNumberComparisonsHandleSpecialValues() {
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, R"(
    function greater(a, b) { return a > b; }
    function less(a, b) { return a < b; }
    function equal(a, b) { return a == b; }
    greater(2, 1);
    less(1, 2);
    equal(1, 1);
  )");

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const double pairs[][2] = {
      {1, 2}, {2, 1}, {2, 2}, {-2, -1}, {0.0, -0.0}, {-0.0, 0.0},
      {inf, 1}, {-inf, 1}, {1, inf}, {1, -inf}, {inf, inf}, {-inf, inf},
      {nan, 1}, {1, nan}, {nan, nan}};
  for (const std::string name : {"greater", "less", "equal"}) {
    const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode(name);
    EXPECT(code != nullptr);
    EXPECT(code->entry != nullptr);
    for (const auto& pair : pairs) {
      const std::vector<minijs::Value> args = {minijs::Value(pair[0]), minijs::Value(pair[1])};
      const bool expected = name == "equal" ? pair[0] == pair[1]
                            : name == "greater" ? pair[0] > pair[1] : pair[0] < pair[1];
      const minijs::Value decoded = vm.debugExecuteGlobalFunctionBaseline(name, args);
      const minijs::Value native =
          vm.debugExecuteGlobalFunctionBaselineEntry(name, args, code->entry);
      EXPECT(native.isBoolean());
      EXPECT(native.equals(minijs::Value(expected)));
      EXPECT(native.equals(decoded));
    }
    // 分别让左右操作数的类型 guard 失败，检查完整 helper 的错误语义。
    for (const auto& args : {std::vector<minijs::Value>{minijs::Value(std::string("x")), minijs::Value(1.0)},
                            std::vector<minijs::Value>{minijs::Value(1.0), minijs::Value(std::string("x"))}}) {
      try {
        const minijs::Value result =
            vm.debugExecuteGlobalFunctionBaselineEntry(name, args, code->entry);
        EXPECT(name == "equal");
        EXPECT(result.equals(minijs::Value(false)));
      } catch (const minijs::RuntimeError& error) {
        EXPECT(name != "equal");
        EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
      }
    }
  }
}

void testJitNumberComparisonsComposeWithControlFlowAndCalls() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      function next(i) { let values = [i, i + 1]; return values[1]; }
      function count(n) {
        let i = 0;
        let total = 0;
        while (i < n) {
          if (i > 2) { total = total + 1; }
          i = next(i);
        }
        return [total, i < n, i > 2];
      }
      count(1);
      count(8);
    )");
    EXPECT(result.toString() == "[5, false, true]");
    if (enabled) {
      const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("count");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->baselineEntryCount == 1);
    }
  }
}

void testJitInlineEqualFallbackAndBranching() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      function equal(a, b) { return a == b; }
      function branch(a, b) {
        if (equal(a, b)) { return 7; }
        return 9;
      }
      equal(1, 1);
      branch(1, 1);
      let object = {x: 1};
      let array = [1];
      function identity() { return 1; }
      [equal(2, 2), equal(2, 3), equal("x", "x"), equal("x", "y"),
       equal(true, true), equal(true, false), equal(null, null),
       equal(undefined, undefined), equal(null, undefined),
       equal(object, object), equal(object, {x: 1}),
       equal(array, array), equal(array, [1]),
       equal(identity, identity), equal(1, true), equal("1", 1),
       equal(1, "1"), branch(4, 4), branch(4, 5)];
    )");
    EXPECT(result.toString() ==
           "[true, false, true, false, true, false, true, true, false, "
           "true, false, true, false, true, false, false, false, 7, 9]");
    if (enabled) {
      const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("equal");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->baselineEntryCount > 0);
      const minijs::JitFeedback* branchFeedback = vm.debugGlobalFunctionJitFeedback("branch");
      EXPECT(branchFeedback != nullptr);
      EXPECT(branchFeedback->baselineEntryCount == 2);
    }
  }
}

void testJitNumberLocalsPreserveTypeChangesAndCapturedSlots() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      function locals() {
        let value = 1;
        function read() { return value; }
        value = 42;
        let number = read();
        value = {x: 7};
        let object = read();
        value = 9;
        let final = read();
        value = "ok";
        return [number, object.x, final, value];
      }
      class Box {
        read(n) { n = n + 1; let copy = n; return copy; }
      }
      locals();
      let box = Box();
      box.read(1);
      [locals(), box.read(40)];
    )");
    EXPECT(result.toString() == "[[42, 7, 9, ok], 41]");
    if (enabled) {
      const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("locals");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->baselineEntryCount == 1);
    }
  }
}

void testJitNumberLocalsSurviveGrowingRecursiveStack() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      function recurse(n) {
        if (n == 0) { return 0; }
        let copy = n;
        let child = recurse(n - 1);
        copy = copy + child;
        return copy;
      }
      recurse(1);
      recurse(80);
    )");
    EXPECT(result.equals(minijs::Value(3240.0)));
    if (enabled) {
      const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("recurse");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->baselineEntryCount > 80);
    }
  }
}

void testNativeNumberLocalsPreservePayloadAndRejectInvalidSlots() {
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, R"(
    function copy(value) { let result = 1; result = value; return result; }
    copy(1);
  )");
  const auto* code = vm.debugGlobalFunctionBaselineCode("copy");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);
  for (const double value : {0.0, -0.0, std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    const minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
        "copy", {minijs::Value(value)}, code->entry);
    EXPECT(result.isNumber());
    if (std::isnan(value)) {
      EXPECT(std::isnan(result.asNumber()));
    } else {
      EXPECT(result.asNumber() == value);
      EXPECT(std::signbit(result.asNumber()) == std::signbit(value));
    }
  }
  for (const minijs::Opcode opcode : {minijs::Opcode::GetLocal, minijs::Opcode::SetLocal}) {
    minijs::BytecodeFunction invalid;
    invalid.params = {"value"};
    if (opcode == minijs::Opcode::SetLocal) {
      invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
      invalid.chunk.writeByte(0);
    }
    invalid.chunk.writeOpcode(opcode);
    invalid.chunk.writeByte(255);
    invalid.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(invalid);
    EXPECT(compiled.succeeded());
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("copy", {minijs::Value(1.0)},
                                                 compiled.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
  }
}

void testNativeNumberConstantsPreserveExactBits() {
#if !defined(_WIN32) || (!defined(_M_X64) && !defined(__x86_64__))
  // 手工常量 chunk 的位编码内嵌测试只针对 Windows x64 后端。
  return;
#endif
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function constant() { return 0; }");
  vm.setBaselineHelperProfilingEnabled(true);
  const std::uint64_t payloads[] = {
      0x0000000000000000ULL, 0x8000000000000000ULL, 0x3FF8000000000000ULL,
      0x7FF0000000000000ULL, 0xFFF0000000000000ULL, 0x0000000000000001ULL,
      0x7FF8000000001234ULL, 0xFFF8000000005678ULL};
  for (const std::uint64_t expected : payloads) {
    double number = 0;
    std::memcpy(&number, &expected, sizeof(number));
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(number));
    function.chunk.writeOpcode(minijs::Opcode::Constant);
    function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    const minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
        "constant", {}, compiled.code->entry);
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[2]);
    EXPECT(result.isNumber());
    const double actual = result.asNumber();
    std::uint64_t actualBits = 0;
    std::memcpy(&actualBits, &actual, sizeof(actualBits));
    EXPECT(actualBits == expected);
  }

  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselinePushNumberSlot");
    EXPECT(helper.name != "minijsBaselineReturn");
    if (helper.name == "minijsBaselineReturnNumberBits") {
      found = true;
      EXPECT(helper.calls == 8);
    }
  }
  EXPECT(found);
  vm.setBaselineHelperProfilingEnabled(false);

  // 连续压入数字常量，覆盖 vector 扩容后重新计算栈顶地址。
  minijs::BytecodeFunction many;
  for (std::uint8_t index = 0; index < 80; ++index) {
    many.chunk.addConstant(minijs::Value(static_cast<double>(index)));
    many.chunk.writeOpcode(minijs::Opcode::Constant);
    many.chunk.writeByte(index);
  }
  many.chunk.writeOpcode(minijs::Opcode::Array);
  many.chunk.writeByte(80);
  many.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(many);
  EXPECT(compiled.succeeded());
  const minijs::Value result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "constant", {}, compiled.code->entry);
  EXPECT(result.asArray().size() == 80);
  for (std::size_t index = 0; index < result.asArray().size(); ++index) {
    EXPECT(result.asArray()[index].equals(minijs::Value(static_cast<double>(index))));
  }
}

void testJitNumberConstantsComposeWithOtherConstantTypes() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      function constants() {
        let number = 1.5;
        if (number == 1.5) { number = number + 2; }
        return [number, "ok", true, false, null, undefined, 17];
      }
      constants();
      constants();
    )");
    EXPECT(result.toString() == "[3.5, ok, true, false, null, undefined, 17]");
    if (enabled) {
      const auto* feedback = vm.debugGlobalFunctionJitFeedback("constants");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->baselineEntryCount == 1);
    }
  }
}

void testDiscardedValuesPreserveSurvivorsAcrossExecutors() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
      let survivor = null;
      function discard(value) {
        let object = {value: value};
        function read() { return object.value; }
        survivor = read;
        value;
        object;
        [value];
        "unused";
        true;
        null;
        undefined;
        read;
        { let transient = 99; transient; }
        return object.value;
      }
      discard(1);
      discard(42);
    )");
    EXPECT(result.equals(minijs::Value(42.0)));
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "survivor();").equals(minijs::Value(42.0)));
    if (enabled) {
      const auto* code = vm.debugGlobalFunctionBaselineCode("discard");
      EXPECT(code != nullptr);
      EXPECT(vm.debugExecuteGlobalFunctionBaseline("discard", {minijs::Value(43.0)})
                 .equals(minijs::Value(43.0)));
      vm.collectGarbage();
      EXPECT(runBytecodeProgramOnVm(vm, "survivor();").equals(minijs::Value(43.0)));
      if (nativeBaselineBackendAvailable()) {
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
                     "discard", {minijs::Value(44.0)}, code->entry)
                   .equals(minijs::Value(44.0)));
        vm.collectGarbage();
        EXPECT(runBytecodeProgramOnVm(vm, "survivor();").equals(minijs::Value(44.0)));
      }
    }
  }
}

void testDiscardTopPreservesStackUnderflowError() {
  minijs::VM vm;
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Pop);
  chunk.writeOpcode(minijs::Opcode::Return);
  try {
    vm.run(chunk);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  runBytecodeProgramOnVm(vm, "function discard() { return 0; }");
  minijs::BytecodeFunction invalid;
  // 测试入口先压入 callee；第一次 Pop 删除它，第二次 Pop 遇到空栈。
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(invalid);
  EXPECT(compiled.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("discard", {}, compiled.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
}

void testBaselineBooleanBranchesAndTruthyFallback() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, R"(
    function truth(value) { if (value) { return 1; } return 0; }
    truth(true);
  )");
  const auto* code = vm.debugGlobalFunctionBaselineCode("truth");
  EXPECT(code != nullptr);
  struct Case {
    minijs::Value value;
    bool truthy;
  };
  const std::vector<Case> cases = {
      {minijs::Value(true), true}, {minijs::Value(false), false},
      {minijs::Value(0.0), false}, {minijs::Value(-0.0), false},
      {minijs::Value(1.0), true}, {minijs::Value(-1.0), true},
      {minijs::Value(std::numeric_limits<double>::quiet_NaN()), true},
      {minijs::Value(std::numeric_limits<double>::infinity()), true},
      {minijs::Value(std::string()), false}, {minijs::Value(std::string("x")), true},
      {minijs::Value(), false}, {minijs::Value::undefined(), false},
      {minijs::Value(std::vector<minijs::Value>{}), true},
      {minijs::Value(std::unordered_map<std::string, minijs::Value>{}), true}};
  for (const auto& test : cases) {
    const std::vector<minijs::Value> arguments = {test.value};
    const minijs::Value expected(test.truthy ? 1.0 : 0.0);
    EXPECT(vm.debugExecuteGlobalFunctionBaseline("truth", arguments).equals(expected));
    if (nativeBaselineBackendAvailable()) {
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("truth", arguments, code->entry)
                 .equals(expected));
    }
  }
}

void testJitBooleanBranchesPreserveLogicalOperandsAndSideEffects() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      let probes = 0;
      function probe(value) { probes = probes + 1; return value; }
      function logic(left, right) { return [left && right, left || right]; }
      function loop() {
        let i = 0;
        let total = 0;
        while (i < 6) {
          if (i == 2 || i > 3) { total = total + 1; }
          i = i + 1;
        }
        return total;
      }
      logic(true, 7);
      loop();
      let left = probe(false) && probe(true);
      let right = probe(true) || probe(false);
      let object = {};
      [logic(false, 7), logic(true, 7), loop(), probes, left, right,
       logic("", 8), logic(0, 8), (object || 9) == object];
    )");
    EXPECT(result.toString() ==
           "[[false, 7], [7, true], 3, 2, false, true, [, 8], [0, 8], true]");
    if (enabled) {
      EXPECT(vm.debugGlobalFunctionJitFeedback("logic")->baselineEntryCount >= 4);
      EXPECT(vm.debugGlobalFunctionJitFeedback("loop")->baselineEntryCount == 1);
    }
  }
}

void testBooleanBranchFallbackPreservesStackUnderflow() {
  minijs::VM vm;
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::JumpIfFalse);
  chunk.writeByte(0);
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Return);
  try {
    vm.run(chunk);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  runBytecodeProgramOnVm(vm, "function truth() { return true; }");
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::JumpIfFalse);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(invalid);
  EXPECT(compiled.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("truth", {}, compiled.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
}

void testBaselineBooleanNotAndTruthyFallback() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function invert(value) { return !value; } invert(true);");
  const auto* code = vm.debugGlobalFunctionBaselineCode("invert");
  EXPECT(code != nullptr);
  struct Case {
    minijs::Value value;
    bool expected;
  };
  const std::vector<Case> cases = {
      {minijs::Value(true), false}, {minijs::Value(false), true},
      {minijs::Value(0.0), true}, {minijs::Value(-0.0), true},
      {minijs::Value(1.0), false}, {minijs::Value(-1.0), false},
      {minijs::Value(std::numeric_limits<double>::quiet_NaN()), false},
      {minijs::Value(std::numeric_limits<double>::infinity()), false},
      {minijs::Value(std::string()), true}, {minijs::Value(std::string("x")), false},
      {minijs::Value(), true}, {minijs::Value::undefined(), true},
      {minijs::Value(std::vector<minijs::Value>{}), false},
      {minijs::Value(std::unordered_map<std::string, minijs::Value>{}), false}};
  for (const auto& test : cases) {
    const minijs::Value expected(test.expected);
    EXPECT(vm.debugExecuteGlobalFunctionBaseline("invert", {test.value}).equals(expected));
    if (nativeBaselineBackendAvailable()) {
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("invert", {test.value}, code->entry)
                 .equals(expected));
    }
  }
}

void testJitBooleanNotComposesWithBranchesAndSideEffects() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      let probes = 0;
      function probe(value) { probes = probes + 1; return value; }
      function invert(value) { return !probe(value); }
      function loop(count) {
        let i = 0;
        let total = 0;
        while (i < count) {
          if (!(i < 2)) { total = total + 1; }
          if (!!(i == 2)) { total = total + 1; }
          i = i + 1;
        }
        return total;
      }
      invert(true);
      loop(4);
      class Box {}
      let object = { value: 9 };
      let instance = Box();
      [invert(false), invert(true), invert(""), invert(object), invert(instance),
       invert(probe), !!object, loop(4), probes, object.value];
    )");
    EXPECT(result.toString() == "[true, false, true, false, false, false, true, 3, 7, 9]");
    if (enabled) {
      EXPECT(vm.debugGlobalFunctionJitFeedback("invert")->baselineEntryCount == 6);
      EXPECT(vm.debugGlobalFunctionJitFeedback("loop")->baselineEntryCount == 1);
    }
  }
}

void testBooleanNotFallbackPreservesStackUnderflow() {
  minijs::VM vm;
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Not);
  chunk.writeOpcode(minijs::Opcode::Return);
  try {
    vm.run(chunk);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  runBytecodeProgramOnVm(vm, "function invert() { return true; }");
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Not);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(invalid);
  EXPECT(compiled.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("invert", {}, compiled.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
}

void testBaselineHelperProfileIsOptionalAndTracksFallback() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function invert(value) { return !value; } invert(true);");
  EXPECT(!vm.jitRuntimeStats().helperProfilingEnabled);
  EXPECT(vm.jitRuntimeStats().helpers.empty());
  vm.setBaselineHelperProfilingEnabled(true);
  EXPECT(vm.debugExecuteGlobalFunctionBaseline("invert", {minijs::Value(false)})
             .equals(minijs::Value(true)));
  // decoded executor 不经过 native C ABI helper，不能混进此次采样。
  EXPECT(vm.jitRuntimeStats().helpers.empty());
  if (!nativeBaselineBackendAvailable()) {
    return;
  }
  const auto* code = vm.debugGlobalFunctionBaselineCode("invert");
  const auto calls = [&vm](const std::string& name) {
    for (const auto& helper : vm.jitRuntimeStats().helpers) {
      if (helper.name == name) return helper.calls;
    }
    return std::uint64_t{0};
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("invert", {minijs::Value(false)}, code->entry)
             .equals(minijs::Value(true)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(calls("minijsBaselineNot") == 0); // Boolean 快路径绕过完整 helper。
#else
  EXPECT(calls("minijsBaselineNot") == 1);
#endif
  EXPECT(calls("minijsBaselineReturn") == 1);
  vm.resetBaselineHelperProfile();
  EXPECT(vm.jitRuntimeStats().helpers.empty());
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
             "invert", {minijs::Value(std::string("x"))}, code->entry)
             .equals(minijs::Value(false)));
  EXPECT(calls("minijsBaselineNot") == 1);
  EXPECT(calls("minijsBaselineReturn") == 1);
  vm.setBaselineHelperProfilingEnabled(false);
  vm.debugExecuteGlobalFunctionBaselineEntry("invert", {minijs::Value(std::string())}, code->entry);
  EXPECT(calls("minijsBaselineNot") == 1); // 关闭后保留快照，但停止增加。
  vm.resetBaselineHelperProfile();
  vm.setBaselineHelperProfilingEnabled(true);
  runBytecodeProgramOnVm(vm, "function profileEmpty() { return true; }");
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Not);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(invalid);
  EXPECT(compiled.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("profileEmpty", {}, compiled.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
  EXPECT(calls("minijsBaselineNot") == 1);
  EXPECT(calls("minijsBaselineReturn") == 0);
}

void testNativePairedLocalAndNumberPushPreservesValues() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function pair(value) { return [value, 1]; }");
  minijs::BytecodeFunction function;
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.addConstant(minijs::Value(-0.0));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Array);
  function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->instructions.size() == 4);
  const std::vector<minijs::Value> values = {
      minijs::Value(-0.0), minijs::Value(std::numeric_limits<double>::quiet_NaN()),
      minijs::Value(std::string("kept")), minijs::Value(true), minijs::Value(),
      minijs::Value(std::vector<minijs::Value>{minijs::Value(9.0)})};
  vm.setBaselineHelperProfilingEnabled(true);
  for (const auto& value : values) {
    const auto result = vm.debugExecuteGlobalFunctionBaselineEntry("pair", {value}, compiled.code->entry);
    const auto& array = result.asArray();
    EXPECT(array.size() == 2);
    if (value.isNumber() && std::isnan(value.asNumber())) {
      EXPECT(std::isnan(array[0].asNumber()));
    } else if (value.isArray()) {
      // 返回到宿主时 copyOutValue 重建数组；跨边界比较内容，而非宿主指针。
      EXPECT(array[0].toString() == value.toString());
    } else {
      EXPECT(array[0].equals(value));
    }
    if (value.isNumber() && value.asNumber() == 0.0) {
      EXPECT(std::signbit(array[0].asNumber()) == std::signbit(value.asNumber()));
    }
    EXPECT(array[1].isNumber() && std::signbit(array[1].asNumber()));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselineGetLocalAndPushNumberSlot") {
      found = true;
      EXPECT(helper.calls == values.size());
    }
  }
  EXPECT(found);
#endif
}

void testNativePairedPushDoesNotFuseBranchTarget() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch() { return 0; }");
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(42.0));
  // Jump 的目标为 offset 5，直接进入 Constant，不能额外执行 offset 3 的 GetLocal。
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(255);
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[3] !=
         compiled.code->bytecodeOffsetToNativeOffset[5]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {}, compiled.code->entry)
             .equals(minijs::Value(42.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.addConstant(minijs::Value(42.0));
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Constant);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("branch", {}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
}

void testJitPairedPushExpansionAndObjectIdentity() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function pair(value) { return [value, 1]; }
      function grow(depth) {
        let x = depth + 1;
        if (depth > 0) { return grow(depth - 1) + x; }
        return x;
      }
      pair(0);
      grow(2);
      let object = { n: 7 };
      [pair(object)[0] == object, pair("x")[0], grow(80), object.n];
    )");
    EXPECT(result.toString() == "[true, x, 3321, 7]");
  }
}

void testBaselineStackSyncPreservesSingleMultipleAndEmptyShrink() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function syncTest() { return 0; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const std::size_t initial = frame->stackSize;
    const minijs::Value number(7.0);
    const minijs::Value text(std::string("kept"));
    const minijs::Value array(std::vector<minijs::Value>{minijs::Value(9.0)});
    EXPECT(minijsBaselinePush(frame, &number));
    EXPECT(minijsBaselinePush(frame, &text));
    EXPECT(minijsBaselinePush(frame, &array));
    // 单槽同步销毁数组栈槽；更早的字符串与数字保持有效。
    --frame->stackSize;
    EXPECT(minijsBaselineSyncStackSize(frame));
    EXPECT(frame->stackSize == initial + 2);
    EXPECT(frame->stackData[frame->stackSize - 1].asString() == "kept");
    EXPECT(frame->stackData[initial].equals(number));
    // 深度不变时，既不丢弃值，也不新增默认 Value。
    EXPECT(minijsBaselineSyncStackSize(frame));
    EXPECT(frame->stackSize == initial + 2);
    frame->stackSize = initial;
    EXPECT(minijsBaselineSyncStackSize(frame));
    EXPECT(frame->stackSize == initial);
    // 通用路径仍允许清空，再正常压入返回值。
    frame->stackSize = 0;
    EXPECT(minijsBaselineSyncStackSize(frame));
    EXPECT(frame->stackSize == 0);
    EXPECT(minijsBaselinePush(frame, &number));
    --frame->stackSize;
    EXPECT(minijsBaselineSyncStackSize(frame));
    EXPECT(frame->stackSize == 0);
    EXPECT(minijsBaselinePush(frame, &number));
    EXPECT(minijsBaselineReturn(frame));
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("syncTest", {}, entry)
             .equals(minijs::Value(7.0)));
}

void testBaselineStackSyncPreservesOversizeError() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function syncTest() { return 0; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    ++frame->stackSize;
    EXPECT(!minijsBaselineSyncStackSize(frame));
    EXPECT(frame->failed);
    EXPECT(frame->errorMessage == "baseline stack size out of bounds");
  };
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("syncTest", {}, entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline stack size out of bounds");
  }
}

void testNativeConstantBinaryFusionNumbersAndComparisons() {
#if !defined(_WIN32) || !(defined(_M_X64) || defined(__x86_64__))
  return; // 手工模板的数字常量只在本次 Windows x64 内联后端嵌入机器码。
#endif
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function apply(value) { return value; }");
  vm.setBaselineHelperProfilingEnabled(true);
  const std::vector<double> numbers = {-0.0, 2.0, -3.0,
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::denorm_min()};
  for (const auto op : {minijs::Opcode::Add, minijs::Opcode::Sub, minijs::Opcode::Mul,
                        minijs::Opcode::Div, minijs::Opcode::Equal,
                        minijs::Opcode::Greater, minijs::Opcode::Less}) {
    for (const double right : numbers) {
      if (op == minijs::Opcode::Div && right == 0.0) continue;
      minijs::BytecodeFunction function;
      function.chunk.addConstant(minijs::Value(right));
      function.chunk.writeOpcode(minijs::Opcode::GetLocal);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Constant);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(op);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      for (const double left : numbers) {
        const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
            "apply", {minijs::Value(left)}, compiled.code->entry);
        if (op == minijs::Opcode::Equal || op == minijs::Opcode::Greater || op == minijs::Opcode::Less) {
          const bool expected = op == minijs::Opcode::Equal ? left == right
                                : op == minijs::Opcode::Greater ? left > right : left < right;
          EXPECT(result.equals(minijs::Value(expected)));
        } else {
          const double expected = op == minijs::Opcode::Add ? left + right
                                  : op == minijs::Opcode::Sub ? left - right
                                  : op == minijs::Opcode::Mul ? left * right : left / right;
          EXPECT(result.isNumber());
          EXPECT(std::isnan(expected) ? std::isnan(result.asNumber()) : result.asNumber() == expected);
          if (expected == 0.0) EXPECT(std::signbit(result.asNumber()) == std::signbit(expected));
        }
      }
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselineSyncStackSize");
    EXPECT(helper.name != "minijsBaselineGetLocalAndPushNumberSlot");
  }
#endif
}

void testConstantBinaryFusionPreservesFallbackErrorsAndBranchEntry() {
  for (const std::string source : {
      "function f(x) { return x - 2; } f(1); f(\"bad\");",
      "function f(x) { return x / 0; } f(1);",
      "function f(x) { return x / -0; } f(1);"}) {
    std::string expectedError;
    for (const bool enabled : {false, true}) {
      minijs::VM vm;
      vm.setJitEnabled(enabled);
      vm.setJitCallThreshold(0); // 首次调用也进入 baseline，避免错误发生在预热阶段。
      try {
        runBytecodeProgramOnVm(vm, source);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        if (!enabled) expectedError = error.what();
        else EXPECT(expectedError == error.what());
      }
    }
  }
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function eq(x) { return x == 2; }
      function add(x) { return x + 2; }
      eq(0);
      add(0);
      [eq("2"), eq(true), eq(null), eq({}), eq(2), add("x")];
    )");
    EXPECT(result.toString() == "[false, false, false, false, true, x2]");
  }
#if !defined(_WIN32) || !(defined(_M_X64) || defined(__x86_64__))
  return;
#endif
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry() { return 0; }");
  minijs::BytecodeFunction function;
  for (const double value : {6.0, 2.0, 99.0}) function.chunk.addConstant(minijs::Value(value));
  function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0); function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::Sub);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[9]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("entry", {}, compiled.code->entry)
             .equals(minijs::Value(4.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.addConstant(minijs::Value(2.0));
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Constant); invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Add);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("entry", {}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: bytecode stack underflow");
  }
}

void testComparisonBranchFusionKeepsLogicalValuesAndSideEffects() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      let probes = 0;
      function touch(value) { probes = probes + 1; return value; }
      function constant(x) { return [(x < 2) && touch(9), (x < 2) || touch(8)]; }
      function dynamic(x, y) { return [(x < y) && touch(9), (x < y) || touch(8)]; }
      function eq(x) { return (x == 2) || object; }
      let object = { n: 7 };
      constant(0);
      dynamic(0, 2);
      eq(0);
      probes = 0;
      [constant(0), constant(3), dynamic(0, 2), dynamic(3, 2),
       probes, eq("2") == object, eq(2), object.n];
    )");
    EXPECT(result.toString() == "[[9, true], [false, 8], [9, true], [false, 8], 4, true, true, 7]");
  }
}

void testComparisonBranchFusionMatchesDecodedWithNaNAndFallback() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, R"(
    function constantLess(x) { if (x < 2) { return 1; } return 0; }
    function dynamicLess(x, y) { if (x < y) { return 1; } return 0; }
    function constantEqual(x) { if (x == 2) { return 1; } return 0; }
    function dynamicGreater(x, y) { if (x > y) { return 1; } return 0; }
    constantLess(0); dynamicLess(0, 2); constantEqual(0); dynamicGreater(0, 2);
  )");
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const std::string name : {"constantLess", "dynamicLess", "constantEqual", "dynamicGreater"}) {
    const auto* code = vm.debugGlobalFunctionBaselineCode(name);
    bool found = false;
    for (std::size_t i = 0; i + 1 < code->instructions.size(); ++i) {
      const auto& instruction = code->instructions[i];
      const auto& next = code->instructions[i + 1];
      if ((instruction.opcode == minijs::Opcode::Equal || instruction.opcode == minijs::Opcode::Less ||
           instruction.opcode == minijs::Opcode::Greater) && next.opcode == minijs::Opcode::JumpIfFalse) {
        found = true;
        EXPECT(code->bytecodeOffsetToNativeOffset[instruction.offset] ==
               code->bytecodeOffsetToNativeOffset[next.offset]);
      }
    }
    EXPECT(found);
  }
#endif
  for (const double value : {-0.0, 2.0, 3.0, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
    for (const std::string name : {"constantLess", "dynamicLess", "constantEqual", "dynamicGreater"}) {
      const bool dynamic = name == "dynamicLess" || name == "dynamicGreater";
      const std::vector<minijs::Value> args = dynamic
          ? std::vector<minijs::Value>{minijs::Value(value), minijs::Value(2.0)}
          : std::vector<minijs::Value>{minijs::Value(value)};
      const auto expected = vm.debugExecuteGlobalFunctionBaseline(name, args);
      if (nativeBaselineBackendAvailable()) {
        const auto* code = vm.debugGlobalFunctionBaselineCode(name);
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(name, args, code->entry).equals(expected));
      }
    }
  }
  if (nativeBaselineBackendAvailable()) {
    const auto* code = vm.debugGlobalFunctionBaselineCode("constantEqual");
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
               "constantEqual", {minijs::Value(std::string("2"))}, code->entry)
               .equals(minijs::Value(0.0)));
  }
  for (const std::string source : {
      "function f(x) { if (x < 2) { return 1; } return 0; } f(\"bad\");",
      "function f(x,y) { if (x < y) { return 1; } return 0; } f(\"bad\",2);"}) {
    std::string expectedError;
    for (const bool enabled : {false, true}) {
      minijs::VM errorVm;
      errorVm.setJitEnabled(enabled);
      errorVm.setJitCallThreshold(0);
      try {
        runBytecodeProgramOnVm(errorVm, source);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        if (!enabled) expectedError = error.what();
        else EXPECT(expectedError == error.what());
      }
    }
  }
}

void testComparisonBranchFusionPreservesIndependentJumpEntry() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry() { return 0; }");
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(0.0));
  function.chunk.addConstant(minijs::Value(2.0));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(3); // 从 offset 5 直接跳到 offset 8 的 JumpIfFalse。
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Equal);
  function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse);
  function.chunk.writeByte(0);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[8]);
  // 独立条件入口不强制转换原操作数；返回的仍应是 Number 0，而非 Boolean false。
  const auto result = vm.debugExecuteGlobalFunctionBaselineEntry("entry", {}, compiled.code->entry);
  EXPECT(result.isNumber() && result.asNumber() == 0.0);
#endif
}

void testNativeLocalNumberUpdateFusionAndSpecialNumbers() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(value) { return value; }");
  vm.setBaselineHelperProfilingEnabled(true);
  const std::vector<double> numbers = {-0.0, 2.0, -3.0,
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::denorm_min()};
  for (const auto op : {minijs::Opcode::Add, minijs::Opcode::Sub, minijs::Opcode::Mul, minijs::Opcode::Div}) {
    for (const double right : numbers) {
      if (op == minijs::Opcode::Div && right == 0.0) continue;
      minijs::BytecodeFunction function;
      function.chunk.addConstant(minijs::Value(right));
      function.chunk.writeOpcode(minijs::Opcode::GetLocal);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Constant);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(op);
      function.chunk.writeOpcode(minijs::Opcode::SetLocal);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal);
      function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      EXPECT(compiled.code->instructions.size() == 7);
      for (const std::size_t offset : {2, 4, 5, 7}) {
        EXPECT(compiled.code->bytecodeOffsetToNativeOffset[offset] ==
               compiled.code->bytecodeOffsetToNativeOffset[0]);
      }
      for (const double left : numbers) {
        const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
            "update", {minijs::Value(left)}, compiled.code->entry);
        const double expected = op == minijs::Opcode::Add ? left + right
                                : op == minijs::Opcode::Sub ? left - right
                                : op == minijs::Opcode::Mul ? left * right : left / right;
        EXPECT(result.isNumber());
        EXPECT(std::isnan(expected) ? std::isnan(result.asNumber()) : result.asNumber() == expected);
        if (expected == 0.0) EXPECT(std::signbit(result.asNumber()) == std::signbit(expected));
      }
    }
  }
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    // 返回值读取仍需压栈；更新本身不应调用这些 helper。
    EXPECT(helper.name != "minijsBaselinePop");
    EXPECT(helper.name != "minijsBaselineSetLocal");
    EXPECT(helper.name != "minijsBaselineSyncStackSize");
  }
#endif
}

void testLocalNumberUpdatePreservesFallbackRetainedValuesAndCaptures() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function update(value) { value = value + 2; return value; }
      function retained(value) { return value = value + 2; }
      function different(value) { let other = 0; other = value + 2; return [value, other]; }
      function outer() {
        let value = 1;
        function read() { return value; }
        value = value + 2;
        let seen = read();
        value = value * 2;
        return [seen, read];
      }
      update(0); retained(0); different(0); outer();
      let captured = outer();
      [update("x"), retained(3), different(3), captured[0], captured[1]()];
    )");
    EXPECT(result.toString() == "[x2, 5, [3, 5], 3, 6]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "captured[1]();").equals(minijs::Value(6.0)));
  }
  for (const std::string source : {
      "function f(x) { x = x - 2; return x; } f(\"bad\");",
      "function f(x) { x = x / 0; return x; } f(1);",
      "function f(x) { x = x / -0; return x; } f(1);"}) {
    std::string expectedError;
    for (const bool enabled : {false, true}) {
      minijs::VM vm;
      vm.setJitEnabled(enabled);
      vm.setJitCallThreshold(0);
      try {
        runBytecodeProgramOnVm(vm, source);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        if (!enabled) expectedError = error.what();
        else EXPECT(expectedError == error.what());
      }
    }
  }
}

void testLocalNumberUpdateDoesNotFuseIndependentPopEntry() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry(value) { return value; }");
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(2.0));
  function.chunk.addConstant(minijs::Value(42.0));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(7); // 从 offset 5 跳进更新序列末尾 offset 12 的 Pop。
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Add);
  function.chunk.writeOpcode(minijs::Opcode::SetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Pop);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[12]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(7.0)}, compiled.code->entry)
             .equals(minijs::Value(7.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.addConstant(minijs::Value(2.0));
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Constant);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Mul);
  invalid.chunk.writeOpcode(minijs::Opcode::SetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(7.0)}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
#endif
}

void testNativeLocalPairUpdateFusionNumbersAndZeroErrors() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(left,right) { return left; }");
  vm.setBaselineHelperProfilingEnabled(true);
  const std::vector<double> numbers = {-0.0, 2.0, -3.0,
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::denorm_min()};
  for (const auto op : {minijs::Opcode::Add, minijs::Opcode::Sub, minijs::Opcode::Mul, minijs::Opcode::Div}) {
    minijs::BytecodeFunction function;
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(1);
    function.chunk.writeOpcode(op);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal);
    function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    for (const std::size_t offset : {2, 4, 5, 7}) {
      EXPECT(compiled.code->bytecodeOffsetToNativeOffset[offset] == compiled.code->bytecodeOffsetToNativeOffset[0]);
    }
    for (const double left : numbers) {
      for (const double right : numbers) {
        if (op == minijs::Opcode::Div && right == 0.0) continue;
        const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
            "update", {minijs::Value(left), minijs::Value(right)}, compiled.code->entry);
        const double expected = op == minijs::Opcode::Add ? left + right
                                : op == minijs::Opcode::Sub ? left - right
                                : op == minijs::Opcode::Mul ? left * right : left / right;
        EXPECT(result.isNumber());
        EXPECT(std::isnan(expected) ? std::isnan(result.asNumber()) : result.asNumber() == expected);
        if (expected == 0.0) EXPECT(std::signbit(result.asNumber()) == std::signbit(expected));
      }
    }
  }
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselinePop");
    EXPECT(helper.name != "minijsBaselineSetLocal");
    EXPECT(helper.name != "minijsBaselineSyncStackSize");
  }
#endif
}

void testLocalPairUpdateFallbackAliasAndClosedCapture() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function update(a,b) { a = a + b; return [a,b]; }
      function alias(a) { a = a + a; a = a / a; return a; }
      function retained(a,b) { return a = a + b; }
      function different(a,b) { let c = 0; c = a + b; return [a,b,c]; }
      function outer(a,b) { function read() { return [a,b]; } a = a + b; return read; }
      update(0,1); alias(2); retained(0,1); different(0,1); outer(0,1);
      let read = outer(3,4);
      [update("x",2), update(1,"x"), alias(3), retained(3,4), different(3,4), read()];
    )");
    EXPECT(result.toString() == "[[x2, 2], [1x, x], 1, 7, [3, 4, 7], [7, 4]]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").toString() == "[7, 4]");
  }
  for (const std::string source : {
      "function f(a,b) { a = a - b; return a; } f(\"bad\",2);",
      "function f(a,b) { a = a * b; return a; } f(2,\"bad\");",
      "function f(a,b) { a = a / b; return a; } f(1,0);",
      "function f(a,b) { a = a / b; return a; } f(1,-0);"}) {
    std::string expectedError;
    for (const bool enabled : {false, true}) {
      minijs::VM vm;
      vm.setJitEnabled(enabled);
      vm.setJitCallThreshold(0);
      try {
        runBytecodeProgramOnVm(vm, source);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        if (!enabled) expectedError = error.what();
        else EXPECT(expectedError == error.what());
      }
    }
  }
}

void testLocalPairUpdateIndependentEntryAndInvalidRightSlot() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry(left,right) { return left; }");
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(42.0));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(7);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Add);
  function.chunk.writeOpcode(minijs::Opcode::SetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Pop);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[12]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
             "entry", {minijs::Value(7.0),minijs::Value(2.0)}, compiled.code->entry).equals(minijs::Value(7.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Add);
  invalid.chunk.writeOpcode(minijs::Opcode::SetLocal);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(7.0),minijs::Value(2.0)}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
#endif
}

void testNativeLocalPairExpressionsSpecialNumbersAndExpansion() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function expression(a,b) { return a; }");
  const std::vector<double> numbers = {-0.0, 2.0, -3.0,
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::denorm_min()};
  vm.setBaselineHelperProfilingEnabled(true);
  for (const auto op : {minijs::Opcode::Add, minijs::Opcode::Sub, minijs::Opcode::Mul, minijs::Opcode::Div,
                        minijs::Opcode::Equal, minijs::Opcode::Less, minijs::Opcode::Greater}) {
    minijs::BytecodeFunction function;
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(1);
    function.chunk.writeOpcode(op);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[4]);
#endif
    for (const double left : numbers) {
      for (const double right : numbers) {
        if (op == minijs::Opcode::Div && right == 0.0) continue;
        const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
            "expression", {minijs::Value(left), minijs::Value(right)}, compiled.code->entry);
        if (op == minijs::Opcode::Equal || op == minijs::Opcode::Less || op == minijs::Opcode::Greater) {
          const bool expected = op == minijs::Opcode::Equal ? left == right
                                : op == minijs::Opcode::Less ? left < right : left > right;
          EXPECT(result.equals(minijs::Value(expected)));
        } else {
          const double expected = op == minijs::Opcode::Add ? left + right
                                  : op == minijs::Opcode::Sub ? left - right
                                  : op == minijs::Opcode::Mul ? left * right : left / right;
          EXPECT(result.isNumber());
          EXPECT(std::isnan(expected) ? std::isnan(result.asNumber()) : result.asNumber() == expected);
          if (expected == 0.0) EXPECT(std::signbit(result.asNumber()) == std::signbit(expected));
        }
      }
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name != "minijsBaselineSyncStackSize");
#endif
  minijs::BytecodeFunction many;
  for (int i = 0; i < 80; ++i) {
    many.chunk.writeOpcode(minijs::Opcode::GetLocal);
    many.chunk.writeByte(0);
    many.chunk.writeOpcode(minijs::Opcode::GetLocal);
    many.chunk.writeByte(1);
    many.chunk.writeOpcode(minijs::Opcode::Add);
  }
  many.chunk.writeOpcode(minijs::Opcode::Array);
  many.chunk.writeByte(80);
  many.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(many);
  EXPECT(compiled.succeeded());
  const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
      "expression", {minijs::Value(1.0), minijs::Value(2.0)}, compiled.code->entry);
  EXPECT(result.asArray().size() == 80);
  for (const auto& value : result.asArray()) EXPECT(value.equals(minijs::Value(3.0)));
}

void testLocalPairExpressionsKeepSourcesAndFallbackValues() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function calculate(a,b) { return [a+b,a-b,a*b,a/b,a==b,a<b,a>b,a,b]; }
      function add(a,b) { return a+b; }
      function alias(a) { return [a+a,a*a,a/a,a==a,a]; }
      function eq(a,b) { return a==b; }
      calculate(2,1); add(0,1); alias(2); eq(0,1);
      let object = { n: 9 };
      [calculate(6,2), add("x",2), add(1,"x"), alias(3), eq(object,object), object.n];
    )");
    EXPECT(result.toString() == "[[8, 4, 12, 3, false, false, true, 6, 2], x2, 1x, [6, 9, 1, true, 3], true, 9]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "eq(object,object);").equals(minijs::Value(true)));
  }
  for (const std::string source : {
      "function f(a,b) { return a-b; } f(\"bad\",2);",
      "function f(a,b) { return a/b; } f(1,0);",
      "function f(a,b) { return a/b; } f(1,-0);"}) {
    std::string expectedError;
    for (const bool enabled : {false,true}) {
      minijs::VM vm;
      vm.setJitEnabled(enabled);
      vm.setJitCallThreshold(0);
      try {
        runBytecodeProgramOnVm(vm, source);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        if (!enabled) expectedError = error.what();
        else EXPECT(expectedError == error.what());
      }
    }
  }
}

void testLocalPairExpressionPreservesIndependentOperationEntry() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry(a,b) { return a; }");
  minijs::BytecodeFunction function;
  // 先正常压入两个值，跳过另一对局部读取，直接执行 offset 11 的 Sub。
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(4);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Sub);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[11]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
             "entry", {minijs::Value(6.0),minijs::Value(2.0)}, compiled.code->entry).equals(minijs::Value(4.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Add);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(6.0),minijs::Value(2.0)}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
}

void testBooleanConstantAssignmentTypesCapturesAndMethods() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled); vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function yes(value) { value = true; return value; }
      function no(value) { value = false; return value; }
      function retained(value) { return value = false; }
      function outer(value) { function read() { return value; } value = true; return read; }
      class Box { self() { return this; } assign(value) { value = false; return value; } }
      yes(false); no(true); retained(true); outer(false);
      let box = Box(); box.assign(true); let object = {}; let read = outer(object);
      [yes(false),no(true),yes(0),no("old"),yes(null),no(undefined),yes([1]),no(object),
       yes(read),no(box),yes(box.self),retained(object),read(),box.assign(object)];
    )").toString() == "[true, false, true, false, true, false, true, false, true, false, true, false, true, false]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm,"read();").equals(minijs::Value(true)));
  }
}

void testNativeBooleanConstantAssignmentHelpersAndBoundaries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  vm.setJitEnabled(true); vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm,"function yes(value) { value = true; return value; } function no(value) { value = false; return value; } yes(false); no(true);");
  vm.setBaselineHelperProfilingEnabled(true);
  for (const auto* name : {"yes","no"}) {
    const bool expected = std::string_view(name) == "yes";
    const auto* code = vm.debugGlobalFunctionBaselineCode(name);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(name,{minijs::Value(!expected)},code->entry)
               .equals(minijs::Value(expected)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[2]);
    EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[4]);
#endif
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnLocal");
#endif
  vm.resetBaselineHelperProfile();
  const auto* yes = vm.debugGlobalFunctionBaselineCode("yes");
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("yes",{minijs::Value(std::string("old"))},yes->entry)
             .equals(minijs::Value(true)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselineAssignConstant") { EXPECT(helper.calls == 1); found = true; }
    EXPECT(helper.name != "minijsBaselinePop" && helper.name != "minijsBaselinePushConstant");
  }
  EXPECT(found);
#endif
  for (const std::uint8_t target : {std::uint8_t(1),std::uint8_t(255)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(true));
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    try {
      const auto result = vm.debugExecuteGlobalFunctionBaselineEntry("yes",{minijs::Value(false)},compiled.code->entry);
      EXPECT(target == 1 && result.equals(minijs::Value(false)));
    } catch (const minijs::RuntimeError& error) {
      EXPECT(target == 255 && std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
  }
  for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(4)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(true));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[5 + jump]);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("yes",{minijs::Value(false)},compiled.code->entry)
               .equals(minijs::Value(false)));
  }
}

void testBooleanConstantAssignmentAbiErrorOrder() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,"function yes(value) { value = true; return value; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto size = frame->stackSize;
    EXPECT(minijsBaselineAssignConstant(frame,0,0));
    EXPECT(frame->stackSize == size);
    EXPECT(!minijsBaselineAssignConstant(frame,255,255));
    EXPECT(frame->stackSize == size && frame->errorMessage == "constant index out of bounds");
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(!minijsBaselineAssignConstant(frame,0,255));
    EXPECT(frame->stackSize == size + 1 && frame->errorMessage == "local slot out of bounds");
    frame->failed = false; frame->errorMessage.clear(); EXPECT(minijsBaselinePop(frame));
    EXPECT(minijsBaselineReturnLocal(frame,0));
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("yes",{minijs::Value(false)},entry).equals(minijs::Value(true)));
}

void testConstantComparisonBranchSemanticsAndFallback() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled); vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function less(value) { if (value < 2) { return 1; } return 0; }
      function greater(value) { if (value > 2) { return 1; } return 0; }
      function equal(value) { if (value == 2) { return 1; } return 0; }
      function kept(value) { return (value < 2) && "right"; }
      function preserve(value) { if (value < 2) {} return value; }
      function loop() { let i = 0; while (i < 3) { i = i + 1; } return i; }
      class Compare { choose(value) { if (value > 2) { return 1; } return 0; } }
      less(1); greater(1); equal(1); kept(1); preserve(1); loop();
      let compare = Compare(); compare.choose(1);
      [less(1),less(3),greater(3),greater(1),equal(2),equal("2"),equal(false),equal(null),
       equal(undefined),equal({}),kept(3),kept(1),preserve(1),preserve(3),loop(),compare.choose(3)];
    )").toString() == "[1, 0, 1, 0, 1, 0, 0, 0, 0, 0, false, right, 1, 3, 3, 1]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm,"equal(2);").equals(minijs::Value(1.0)));
    try {
      runBytecodeProgramOnVm(vm,"less(\"bad\");"); EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (enabled) {
      const auto* code = vm.debugGlobalFunctionBaselineCode("kept");
      EXPECT(code->bytecodeOffsetToNativeOffset[0] != code->bytecodeOffsetToNativeOffset[8]);
    }
#endif
  }
}

void testNativeConstantComparisonBranchSpecialNumbersAndCounts() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  vm.setJitEnabled(true); vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm,"function branch(value) { if (value == 2) { return 1; } return 0; } branch(1);");
  vm.setBaselineHelperProfilingEnabled(true);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  for (const double constant : {2.0,-0.0,nan,inf,std::numeric_limits<double>::denorm_min()}) {
    for (const auto operation : {minijs::Opcode::Less,minijs::Opcode::Greater,minijs::Opcode::Equal}) {
      minijs::BytecodeFunction function;
      function.chunk.addConstant(minijs::Value(constant));
      function.chunk.addConstant(minijs::Value(1.0)); function.chunk.addConstant(minijs::Value(0.0));
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
      function.chunk.writeOpcode(operation);
      function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(2);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      for (const double left : {-0.0,0.0,1.0,2.0,3.0,nan,inf,-inf}) {
        const bool expected = operation == minijs::Opcode::Less ? left < constant
                            : operation == minijs::Opcode::Greater ? left > constant : left == constant;
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",{minijs::Value(left)},compiled.code->entry)
                   .equals(minijs::Value(expected ? 1.0 : 0.0)));
      }
      for (const std::size_t inner : {2u,4u,5u,8u}) {
        EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[inner]);
      }
    }
  }
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnNumberBits");
  vm.resetBaselineHelperProfile();
  const auto* code = vm.debugGlobalFunctionBaselineCode("branch");
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",{minijs::Value(std::string("2"))},code->entry)
             .equals(minijs::Value(0.0)));
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselineGetLocal" && helper.name != "minijsBaselinePop");
    if (helper.name == "minijsBaselineCompareLocalConstantTruthy") { EXPECT(helper.calls == 1); found = true; }
  }
  EXPECT(found);
#endif
}

void testConstantComparisonBranchAbiErrorsAndIndependentEntries() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,"function branch(value) { if (value < 2) { return 1; } return 0; }");
  EXPECT(minijsBaselineCompareLocalConstantTruthy(nullptr,0,0,0) == 2);
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto size = frame->stackSize;
    const auto less = static_cast<std::uint32_t>(minijs::Opcode::Less);
    EXPECT(minijsBaselineCompareLocalConstantTruthy(frame,0,0,less) == 1);
    EXPECT(frame->stackSize == size);
    EXPECT(minijsBaselineCompareLocalConstantTruthy(frame,0,255,less) == 2);
    EXPECT(frame->stackSize == size + 1 && frame->errorMessage == "constant index out of bounds");
    frame->failed = false; frame->errorMessage.clear(); EXPECT(minijsBaselinePop(frame));
    EXPECT(minijsBaselineCompareLocalConstantTruthy(frame,255,255,less) == 2);
    EXPECT(frame->stackSize == size && frame->errorMessage == "local slot out of bounds");
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(minijsBaselineCompareLocalConstantTruthy(frame,0,0,999) == 2);
    EXPECT(frame->stackSize == size && frame->errorMessage == "unsupported local comparison opcode");
    frame->failed = false; frame->errorMessage.clear(); EXPECT(minijsBaselineReturnLocal(frame,0));
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",{minijs::Value(1.0)},entry).equals(minijs::Value(1.0)));
  if (!nativeBaselineBackendAvailable()) return;
  for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(4),std::uint8_t(5),std::uint8_t(8),std::uint8_t(12)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(2.0)); function.chunk.addConstant(minijs::Value(1.0));
    function.chunk.addConstant(minijs::Value(0.0));
    for (int i = 0; i < 2; ++i) { function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0); }
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Less);
    function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(2);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[7 + jump]);
    vm.resetBaselineHelperProfile(); vm.setBaselineHelperProfilingEnabled(true);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",{minijs::Value(1.0)},compiled.code->entry)
               .equals(minijs::Value(jump == 4 || jump == 12 ? 0.0 : 1.0)));
    if (jump == 12) {
      bool popped = false;
      for (const auto& helper : vm.jitRuntimeStats().helpers) {
        if (helper.name == "minijsBaselinePop") { EXPECT(helper.calls == 1); popped = true; }
      }
      EXPECT(popped);
    }
  }
}

void testComparisonBranchPreservesLoopsShortCircuitAndFallback() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function less(left,right) { if (left < right) { return 1; } return 0; }
      function equal(left,right) { if (left == right) { return 1; } return 0; }
      function kept(left,right) { return (left < right) && "right"; }
      function preserve(left,right) { if (left < right) {} return [left,right]; }
      function captured(left,right) {
        function read() { return left; }
        if (left == right) {} return read;
      }
      function control(limit) {
        let i = 0; let total = 0;
        while (i < limit) {
          i = i + 1; if (i == 2) { continue; }
          total = total + 1; if (i == 3) { break; }
        }
        return total;
      }
      let hits = 0;
      function bump() { hits = hits + 1; return "called"; }
      function discard(left,right) { (left < right) && bump(); return 1; }
      class Compare { choose(left,right) { if (left > right) { return 1; } return 0; } }
      less(1,2); equal(1,2); kept(1,2); control(0); discard(2,1);
      preserve(1,2); captured(1,2);
      let compare = Compare(); compare.choose(1,2);
      let object = {}; let read = captured(object,object);
      discard(2,1); discard(1,2);
      [less(1,2),less(2,1),equal("x","x"),equal(object,object),equal(object,{}),
       equal(false,false),equal(null,null),equal(undefined,undefined),
       kept(2,1),kept(1,2),control(0),control(5),hits,compare.choose(8,2)];
    )").toString() == "[1, 0, 1, 1, 0, 1, 1, 1, false, right, 0, 2, 1, 1]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "equal(object,object);").equals(minijs::Value(1.0)));
    EXPECT(runBytecodeProgramOnVm(vm, "[preserve(1,2),preserve(8,2),read() == object];").toString()
               == "[[1, 2], [8, 2], true]");
    try {
      runBytecodeProgramOnVm(vm, "less(\"bad\",2);");
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (enabled) {
      const auto* code = vm.debugGlobalFunctionBaselineCode("kept");
      EXPECT(code->bytecodeOffsetToNativeOffset[0] != code->bytecodeOffsetToNativeOffset[8]);
    }
#endif
  }
}

void testNativeComparisonBranchSpecialValuesAndMergedHelper() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch(left,right) { if (left < right) { return 1; } return 0; }");
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::pair<double,double> cases[] = {{1,2},{2,1},{2,2},{nan,2},{2,nan},{nan,nan},{0,-0.0},{inf,2},{-inf,inf}};
  vm.setBaselineHelperProfilingEnabled(true);
  for (const auto operation : {minijs::Opcode::Less,minijs::Opcode::Greater,minijs::Opcode::Equal}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(1.0)); function.chunk.addConstant(minijs::Value(0.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
    function.chunk.writeOpcode(operation);
    function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    for (const auto& values : cases) {
      const bool expected = operation == minijs::Opcode::Less ? values.first < values.second
                          : operation == minijs::Opcode::Greater ? values.first > values.second
                                                                : values.first == values.second;
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",
          {minijs::Value(values.first),minijs::Value(values.second)}, compiled.code->entry)
                 .equals(minijs::Value(expected ? 1.0 : 0.0)));
    }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    for (const std::size_t inner : {2u,4u,5u,8u}) {
      EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[inner]);
    }
#endif
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnNumberBits");
#endif
  vm.setJitEnabled(true); vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function stringEqual(left,right) { if (left == right) { return 1; } return 0; } stringEqual(1,2);");
  vm.resetBaselineHelperProfile();
  const auto* code = vm.debugGlobalFunctionBaselineCode("stringEqual");
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("stringEqual",
      {minijs::Value(std::string("x")),minijs::Value(std::string("x"))}, code->entry).equals(minijs::Value(1.0)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselineGetLocal" && helper.name != "minijsBaselinePop");
    if (helper.name == "minijsBaselineCompareLocalsTruthy") { EXPECT(helper.calls == 1); found = true; }
  }
  EXPECT(found);
#endif
}

void testComparisonBranchAbiPreservesTemporarySlotsAndErrors() {
  EXPECT(minijsBaselineCompareLocalsTruthy(nullptr,0,1,0) == 2);
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch(left,right) { return left; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto size = frame->stackSize;
    const auto equal = static_cast<std::uint32_t>(minijs::Opcode::Equal);
    EXPECT(minijsBaselineCompareLocalsTruthy(frame,0,1,equal) == 0);
    EXPECT(frame->stackSize == size && !frame->failed);
    EXPECT(minijsBaselineCompareLocalsTruthy(frame,0,2,equal) == 1);
    EXPECT(frame->stackSize == size);
    EXPECT(minijsBaselineCompareLocalsTruthy(frame,0,255,equal) == 2);
    EXPECT(frame->stackSize == size + 1 && frame->errorMessage == "local slot out of bounds");
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(minijsBaselinePop(frame));
    EXPECT(minijsBaselineCompareLocalsTruthy(frame,255,0,equal) == 2);
    EXPECT(frame->stackSize == size);
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(minijsBaselineCompareLocalsTruthy(frame,0,1,999) == 2);
    EXPECT(frame->stackSize == size && frame->errorMessage == "unsupported local comparison opcode");
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(minijsBaselineReturnLocal(frame,0));
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(1.0),minijs::Value(2.0)}, entry)
             .equals(minijs::Value(1.0)));
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(false),minijs::Value(true)}, entry)
             .equals(minijs::Value(false)));
}

void testBooleanEqualityBranchSemanticsAndFallback() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function equal(left, right) { if (left == right) { return 1; } return 0; }
      function self(value) { if (value == value) { return 1; } return 0; }
      function kept(left, right) { return (left == right) && "right"; }
      function outer(left, right) {
        function read() { return [left, right]; }
        if (left == right) {} return read;
      }
      function loop(left, right) {
        let n = 0;
        while (left == right) { n = n + 1; right = !right; }
        return n;
      }
      let hits = 0;
      function bump() { hits = hits + 1; return true; }
      function discard(left, right) { (left == right) && bump(); return hits; }
      class Compare { equal(left, right) { if (left == right) { return 1; } return 0; } }
      equal(false, true); self(false); kept(false, true); outer(false, true);
      loop(false, true); discard(false, true);
      let compare = Compare(); compare.equal(false, true);
      let object = {}; let read = outer(false, true);
      discard(false, true); discard(false, false);
      [equal(false, false), equal(false, true), equal(true, false), equal(true, true),
       self(false), self(true), equal(false, 0), equal(1, true),
       equal(false, ""), equal(null, undefined), equal("x", "x"),
       equal(object, object), equal(object, {}), kept(false, true), kept(false, false),
       loop(false, false), loop(false, true), hits, compare.equal(true, true), read()];
    )");
    EXPECT(result.toString() ==
        "[1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, false, right, 1, 0, 1, 1, [false, true]]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").toString() == "[false, true]");
    try {
      runBytecodeProgramOnVm(vm, "function less(a,b) { if (a < b) {} } less(1,2); less(false,true);");
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
  }
}

void testNativeBooleanEqualityBranchGuardsAndEntries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm,
      "function equal(left,right) { if (left == right) { return 1; } return 0; } equal(false,true);");
  const auto* code = vm.debugGlobalFunctionBaselineCode("equal");
  vm.setBaselineHelperProfilingEnabled(true);
  for (const bool left : {false, true}) {
    for (const bool right : {false, true}) {
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("equal",
          {minijs::Value(left), minijs::Value(right)}, code->entry)
                 .equals(minijs::Value(left == right ? 1.0 : 0.0)));
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name == "minijsBaselineReturnNumberBits");
  }
  for (const std::size_t inner : {2u, 4u, 5u, 8u}) {
    EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[inner]);
  }
#endif
  vm.resetBaselineHelperProfile();
  for (const auto& values : {std::pair<minijs::Value, minijs::Value>{minijs::Value(false), minijs::Value(0.0)},
                            {minijs::Value(1.0), minijs::Value(true)},
                            {minijs::Value(std::string("x")), minijs::Value(std::string("x"))}}) {
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("equal", {values.first, values.second}, code->entry)
               .equals(minijs::Value(values.first.equals(values.second) ? 1.0 : 0.0)));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselineCompareLocalsTruthy") { EXPECT(helper.calls == 3); found = true; }
    EXPECT(helper.name != "minijsBaselineGetLocal" && helper.name != "minijsBaselinePop");
  }
  EXPECT(found);
#endif
  // 手写中间入口及共享假路径 Pop，不得被五条指令的合并误吞。
  for (const std::uint8_t jump : {std::uint8_t(2), std::uint8_t(4), std::uint8_t(5), std::uint8_t(8), std::uint8_t(12)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(1.0)); function.chunk.addConstant(minijs::Value(0.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Equal);
    function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[7 + jump]);
    vm.resetBaselineHelperProfile();
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("equal",
        {minijs::Value(false), minijs::Value(true)}, compiled.code->entry)
               .equals(minijs::Value(jump == 4 || jump == 12 ? 0.0 : 1.0)));
    if (jump == 12) {
      bool popped = false;
      for (const auto& helper : vm.jitRuntimeStats().helpers) {
        if (helper.name == "minijsBaselinePop") { EXPECT(helper.calls == 1); popped = true; }
      }
      EXPECT(popped);
    }
  }
}

void testComparisonBranchKeepsIndependentEntries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch(left,right) { if (left < right) { return 1; } return 0; }");
  for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(4),std::uint8_t(5),std::uint8_t(8),std::uint8_t(12)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(1.0)); function.chunk.addConstant(minijs::Value(0.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Less);
    function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[7 + jump]);
    vm.resetBaselineHelperProfile();
    vm.setBaselineHelperProfilingEnabled(true);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch",
        {minijs::Value(1.0),minijs::Value(2.0)}, compiled.code->entry)
               .equals(minijs::Value(jump == 2 || jump == 12 ? 0.0 : 1.0)));
    if (jump == 12) {
      bool popped = false;
      for (const auto& helper : vm.jitRuntimeStats().helpers) {
        if (helper.name == "minijsBaselinePop") { EXPECT(helper.calls == 1); popped = true; }
      }
      EXPECT(popped);
    }
  }
}

void testLocalBranchPreservesLoopsShortCircuitAndCaptures() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function loop(flag) { let n = 0; while (flag) { n = n + 1; flag = false; } return n; }
      function control(flag) {
        let i = 0; let total = 0;
        while (flag) {
          i = i + 1; if (i == 2) { continue; }
          total = total + 1; if (i == 3) { break; }
        }
        return total;
      }
      function andValue(value) { return value && "right"; }
      function orValue(value) { return value || "right"; }
      function outer(flag) { function read() { return flag; } if (flag) { flag = false; } return read; }
      let hits = 0;
      function bump() { hits = hits + 1; return "called"; }
      function discard(value) { value && bump(); return 1; }
      class Branch { choose(flag) { if (flag) { return 1; } return 0; } }
      loop(false); control(false); andValue(false); orValue(false); outer(false); discard(false);
      let branch = Branch(); branch.choose(false);
      let read = outer(true); let object = {};
      discard(false); discard(true);
      [loop(true),loop(false),control(true),control(false),andValue(0),andValue(object),
       orValue(object) == object,orValue(""),hits,read(),branch.choose(true),branch.choose(false)];
    )").toString() == "[1, 0, 2, 0, 0, right, true, right, 1, false, 1, 0]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(false)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (enabled) {
      const auto* code = vm.debugGlobalFunctionBaselineCode("andValue");
      EXPECT(code->bytecodeOffsetToNativeOffset[0] != code->bytecodeOffsetToNativeOffset[2]);
    }
#endif
  }
}

void testNativeLocalBranchValuesAndHelperCounts() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function branch(value) { if (value) { return 1; } return 0; } branch(true);");
  if (!nativeBaselineBackendAvailable()) return;
  const auto* code = vm.debugGlobalFunctionBaselineCode("branch");
  vm.setBaselineHelperProfilingEnabled(true);
  for (const bool value : {false,true}) {
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(value)}, code->entry)
               .equals(minijs::Value(value ? 1.0 : 0.0)));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[2]);
  EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[5]);
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnNumberBits");
#endif
  vm.resetBaselineHelperProfile();
  const std::vector<minijs::Value> values = {
      minijs::Value(0.0),minijs::Value(-0.0),minijs::Value(7.0),
      minijs::Value(std::numeric_limits<double>::quiet_NaN()),
      minijs::Value(std::numeric_limits<double>::infinity()),minijs::Value(),minijs::Value::undefined(),
      minijs::Value(std::string("")),minijs::Value(std::string("text")),
      minijs::Value(std::vector<minijs::Value>{}),minijs::Value(std::unordered_map<std::string,minijs::Value>{})};
  for (const auto& value : values) {
    const auto expected = vm.debugExecuteGlobalFunctionBaseline("branch", {value});
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {value}, code->entry).equals(expected));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselineGetLocal" && helper.name != "minijsBaselinePop");
    if (helper.name == "minijsBaselineLocalTruthy") {
      EXPECT(helper.calls == values.size());
      found = true;
    }
  }
  EXPECT(found);
#endif
}

void testLocalTruthyAbiPreservesStackAndErrors() {
  EXPECT(minijsBaselineLocalTruthy(nullptr,0) == 2);
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch(value) { return value; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto size = frame->stackSize;
    EXPECT(minijsBaselineLocalTruthy(frame,0) == 0);
    EXPECT(frame->stackSize == size && !frame->failed);
    EXPECT(!minijsBaselineLocalTruthy(frame,0));
    EXPECT(minijsBaselineLocalTruthy(frame,255) == 2);
    EXPECT(frame->stackSize == size && frame->errorMessage == "local slot out of bounds");
    frame->failed = false; frame->errorMessage.clear();
    EXPECT(minijsBaselineReturnLocal(frame,0));
    EXPECT(minijsBaselineLocalTruthy(frame,0) == 2);
    EXPECT(frame->completed && frame->failed);
    frame->failed = false;
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(false)}, entry)
             .equals(minijs::Value(false)));
}

void testLocalBranchIndependentEntriesAndInvalidSlot() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function branch(value) { if (value) { return 1; } return 0; }");
  for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(5)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(1.0)); function.chunk.addConstant(minijs::Value(0.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); function.chunk.writeByte(0); function.chunk.writeByte(4);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(1);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[5 + jump]);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(false)}, compiled.code->entry)
               .equals(minijs::Value(jump == 2 ? 0.0 : 1.0)));
  }
  // 假分支 Pop 可能也被其它跳转使用，必须保留原始弹栈入口。
  minijs::BytecodeFunction shared;
  shared.chunk.addConstant(minijs::Value(1.0)); shared.chunk.addConstant(minijs::Value(0.0));
  shared.chunk.writeOpcode(minijs::Opcode::GetLocal); shared.chunk.writeByte(0);
  shared.chunk.writeOpcode(minijs::Opcode::Jump); shared.chunk.writeByte(0); shared.chunk.writeByte(9);
  shared.chunk.writeOpcode(minijs::Opcode::GetLocal); shared.chunk.writeByte(0);
  shared.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); shared.chunk.writeByte(0); shared.chunk.writeByte(4);
  shared.chunk.writeOpcode(minijs::Opcode::Pop);
  shared.chunk.writeOpcode(minijs::Opcode::Constant); shared.chunk.writeByte(0);
  shared.chunk.writeOpcode(minijs::Opcode::Return);
  shared.chunk.writeOpcode(minijs::Opcode::Pop);
  shared.chunk.writeOpcode(minijs::Opcode::Constant); shared.chunk.writeByte(1);
  shared.chunk.writeOpcode(minijs::Opcode::Return);
  const auto sharedCode = minijs::BaselineCompiler().compile(shared);
  EXPECT(sharedCode.succeeded());
  EXPECT(sharedCode.code->bytecodeOffsetToNativeOffset[5] != sharedCode.code->bytecodeOffsetToNativeOffset[14]);
  vm.setBaselineHelperProfilingEnabled(true);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(false)}, sharedCode.code->entry)
             .equals(minijs::Value(0.0)));
  bool popped = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselinePop") { EXPECT(helper.calls == 1); popped = true; }
  }
  EXPECT(popped);
  minijs::BytecodeFunction invalid;
  invalid.chunk.addConstant(minijs::Value(1.0)); invalid.chunk.addConstant(minijs::Value(0.0));
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal); invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::JumpIfFalse); invalid.chunk.writeByte(0); invalid.chunk.writeByte(4);
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Constant); invalid.chunk.writeByte(0);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  invalid.chunk.writeOpcode(minijs::Opcode::Pop);
  invalid.chunk.writeOpcode(minijs::Opcode::Constant); invalid.chunk.writeByte(1);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(invalid);
  EXPECT(compiled.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("branch", {minijs::Value(false)}, compiled.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
}

void testComparisonAssignmentPreservesFallbackCapturesAndMethods() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function pair(left,right) {
        let flag = false;
        flag = left < right; let less = flag;
        flag = left > right; let greater = flag;
        flag = left == right;
        return [left,right,less,greater,flag];
      }
      function constant(left) {
        let flag = false;
        flag = left < 2; let less = flag;
        flag = left > 2; let greater = flag;
        flag = left == 2;
        return [left,less,greater,flag];
      }
      function equal(left,right,target) { target = left == right; return target; }
      function retained(left,right) { let flag = false; return flag = left < right; }
      function alias(left,right) { left = left == right; return left; }
      function outer(left,right) {
        let flag = false; function read() { return flag; }
        flag = left < right; return read;
      }
      class Compare { less(left,right) { let flag = false; flag = left < right; return flag; } }
      pair(1,2); constant(1); equal(1,2,false); retained(1,2); alias(1,2); outer(1,2);
      let compare = Compare(); compare.less(1,2);
      let object = {}; let read = outer(1,2);
      [pair(8,2),constant(2),equal("a","a",false),equal(object,object,false),
       equal(object,{},false),equal(1,2,"old"),retained(8,2),alias(2,2),read(),compare.less(8,2)];
    )").toString() == "[[8, 2, false, true, false], [2, false, false, true], true, true, false, false, false, true, true, false]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(true)));
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      let bound = compare.less;
      [equal(false,false,false),equal(null,null,false),equal(undefined,undefined,false),
       equal(read,read,false),equal(compare,compare,false),equal(bound,bound,false)];
    )").toString() == "[true, true, true, true, true, true]");
    try {
      runBytecodeProgramOnVm(vm, "pair(\"bad\",2);");
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
  }
}

void testNativeComparisonAssignmentNumbersAliasesAndErrors() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(left,right,target) { target = left == 2; return target; }");
  auto compileAssign = [](minijs::Opcode operation, bool constant, std::uint8_t right, std::uint8_t target) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(2.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(constant ? minijs::Opcode::Constant : minijs::Opcode::GetLocal);
    function.chunk.writeByte(constant ? 0 : right);
    function.chunk.writeOpcode(operation);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(target < 3 ? target : 0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    return minijs::BaselineCompiler().compile(function);
  };
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::pair<double,double> cases[] = {{8,2},{2,2},{1,2},{nan,2},{2,nan},{nan,nan},{inf,2},{0,-0.0},{-inf,inf}};
  vm.setBaselineHelperProfilingEnabled(true);
  for (const bool constant : {false,true}) {
    for (const auto operation : {minijs::Opcode::Less,minijs::Opcode::Greater,minijs::Opcode::Equal}) {
      const auto compiled = compileAssign(operation,constant,1,2);
      EXPECT(compiled.succeeded());
      for (const auto& values : cases) {
        const double right = constant ? 2.0 : values.second;
        const bool expected = operation == minijs::Opcode::Less ? values.first < right
                            : operation == minijs::Opcode::Greater ? values.first > right : values.first == right;
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {minijs::Value(values.first),minijs::Value(values.second),minijs::Value(false)}, compiled.code->entry)
                   .equals(minijs::Value(expected)));
      }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      for (const std::size_t inner : {2u,4u,5u,7u}) {
        EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[inner]);
      }
#endif
    }
  }
  // 三种目标位置覆盖独立目标、左别名和右别名；两源读完后才允许写入。
  for (const std::uint8_t target : {std::uint8_t(0), std::uint8_t(1), std::uint8_t(2)}) {
    const auto compiled = compileAssign(minijs::Opcode::Equal, false, 1, target);
    EXPECT(compiled.succeeded());
    for (const bool left : {false, true}) {
      for (const bool right : {false, true}) {
        for (const bool oldTarget : {false, true}) {
          EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
              {minijs::Value(left), minijs::Value(right), minijs::Value(oldTarget)}, compiled.code->entry)
                     .equals(minijs::Value(left == right)));
        }
      }
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnLocal");
#endif
  vm.resetBaselineHelperProfile();
  const auto equal = compileAssign(minijs::Opcode::Equal,false,1,2);
  for (const auto& value : {minijs::Value(2.0), minijs::Value(true)}) {
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
        {value, value, minijs::Value(std::string("old"))}, equal.code->entry).equals(minijs::Value(true)));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(vm.jitRuntimeStats().helpers.size() == 5);
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.calls == (helper.name == "minijsBaselineGetLocal" ? 4u : 2u));
  }
#endif
  for (const bool constant : {false,true}) {
    for (const std::uint8_t target : {std::uint8_t(0),std::uint8_t(1),std::uint8_t(3)}) {
      const auto compiled = compileAssign(minijs::Opcode::Equal,constant,1,target);
      EXPECT(compiled.succeeded());
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(2.0),minijs::Value(2.0),minijs::Value(false)}, compiled.code->entry)
                 .equals(target == 3 ? minijs::Value(2.0) : minijs::Value(true)));
    }
    const auto bad = compileAssign(minijs::Opcode::Less,constant,1,255);
    EXPECT(bad.succeeded());
    for (const bool typeError : {false,true}) {
      try {
        vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {typeError ? minijs::Value(std::string("bad")) : minijs::Value(2.0),
             minijs::Value(2.0),minijs::Value(false)}, bad.code->entry);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        EXPECT(std::string_view(error.what()) == (typeError ? "RuntimeError: value is not a number"
                                                          : "RuntimeError: local slot out of bounds"));
      }
    }
  }
  const auto badRight = compileAssign(minijs::Opcode::Equal,false,255,2);
  EXPECT(badRight.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("update",
        {minijs::Value(2.0),minijs::Value(2.0),minijs::Value(false)}, badRight.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
  for (const auto& code : {badRight, compileAssign(minijs::Opcode::Equal, false, 1, 255)}) {
    EXPECT(code.succeeded());
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(false), minijs::Value(true), minijs::Value(false)}, code.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
  }
  // GetLocal 产生的临时右槽和比较结果临时目标仍按原始顺序执行。
  for (const auto& slots : {std::pair<std::uint8_t, std::uint8_t>{3, 2}, {1, 3}}) {
    const auto compiled = compileAssign(minijs::Opcode::Equal, false, slots.first, slots.second);
    EXPECT(compiled.succeeded());
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
        {minijs::Value(false), minijs::Value(true), minijs::Value(false)}, compiled.code->entry)
               .equals(minijs::Value(slots.first == 3)));
  }
}

void testComparisonAssignmentIndependentEntries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(left,right,target) { target = left == 2; return target; }");
  for (const bool constant : {false,true}) {
    for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(4),std::uint8_t(5),std::uint8_t(7)}) {
      minijs::BytecodeFunction function;
      function.chunk.addConstant(minijs::Value(2.0));
      for (const std::uint8_t slot : {std::uint8_t(0),std::uint8_t(1)}) {
        function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(slot);
      }
      function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(constant ? minijs::Opcode::Constant : minijs::Opcode::GetLocal);
      function.chunk.writeByte(constant ? 0 : 1);
      function.chunk.writeOpcode(minijs::Opcode::Equal);
      function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(2);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(2);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      EXPECT(compiled.code->bytecodeOffsetToNativeOffset[7] != compiled.code->bytecodeOffsetToNativeOffset[7 + jump]);
      const auto expected = jump == 5 ? minijs::Value(2.0) : minijs::Value(jump == 2);
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(8.0),minijs::Value(2.0),minijs::Value(false)}, compiled.code->entry).equals(expected));
      if (!constant) {
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {minijs::Value(false), minijs::Value(true), minijs::Value(false)}, compiled.code->entry)
                   .equals(minijs::Value(jump == 2 || jump == 5)));
      }
    }
  }
}

void testBooleanEqualityAssignmentCapturesMethodsAndFallback() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function update(left, right, target) { target = left == right; return target; }
      function preserve(left, right) { let flag = false; flag = left == right; return [left, right, flag]; }
      function retained(left, right) { let flag = false; return flag = left == right; }
      function self(value) { value = value == value; return value; }
      function outer(left, right) {
        let flag = false; function read() { return flag; }
        flag = left == right; return read;
      }
      class Compare { equal(left, right) { let flag = false; flag = left == right; return flag; } }
      update(false, true, false); preserve(false, true); retained(false, true); self(false); outer(false, true);
      let compare = Compare(); compare.equal(false, true);
      let object = { n: 9 }; let read = outer(true, true);
      [update(false, false, false), update(false, true, true), update(true, false, true), update(true, true, false),
       preserve(false, true), preserve(true, true), retained(false, true), retained(false, false),
       self(false), self(true), read(), compare.equal(false, true), compare.equal(true, true),
       update(false, false, object), update(false, true, "old"), update(true, true, null),
       update(true, false, undefined), update(false, false, [1]), update(true, true, read),
       update(false, true, compare), update(false, false, compare.equal), object.n,
       update(false, 0, false), update(1, true, false), update("x", "x", false)];
    )");
    EXPECT(result.toString() ==
        "[true, false, false, true, [false, true, false], [true, true, true], false, true, true, true, true, false, true, true, false, true, false, true, true, false, true, 9, false, false, true]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(true)));
  }
}

void testArithmeticAssignmentDifferentTargetsAndFallback() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function pair(left,right) {
        let target = 0;
        target = left + right; let add = target;
        target = left - right; let sub = target;
        target = left * right; let mul = target;
        target = left / right;
        return [left,right,add,sub,mul,target];
      }
      function constant(left) {
        let target = 0;
        target = left + 2; let add = target;
        target = left - 2; let sub = target;
        target = left * 2; let mul = target;
        target = left / 2;
        return [left,add,sub,mul,target];
      }
      function rightAlias(left,right) { right = left - right; return [left,right]; }
      function changed(left,right) { let target = {}; target = left + right; return target; }
      function retained(left,right) { let target = 0; return target = left + right; }
      function outer(left,right) {
        let target = 0; function read() { return target; }
        target = left + right; return read;
      }
      class Calc { add(left,right) { let target = 0; target = left + right; return target; } }
      pair(8,2); constant(8); rightAlias(8,2); changed(1,2); retained(1,2); outer(1,2);
      let calc = Calc(); calc.add(1,2);
      let read = outer(7,3);
      [pair(8,2),constant(8),rightAlias(8,2),changed("a","b"),changed(4,3),retained(4,3),read(),calc.add(4,3)];
    )").toString() == "[[8, 2, 10, 6, 16, 4], [8, 10, 6, 16, 4], [8, 6], ab, 7, 7, 10, 7]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(10.0)));
    try {
      runBytecodeProgramOnVm(vm, "pair(8,0);");
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
    }
  }
}

void testNativeArithmeticAssignmentTargetsNumbersAndErrors() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(left,right,target) { target = left + 2; return target; }");
  auto compileAssign = [](minijs::Opcode operation, bool constant, std::uint8_t right,
                          std::uint8_t target) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(2.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(constant ? minijs::Opcode::Constant : minijs::Opcode::GetLocal);
    function.chunk.writeByte(constant ? 0 : right);
    function.chunk.writeOpcode(operation);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(target < 3 ? target : 0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    return minijs::BaselineCompiler().compile(function);
  };
  vm.setBaselineHelperProfilingEnabled(true);
  for (const bool constant : {false,true}) {
    for (const auto operation : {minijs::Opcode::Add,minijs::Opcode::Sub,minijs::Opcode::Mul,minijs::Opcode::Div}) {
      for (const std::uint8_t target : {std::uint8_t(0),std::uint8_t(1),std::uint8_t(2)}) {
        const auto compiled = compileAssign(operation, constant, 1, target);
        EXPECT(compiled.succeeded());
        const double expected = operation == minijs::Opcode::Add ? 10.0
                              : operation == minijs::Opcode::Sub ? 6.0
                              : operation == minijs::Opcode::Mul ? 16.0 : 4.0;
        EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {minijs::Value(8.0),minijs::Value(2.0),minijs::Value(0.0)}, compiled.code->entry)
                   .equals(minijs::Value(expected)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
        for (const std::size_t inner : {2u,4u,5u,7u}) {
          EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[inner]);
        }
#endif
      }
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.name == "minijsBaselineReturnLocal");
#endif
  const auto mul = compileAssign(minijs::Opcode::Mul, false, 1, 2);
  const double negativeZero = vm.debugExecuteGlobalFunctionBaselineEntry("update",
      {minijs::Value(-0.0),minijs::Value(2.0),minijs::Value(0.0)}, mul.code->entry).asNumber();
  EXPECT(negativeZero == 0.0 && std::signbit(negativeZero));
  const auto div = compileAssign(minijs::Opcode::Div, false, 1, 2);
  EXPECT(std::isnan(vm.debugExecuteGlobalFunctionBaselineEntry("update",
      {minijs::Value(8.0),minijs::Value(std::numeric_limits<double>::quiet_NaN()),minijs::Value(0.0)}, div.code->entry).asNumber()));
  for (const double zero : {0.0,-0.0}) {
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(8.0),minijs::Value(zero),minijs::Value(0.0)}, div.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
    }
  }
  for (const bool constant : {false,true}) {
    const auto temporary = compileAssign(minijs::Opcode::Add, constant, 1, 3);
    EXPECT(temporary.succeeded());
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
        {minijs::Value(8.0),minijs::Value(2.0),minijs::Value(0.0)}, temporary.code->entry).equals(minijs::Value(8.0)));
    for (const auto& slots : {std::pair<std::uint8_t,std::uint8_t>{1,255}, {255,2}}) {
      if (constant && slots.first == 255) continue;
      const auto invalid = compileAssign(minijs::Opcode::Add, constant, slots.first, slots.second);
      EXPECT(invalid.succeeded());
      try {
        vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {minijs::Value(8.0),minijs::Value(2.0),minijs::Value(0.0)}, invalid.code->entry);
        EXPECT(false);
      } catch (const minijs::RuntimeError& error) {
        EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
      }
    }
    // 即使目标非法，也必须先执行原始运算，不能把目标错误提前。
    const auto badTarget = compileAssign(minijs::Opcode::Sub, constant, 1, 255);
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(std::string("bad")),minijs::Value(2.0),minijs::Value(0.0)}, badTarget.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
  }
}

void testArithmeticAssignmentIndependentEntries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(left,right,target) { target = left + 2; return target; }");
  for (const bool constant : {false,true}) {
    for (const std::uint8_t jump : {std::uint8_t(2),std::uint8_t(5),std::uint8_t(7)}) {
      minijs::BytecodeFunction function;
      function.chunk.addConstant(minijs::Value(2.0));
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(constant ? minijs::Opcode::Constant : minijs::Opcode::GetLocal);
      function.chunk.writeByte(constant ? 0 : 1);
      function.chunk.writeOpcode(minijs::Opcode::Add);
      function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(2);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(2);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[5 + jump]);
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {minijs::Value(8.0),minijs::Value(2.0),minijs::Value(0.0)}, compiled.code->entry)
                 .equals(minijs::Value(jump == 2 ? 10.0 : jump == 5 ? 8.0 : 0.0)));
    }
  }
}

void testLocalUnaryUpdatePreservesFallbackCapturesAndRetainedResults() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function negate(value) { value = -value; return value; }
      function invert(value) { value = !value; return value; }
      function different(value) { let target = "old"; target = -value; return [value,target]; }
      function retained(value) { return value = -value; }
      function outer(value) {
        function read() { return value; }
        value = !value;
        return read;
      }
      function negativeOuter(value) {
        function read() { return value; }
        value = -value;
        return read;
      }
      negate(0); invert(false); different(0); retained(0); outer(false); negativeOuter(0);
      class Unary {
        flip(value) { value = -value; return value; }
        truth(value) { value = !value; return value; }
      }
      let unary = Unary(); unary.flip(0); unary.truth(false);
      let read = outer({ n: 7 });
      let negativeRead = negativeOuter(7);
      [negate(7), invert(true), invert(0), invert(1), invert(""), invert("text"),
       invert(null), invert(undefined), invert([]), invert({}), different(7), retained(7), read(), negativeRead(),
       unary.flip(3), unary.truth(true)];
    )").toString() == "[-7, false, true, false, true, false, true, true, false, false, [7, -7], -7, false, -7, -3, false]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(false)));
    EXPECT(runBytecodeProgramOnVm(vm, "negativeRead();").equals(minijs::Value(-7.0)));
    try {
      runBytecodeProgramOnVm(vm, "negate(\"bad\");");
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
    }
  }
}

void testNativeLocalUnaryUpdatePayloadHelpersAndBoundaries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(source,target) { return target; }");
  auto compileUpdate = [](minijs::Opcode operation, std::uint8_t source, std::uint8_t target) {
    minijs::BytecodeFunction function;
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(source);
    function.chunk.writeOpcode(operation);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(target < 2 ? target : 0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    return minijs::BaselineCompiler().compile(function);
  };
  const auto negate = compileUpdate(minijs::Opcode::Negate, 0, 1);
  const auto invert = compileUpdate(minijs::Opcode::Not, 0, 1);
  EXPECT(negate.succeeded() && invert.succeeded());
  vm.setBaselineHelperProfilingEnabled(true);
  const std::uint64_t patterns[] = {0ULL, 0x8000000000000000ULL, 0x7FF8000000001234ULL,
                                  0x7FF0000000000000ULL, 1ULL};
  for (const auto bits : patterns) {
    double number;
    std::memcpy(&number, &bits, sizeof(number));
    const double result = vm.debugExecuteGlobalFunctionBaselineEntry(
        "update", {minijs::Value(number), minijs::Value(0.0)}, negate.code->entry).asNumber();
    std::uint64_t actual;
    std::memcpy(&actual, &result, sizeof(actual));
    EXPECT(actual == (bits ^ 0x8000000000000000ULL));
  }
  for (const bool value : {false,true}) {
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
        {minijs::Value(value), minijs::Value(false)}, invert.code->entry).equals(minijs::Value(!value)));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  for (const auto* code : {negate.code.get(), invert.code.get()}) {
    for (const std::size_t inner : {2u,3u,5u}) {
      EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[inner]);
    }
  }
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name == "minijsBaselineReturnLocal");
  }
#endif
  vm.resetBaselineHelperProfile();
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
      {minijs::Value(7.0), minijs::Value(std::string("old"))}, negate.code->entry).equals(minijs::Value(-7.0)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(vm.jitRuntimeStats().helpers.size() == 5);
  for (const auto& helper : vm.jitRuntimeStats().helpers) EXPECT(helper.calls == 1);
#endif
  for (const auto operation : {minijs::Opcode::Negate, minijs::Opcode::Not}) {
    for (const auto& slots : {std::pair<std::uint8_t,std::uint8_t>{0,0}, {0,2}, {255,0}, {0,255}}) {
      const auto compiled = compileUpdate(operation, slots.first, slots.second);
      EXPECT(compiled.succeeded());
      try {
        const auto result = vm.debugExecuteGlobalFunctionBaselineEntry("update",
            {minijs::Value(7.0), minijs::Value(0.0)}, compiled.code->entry);
        EXPECT(slots.first == 0 && slots.second != 255);
        const auto expected = slots.second == 2 ? minijs::Value(7.0)
                            : operation == minijs::Opcode::Negate ? minijs::Value(-7.0) : minijs::Value(false);
        EXPECT(result.equals(expected));
      } catch (const minijs::RuntimeError& error) {
        EXPECT(slots.first == 255 || slots.second == 255);
        EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
      }
    }
  }
}

void testLocalUnaryUpdateDoesNotFuseIndependentEntries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function update(source,target) { return target; }");
  for (const auto operation : {minijs::Opcode::Negate, minijs::Opcode::Not}) {
    for (const std::uint8_t jump : {std::uint8_t(2), std::uint8_t(3), std::uint8_t(5)}) {
      minijs::BytecodeFunction function;
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
      function.chunk.writeOpcode(operation);
      function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(1);
      function.chunk.writeOpcode(minijs::Opcode::Pop);
      function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(1);
      function.chunk.writeOpcode(minijs::Opcode::Return);
      const auto compiled = minijs::BaselineCompiler().compile(function);
      EXPECT(compiled.succeeded());
      EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[5 + jump]);
      const bool negate = operation == minijs::Opcode::Negate;
      const auto expected = negate ? minijs::Value(jump == 2 ? -7.0 : jump == 3 ? 7.0 : 3.0)
                                   : minijs::Value(jump != 2);
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("update",
          {negate ? minijs::Value(7.0) : minijs::Value(true),
           negate ? minijs::Value(3.0) : minijs::Value(true)}, compiled.code->entry).equals(expected));
    }
  }
}

void testConstantAssignmentPreservesTypesCapturesAndRetainedResult() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    EXPECT(runBytecodeProgramOnVm(vm, R"(
      function assign(value) { value = 7; return value; }
      function retained(value) { return value = 9; }
      function outer(value) {
        function read() { return value; }
        value = 7;
        return read;
      }
      assign(0); retained(0); outer(0);
      class Box { self() { return this; } }
      let box = Box();
      let object = { n: 8 };
      let read = outer(object);
      [assign(false), assign(null), assign(undefined), assign("old"),
       assign([1]), assign(object), assign(read), assign(box), assign(box.self),
       retained(object), read(), object.n];
    )").toString() == "[7, 7, 7, 7, 7, 7, 7, 7, 7, 9, 7, 8]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(7.0)));
  }
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function assign(value) { value = 7; return value; } assign(0);");
  if (!nativeBaselineBackendAvailable()) return;
  const auto* code = vm.debugGlobalFunctionBaselineCode("assign");
  vm.setBaselineHelperProfilingEnabled(true);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("assign", {minijs::Value(0.0)}, code->entry)
             .equals(minijs::Value(7.0)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[2]);
  EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[4]);
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselinePushNumberSlot");
    EXPECT(helper.name != "minijsBaselinePop");
    EXPECT(helper.name != "minijsBaselineAssignConstant");
  }
#endif
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("assign", {minijs::Value(std::string("old"))}, code->entry)
             .equals(minijs::Value(7.0)));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselineAssignConstant") {
      EXPECT(helper.calls == 1);
      found = true;
    }
  }
  EXPECT(found);
#endif
}

void testConstantAssignmentAbiPreservesErrorOrderAndTemporarySlot() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function assign(value) { value = 7; return value; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto size = frame->stackSize;
    EXPECT(minijsBaselineAssignConstant(frame, 0, 1));
    EXPECT(frame->stackSize == size);
    EXPECT(!minijsBaselineAssignConstant(frame, 255, 255));
    EXPECT(frame->stackSize == size);
    EXPECT(frame->errorMessage == "constant index out of bounds");
    frame->failed = false;
    frame->errorMessage.clear();
    EXPECT(!minijsBaselineAssignConstant(frame, 0, 255));
    EXPECT(frame->stackSize == size + 1);
    EXPECT(frame->errorMessage == "local slot out of bounds");
    frame->failed = false;
    frame->errorMessage.clear();
    EXPECT(minijsBaselinePop(frame));
    EXPECT(minijsBaselineReturnLocal(frame, 0));
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("assign", {minijs::Value(3.0)}, entry)
             .equals(minijs::Value(3.0)));
}

void testNativeConstantAssignmentBitsAndIndependentEntries() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function assign(value) { value = 7; return value; }");
  const std::uint64_t bitPatterns[] = {0x8000000000000000ULL, 0x7FF8000000001234ULL,
                                     0x7FF0000000000000ULL, 1ULL};
  for (const auto bits : bitPatterns) {
    double number;
    std::memcpy(&number, &bits, sizeof(number));
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(number));
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    const double actual = vm.debugExecuteGlobalFunctionBaselineEntry(
        "assign", {minijs::Value(0.0)}, compiled.code->entry).asNumber();
    EXPECT(std::memcmp(&number, &actual, sizeof(number)) == 0);
  }
  // guard 拒绝原栈外的目标后，helper 仍允许刚压入的临时槽，真正越界则报错。
  for (const std::uint8_t target : {std::uint8_t(1), std::uint8_t(255)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(7.0));
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    try {
      const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
          "assign", {minijs::Value(3.0)}, compiled.code->entry);
      EXPECT(target == 1 && result.equals(minijs::Value(3.0)));
    } catch (const minijs::RuntimeError& error) {
      EXPECT(target == 255);
      EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
  }
  for (const std::uint8_t jump : {std::uint8_t(2), std::uint8_t(4)}) {
    minijs::BytecodeFunction function;
    function.chunk.addConstant(minijs::Value(7.0));
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Jump); function.chunk.writeByte(0); function.chunk.writeByte(jump);
    function.chunk.writeOpcode(minijs::Opcode::Constant); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal); function.chunk.writeByte(0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    const auto compiled = minijs::BaselineCompiler().compile(function);
    EXPECT(compiled.succeeded());
    EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[5 + jump]);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("assign", {minijs::Value(3.0)}, compiled.code->entry)
               .equals(minijs::Value(3.0)));
  }
#endif
}

void testLocalCopyFusionPreservesValuesAndIdentity() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function copy(value) { let target = 0; target = value; return target; }
      function self(value) { value = value; return value; }
      function retained(value) { let target = 0; return target = value; }
      function outer(value) {
        let target = 0;
        function read() { return target; }
        target = value;
        return read;
      }
      class Box { self() { let target = 0; target = this; return target; } }
      copy(0); self(0); retained(0); outer(0);
      let box = Box(); box.self();
      let object = { n: 8 };
      let read = outer(object);
      [copy(object) == object, copy(read) == read, self(object) == object,
       copy("kept"), copy(false), copy(null), copy(undefined), retained(7),
       read() == object, box.self() == box];
    )");
    EXPECT(result.toString() == "[true, true, true, kept, false, null, undefined, 7, true, true]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read().n;").equals(minijs::Value(8.0)));
  }
}

void testLocalCopyAbiPreservesFailureStackAndTemporaryTarget() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function copy(source,target) { return source; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    const auto originalSize = frame->stackSize;
    EXPECT(minijsBaselineCopyLocal(frame, 0, 2));
    EXPECT(frame->stackSize == originalSize && !frame->failed);
    EXPECT(!minijsBaselineCopyLocal(frame, 255, 0));
    EXPECT(frame->stackSize == originalSize);
    EXPECT(frame->errorMessage == "local slot out of bounds");
    frame->failed = false;
    frame->errorMessage.clear();
    EXPECT(!minijsBaselineCopyLocal(frame, 0, 255));
    // 原顺序先成功 GetLocal 再失败 SetLocal，所以新增槽仍留在栈上。
    EXPECT(frame->stackSize == originalSize + 1);
    EXPECT(frame->errorMessage == "local slot out of bounds");
    frame->failed = false;
    frame->errorMessage.clear();
    EXPECT(minijsBaselinePop(frame));
    EXPECT(minijsBaselineReturnLocal(frame, 0));
  };
  for (const auto& value : {minijs::Value(7.0), minijs::Value(false), minijs::Value(true)}) {
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
        {value, minijs::Value(0.0)}, entry).equals(value));
  }
}

void testNativeLocalCopyPayloadFallbackAndBoundaries() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function copy(source,target) { return source; }");
  auto compileCopy = [](std::uint8_t source, std::uint8_t target) {
    minijs::BytecodeFunction function;
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(source);
    function.chunk.writeOpcode(minijs::Opcode::SetLocal);
    function.chunk.writeByte(target);
    function.chunk.writeOpcode(minijs::Opcode::Pop);
    function.chunk.writeOpcode(minijs::Opcode::GetLocal);
    function.chunk.writeByte(target < 2 ? target : 0);
    function.chunk.writeOpcode(minijs::Opcode::Return);
    return minijs::BaselineCompiler().compile(function);
  };
  const auto compiled = compileCopy(0, 1);
  EXPECT(compiled.succeeded());
  vm.setBaselineHelperProfilingEnabled(true);
  for (const double number : {-0.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(), 7.0}) {
    const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
        "copy", {minijs::Value(number), minijs::Value(0.0)}, compiled.code->entry);
    const double actual = result.asNumber();
    EXPECT(std::memcmp(&number, &actual, sizeof(number)) == 0);
  }
  // false 载荷不是失败状态；同时覆盖真假互相覆盖和相同值复制。
  for (const bool source : {false, true}) {
    for (const bool target : {false, true}) {
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
          {minijs::Value(source), minijs::Value(target)}, compiled.code->entry)
                 .equals(minijs::Value(source)));
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[2]);
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[0] == compiled.code->bytecodeOffsetToNativeOffset[4]);
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselineCopyLocal");
    EXPECT(helper.name != "minijsBaselinePushNumberSlot");
    EXPECT(helper.name != "minijsBaselinePop");
  }
#endif
  const std::vector<minijs::Value> values = {
      minijs::Value(false), minijs::Value(), minijs::Value::undefined(),
      minijs::Value(std::string("kept")),
      minijs::Value(std::vector<minijs::Value>{minijs::Value(9.0)}),
      minijs::Value(std::unordered_map<std::string,minijs::Value>{{"n",minijs::Value(7.0)}})};
  for (const auto& value : values) {
    const auto result = vm.debugExecuteGlobalFunctionBaselineEntry(
        "copy", {value, minijs::Value(0.0)}, compiled.code->entry);
    EXPECT(result.toString() == value.toString());
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    if (helper.name == "minijsBaselineCopyLocal") {
      found = true;
      EXPECT(helper.calls == values.size());
    }
  }
  EXPECT(found);
#endif
  for (const std::uint8_t target : {std::uint8_t(0), std::uint8_t(1), std::uint8_t(2)}) {
    const auto code = compileCopy(0, target);
    EXPECT(code.succeeded());
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
        {minijs::Value(7.0), minijs::Value(std::string("old"))}, code.code->entry).equals(minijs::Value(7.0)));
    for (const bool source : {false, true}) {
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
          {minijs::Value(source), minijs::Value(std::string("old"))}, code.code->entry)
                 .equals(minijs::Value(source)));
    }
  }
  for (const auto& slots : {std::pair<std::uint8_t,std::uint8_t>{255, 0}, {0, 255}}) {
    const auto code = compileCopy(slots.first, slots.second);
    EXPECT(code.succeeded());
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("copy",
          {minijs::Value(7.0), minijs::Value(0.0)}, code.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
    try {
      vm.debugExecuteGlobalFunctionBaselineEntry("copy",
          {minijs::Value(false), minijs::Value(true)}, code.code->entry);
      EXPECT(false);
    } catch (const minijs::RuntimeError& error) {
      EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
    }
  }
  // 跳转直接进入 SetLocal 或 Pop：不能把未执行的读取/赋值融合进去。
  for (const std::uint8_t jump : {std::uint8_t(2), std::uint8_t(4)}) {
    minijs::BytecodeFunction branch;
    branch.chunk.writeOpcode(minijs::Opcode::GetLocal); branch.chunk.writeByte(0);
    branch.chunk.writeOpcode(minijs::Opcode::Jump); branch.chunk.writeByte(0); branch.chunk.writeByte(jump);
    branch.chunk.writeOpcode(minijs::Opcode::GetLocal); branch.chunk.writeByte(0);
    branch.chunk.writeOpcode(minijs::Opcode::SetLocal); branch.chunk.writeByte(1);
    branch.chunk.writeOpcode(minijs::Opcode::Pop);
    branch.chunk.writeOpcode(minijs::Opcode::GetLocal); branch.chunk.writeByte(1);
    branch.chunk.writeOpcode(minijs::Opcode::Return);
    const auto code = minijs::BaselineCompiler().compile(branch);
    EXPECT(code.succeeded());
    EXPECT(code.code->bytecodeOffsetToNativeOffset[5] != code.code->bytecodeOffsetToNativeOffset[5 + jump]);
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
        {minijs::Value(7.0), minijs::Value(2.0)}, code.code->entry).equals(minijs::Value(jump == 2 ? 7.0 : 2.0)));
    EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("copy",
        {minijs::Value(false), minijs::Value(true)}, code.code->entry)
               .equals(minijs::Value(jump != 2)));
  }
}

void testBooleanLocalCopyCapturesMethodsAndTypeChanges() {
  for (const bool enabled : {false, true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function copy(source, target) { target = source; return target; }
      function self(value) { value = value; return value; }
      function retained(source, target) { return target = source; }
      function outer(source) {
        let target = true;
        function read() { return target; }
        target = source;
        return read;
      }
      class Box { copy(source, target) { target = source; return target; } }
      copy(false, true); self(false); retained(false, true); outer(false);
      let box = Box(); box.copy(false, true);
      let object = { n: 9 };
      let read = outer(false);
      [copy(false, true), copy(true, false), self(false), self(true),
       retained(false, true), read(), box.copy(false, true),
       copy(false, object), copy(object, true) == object,
       copy(7, false), copy(false, 7), copy(true, "old"),
       copy(false, null), copy(false, undefined), copy(false, [1]),
       copy(false, read), copy(false, box), copy(false, box.copy), object.n];
    )");
    EXPECT(result.toString() ==
        "[false, true, false, true, false, false, false, false, true, 7, false, true, false, false, false, false, false, false, 9]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read();").equals(minijs::Value(false)));
  }
}

void testNativeLocalReturnPreservesValuesAndUsesMergedHelper() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, "function identity(value) { return value; } identity(0);");
  const auto* code = vm.debugGlobalFunctionBaselineCode("identity");
  const std::vector<minijs::Value> values = {
      minijs::Value(-0.0), minijs::Value(std::numeric_limits<double>::quiet_NaN()),
      minijs::Value(std::numeric_limits<double>::infinity()), minijs::Value(true), minijs::Value(false),
      minijs::Value(), minijs::Value::undefined(), minijs::Value(std::string("kept")),
      minijs::Value(std::vector<minijs::Value>{minijs::Value(9.0)}),
      minijs::Value(std::unordered_map<std::string,minijs::Value>{{"n",minijs::Value(7.0)}})};
  vm.setBaselineHelperProfilingEnabled(true);
  for (const auto& value : values) {
    const auto expected = vm.debugExecuteGlobalFunctionBaseline("identity", {value});
    if (!nativeBaselineBackendAvailable()) continue;
    const auto actual = vm.debugExecuteGlobalFunctionBaselineEntry("identity", {value}, code->entry);
    if (value.isNumber() && std::isnan(value.asNumber())) {
      EXPECT(std::isnan(actual.asNumber()));
    } else if (value.isArray() || value.isObject()) {
      EXPECT(actual.toString() == expected.toString());
    } else {
      EXPECT(actual.equals(expected));
    }
    if (value.isNumber() && value.asNumber() == 0.0) EXPECT(std::signbit(actual.asNumber()));
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselinePushNumberSlot");
    EXPECT(helper.name != "minijsBaselineGetLocal");
    if (helper.name == "minijsBaselineReturnLocal") {
      found = true;
      EXPECT(helper.calls == values.size());
    }
  }
  EXPECT(found);
  EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[2]);
#endif
}

void testLocalReturnKeepsIdentityClosuresMethodsAndReceiverRule() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      function identity(value) { return value; }
      function outer() { let object = { n: 9 }; function read() { return object; } return read; }
      class Box { init(value) { this.n = value; } self() { return this; } echo(value) { return value; } }
      identity(0); outer();
      let box = Box(7);
      box.self(); box.echo(0);
      let object = { n: 8 };
      let read = outer();
      [identity(object) == object, identity(read) == read, read().n,
       box.self() == box, box.echo(object) == object, box.n];
    )");
    EXPECT(result.toString() == "[true, true, 9, true, true, 7]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "read().n;").equals(minijs::Value(9.0)));
  }
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function receiver(a,b) { return a; }");
  const auto entry = +[](minijs::BaselineFrame* frame) {
    frame->returnsReceiver = true;
    EXPECT(minijsBaselineReturnLocal(frame, 1));
    EXPECT(frame->completed && !frame->failed);
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(
             "receiver", {minijs::Value(7.0),minijs::Value(9.0)}, entry).equals(minijs::Value(7.0)));
}

void testLocalReturnIndependentEntryAndInvalidSlot() {
  if (!nativeBaselineBackendAvailable()) return;
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry(value) { return value; }");
  minijs::BytecodeFunction function;
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::GetLocal);
  function.chunk.writeByte(255);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[7]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(7.0)}, compiled.code->entry)
             .equals(minijs::Value(7.0)));
  minijs::BytecodeFunction invalid;
  invalid.chunk.writeOpcode(minijs::Opcode::GetLocal);
  invalid.chunk.writeByte(255);
  invalid.chunk.writeOpcode(minijs::Opcode::Return);
  const auto bad = minijs::BaselineCompiler().compile(invalid);
  EXPECT(bad.succeeded());
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("entry", {minijs::Value(7.0)}, bad.code->entry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: local slot out of bounds");
  }
}

void testNumberReturnKeepsCapturedValuesAndOtherConstantTypes() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      let escaped = undefined;
      function factory() {
        let object = { n: 9 };
        function read() { return object; }
        escaped = read;
        return 42;
      }
      function label() { return "kept"; }
      function flag() { return true; }
      function empty() { return null; }
      function missing() { return; }
      class Box { get() { return 17; } }
      factory(); label(); flag(); empty(); missing();
      let box = Box();
      box.get();
      [factory(), escaped().n, label(), flag(), empty(), missing(), box.get()];
    )");
    EXPECT(result.toString() == "[42, 9, kept, true, null, undefined, 17]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "escaped().n;").equals(minijs::Value(9.0)));
  }
}

void testNumberReturnReceiverAndReturnSlotError() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function receiver(value) { return value; }");
  const auto receiverEntry = +[](minijs::BaselineFrame* frame) {
    frame->returnsReceiver = true;
    EXPECT(minijsBaselineReturnNumberBits(frame, 0x4045000000000000ULL)); // double 42
    EXPECT(frame->completed && !frame->failed);
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("receiver", {minijs::Value(7.0)}, receiverEntry)
             .equals(minijs::Value(7.0)));
  const auto invalidEntry = +[](minijs::BaselineFrame* frame) {
    frame->returnSlot = frame->stackSize + 1;
    EXPECT(!minijsBaselineReturnNumberBits(frame, 0x4045000000000000ULL));
    EXPECT(frame->failed && !frame->completed);
    EXPECT(frame->errorMessage == "baseline return slot out of bounds");
  };
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("receiver", {minijs::Value(7.0)}, invalidEntry);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline return slot out of bounds");
  }
}

void testNumberReturnPreservesIndependentReturnEntry() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function entry() { return 0; }");
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(7.0));
  function.chunk.addConstant(minijs::Value(99.0));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[7]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("entry", {}, compiled.code->entry).equals(minijs::Value(7.0)));
#endif
}

void testLiteralReturnValuesMergedHelpersAndGcCleanup() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  runBytecodeProgramOnVm(vm, R"(
    function text() { return "kept"; }
    function emptyText() { return ""; }
    function longText() { return "a sufficiently long string constant beyond small string storage"; }
    function yes() { return true; }
    function no() { return false; }
    function nothing() { return null; }
    function missing() { return; }
    function number() { return 1; }
    text(); emptyText(); longText(); yes(); no(); nothing(); missing(); number();
  )");
  vm.setBaselineHelperProfilingEnabled(true);
  for (const std::string name : {"text", "emptyText", "longText", "yes", "no", "nothing", "missing", "number"}) {
    const auto expected = vm.debugExecuteGlobalFunctionBaseline(name, {});
    if (nativeBaselineBackendAvailable()) {
      const auto* code = vm.debugGlobalFunctionBaselineCode(name);
      EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry(name, {}, code->entry).equals(expected));
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      EXPECT(code->bytecodeOffsetToNativeOffset[0] == code->bytecodeOffsetToNativeOffset[2]);
#endif
    }
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  bool found = false;
  for (const auto& helper : vm.jitRuntimeStats().helpers) {
    EXPECT(helper.name != "minijsBaselinePushConstant");
    EXPECT(helper.name != "minijsBaselinePushNumberSlot");
    EXPECT(helper.name != "minijsBaselineReturn");
    if (helper.name == "minijsBaselineReturnConstant") {
      found = true;
      EXPECT(helper.calls == 7);
    }
    if (helper.name == "minijsBaselineReturnNumberBits") EXPECT(helper.calls == 1);
  }
  EXPECT(found);
#endif
  if (nativeBaselineBackendAvailable()) {
    vm.collectGarbage();
    const auto before = vm.objectCount();
    const auto* code = vm.debugGlobalFunctionBaselineCode("longText");
    for (int i = 0; i < 100; ++i) {
      const auto value = vm.debugExecuteGlobalFunctionBaselineEntry("longText", {}, code->entry);
      EXPECT(value.asString() == "a sufficiently long string constant beyond small string storage");
      vm.collectGarbage();
      EXPECT(vm.objectCount() == before); // 临时字符串根退出后不能残留。
    }
  }
}

void testLiteralReturnKeepsCapturesMethodsAndStringLifetime() {
  for (const bool enabled : {false,true}) {
    minijs::VM vm;
    vm.setJitEnabled(enabled);
    vm.setJitCallThreshold(1);
    const auto result = runBytecodeProgramOnVm(vm, R"(
      let escaped = undefined;
      function outer() {
        let object = { n: 9 };
        function read() { return object; }
        escaped = read;
        return "saved";
      }
      class Box { label() { return "box"; } flag() { return false; } }
      outer();
      let box = Box();
      box.label(); box.flag();
      let saved = outer();
      let i = 0;
      while (i < 100) { outer(); i = i + 1; }
      [saved, escaped().n, box.label(), box.flag()];
    )");
    EXPECT(result.toString() == "[saved, 9, box, false]");
    vm.collectGarbage();
    EXPECT(runBytecodeProgramOnVm(vm, "[saved, escaped().n];").toString() == "[saved, 9]");
  }
}

void testLiteralReturnErrorsReceiverAndIndependentEntry() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm, "function literal(value) { return \"kept\"; }");
  const auto receiver = +[](minijs::BaselineFrame* frame) {
    frame->returnsReceiver = true;
    EXPECT(minijsBaselineReturnConstant(frame, 0));
    EXPECT(frame->completed && !frame->failed);
  };
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("literal", {minijs::Value(7.0)}, receiver)
             .equals(minijs::Value(7.0)));
  vm.collectGarbage();
  const auto before = vm.objectCount();
  const auto invalidReturn = +[](minijs::BaselineFrame* frame) {
    frame->returnSlot = frame->stackSize + 1;
    EXPECT(!minijsBaselineReturnConstant(frame, 0));
    EXPECT(frame->failed && !frame->completed);
  };
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("literal", {minijs::Value(7.0)}, invalidReturn);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline return slot out of bounds");
  }
  vm.collectGarbage();
  EXPECT(vm.objectCount() == before); // 异常退出同样释放字符串临时根。
  const auto invalidConstant = +[](minijs::BaselineFrame* frame) {
    EXPECT(!minijsBaselineReturnConstant(frame, 255));
    EXPECT(frame->failed && !frame->completed);
  };
  try {
    vm.debugExecuteGlobalFunctionBaselineEntry("literal", {minijs::Value(7.0)}, invalidConstant);
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: constant index out of bounds");
  }
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  minijs::BytecodeFunction function;
  function.chunk.addConstant(minijs::Value(7.0));
  function.chunk.addConstant(minijs::Value(true));
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(0);
  function.chunk.writeOpcode(minijs::Opcode::Jump);
  function.chunk.writeByte(0);
  function.chunk.writeByte(2);
  function.chunk.writeOpcode(minijs::Opcode::Constant);
  function.chunk.writeByte(1);
  function.chunk.writeOpcode(minijs::Opcode::Return);
  runBytecodeProgramOnVm(vm, "function entry() { return 0; }");
  const auto compiled = minijs::BaselineCompiler().compile(function);
  EXPECT(compiled.succeeded());
  EXPECT(compiled.code->bytecodeOffsetToNativeOffset[5] != compiled.code->bytecodeOffsetToNativeOffset[7]);
  EXPECT(vm.debugExecuteGlobalFunctionBaselineEntry("entry", {}, compiled.code->entry).equals(minijs::Value(7.0)));
#endif
}

void testJitDispatchInlineNegateFallbackPreservesTypeError() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function neg(value) { return -value; }
      neg(1);
      neg("x");
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("neg");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchRunsCurrentClosureOpcode() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function makeSelf() {
      function self() { return self; }
      return self;
    }
    let self = makeSelf();
    self();
    self();
  )");

  EXPECT(result.isBytecodeClosure());

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("self");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);

  const minijs::BaselineCode* code = vm.debugGlobalFunctionBaselineCode("self");
  EXPECT(code != nullptr);
  EXPECT(code->entry != nullptr);

  const minijs::Value entryResult =
      vm.debugExecuteGlobalFunctionBaselineEntry("self", {}, code->entry);
  EXPECT(entryResult.isBytecodeClosure());
}

void testJitDispatchInlineAddRunsNumberFastPath() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function add(left, right) { return left + right; }
    add(1, 2);
    add(10, 32);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("add");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineAddFallbackPreservesStringConcat() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value leftString = runBytecodeProgramOnVm(vm, R"(
    function add(left, right) { return left + right; }
    add(1, 2);
    add("x", 1);
  )");

  EXPECT(leftString.isString());
  EXPECT(leftString.toString() == "x1");

  const minijs::Value rightString = runBytecodeProgramOnVm(vm, R"(
    add(1, "x");
  )");

  EXPECT(rightString.isString());
  EXPECT(rightString.toString() == "1x");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("add");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount >= 3);
  EXPECT(feedback->baselineEntryCount >= 2);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineSubRunsNumberFastPath() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function sub(left, right) { return left - right; }
    sub(5, 2);
    sub(10, 3);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 7);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("sub");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineSubFallbackPreservesTypeError() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function sub(left, right) { return left - right; }
      sub(5, 2);
      sub("x", 1);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("sub");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineMulRunsNumberFastPath() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function mul(left, right) { return left * right; }
    mul(3, 4);
    mul(6, 7);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 42);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("mul");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineMulFallbackPreservesTypeError() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function mul(left, right) { return left * right; }
      mul(3, 4);
      mul("x", 2);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("mul");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineDivRunsNumberFastPath() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function div(left, right) { return left / right; }
    div(20, 2);
    div(21, 3);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 7);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("div");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineDivFallbackPreservesDivisionByZero() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function div(left, right) { return left / right; }
      div(20, 2);
      div(1, 0);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: division by zero");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("div");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchInlineDivFallbackPreservesTypeError() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function div(left, right) { return left / right; }
      div(20, 2);
      div("x", 1);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("div");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchGuardedModRunsNumberHelper() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function mod(left, right) { return left % right; }
    mod(20, 6);
    mod(22, 5);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 2);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("mod");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchGuardedModFallbackPreservesModuloByZero() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function mod(left, right) { return left % right; }
      mod(20, 6);
      mod(1, 0);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: modulo by zero");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("mod");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchGuardedModFallbackPreservesTypeError() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  try {
    runBytecodeProgramOnVm(vm, R"(
      function mod(left, right) { return left % right; }
      mod(20, 6);
      mod("x", 1);
    )");
    EXPECT(false);
  } catch (const minijs::RuntimeError& error) {
    EXPECT(std::string_view(error.what()) == "RuntimeError: value is not a number");
  }

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("mod");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchRunsAfterManualGc() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  runBytecodeProgramOnVm(vm, R"(
    function label() { return "hot"; }
    label();
  )");

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("label");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 0);

  vm.collectGarbage();

  const minijs::Value result = runBytecodeProgramOnVm(vm, "label();");
  EXPECT(result.isString());
  EXPECT(result.toString() == "hot");

  feedback = vm.debugGlobalFunctionJitFeedback("label");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->baselineEntryCount == 1);
}

void testJitDispatchUsesMethodSlotStart() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Box {
      self() { return this; }
    }
    let box = Box();
    box.self();
    box.self();
  )");

  EXPECT(result.isBytecodeInstance());

  const minijs::JitFeedback* feedback =
      vm.debugGlobalClassMethodJitFeedback("Box", "self");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}

void testJitDispatchPreservesInitReceiverReturn() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    class Box {
      init() { return 999; }
    }
    Box();
    Box();
  )");

  EXPECT(result.isBytecodeInstance());

  const minijs::JitFeedback* feedback =
      vm.debugGlobalClassMethodJitFeedback("Box", "init");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 2);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->state == minijs::JitState::Compiled);
}
}  // namespace

void runBytecodeTests() {
  testChunkInlineCacheStartsEmptyAfterCopyOrMove();
  testCompileNumberExpression();
  testCompileArithmeticExpression();
  testCompileUnaryMinus();
  testCompileModuloExpression();
  testCompileBooleanNullUndefinedLiterals();
  testCompileStringLiteral();
  testBytecodeStringConstantUsesGcObject();
  testBytecodeNonStringConstantDoesNotAllocateGcObject();
  testBytecodeGcCollectsUnreachableStringConstant();
  testBytecodeGcKeepsGlobalString();
  testBytecodeGcKeepsStringInGlobalObjectField();
  testBytecodeAutoGcCollectsUnreachableStrings();
  testBytecodeAutoGcKeepsReachableGlobalStrings();
  testBytecodeArrayLiteralUsesGcObject();
  testBytecodeGcKeepsStringInGcArray();
  testBytecodeAutoGcKeepsArrayElementsDuringAllocation();
  testBytecodeGcCollectsUnreachableArrayAndString();
  testBytecodeVmRootShapeIsInternalObject();
  testBytecodeObjectLiteralUsesGcObject();
  testBytecodeGcKeepsStringInGcObject();
  testBytecodeGcKeepsDynamicShapeSlotValue();
  testBytecodeGcKeepsDictionaryModeObjectValue();
  testBytecodeGcCollectsUnreachableObjectAndString();
  testBytecodeAutoGcKeepsObjectPropertiesDuringAllocation();
  testBytecodeGcMarksNestedObjectGraph();
  testBytecodeManualGcIsIdempotent();
  testBytecodeAutoGcPressureKeepsReachableObjectGraph();
  testBytecodeAutoGcPressureKeepsClosureClassGraph();
  testBytecodeGcMarksClosedUpvalueValue();
  testBytecodeClosureReturnsCompatibleValue();
  testBytecodeGlobalClosureKeepsGcObjectGraph();
  testBytecodeGcCollectsUnreachableClosure();
  testBytecodeUpvalueReturnsCompatibleClosureValue();
  testBytecodeGcCollectsUnreachableUpvalue();
  testBytecodeGcMarksClassMethodClosureUpvalues();
  testBytecodeClassReturnsCompatibleValue();
  testBytecodeGcCollectsUnreachableClass();
  testBytecodeClassInstanceReturnsCompatibleValue();
  testBytecodeGcKeepsStringInInstanceField();
  testBytecodeGcCollectsUnreachableInstanceAndField();
  testBytecodeGcBoundMethodKeepsReceiverInstance();
  testBytecodeBoundMethodReturnsCompatibleValue();
  testBytecodeGcBoundMethodKeepsReceiverAfterOriginalVariableCleared();
  testBytecodeGcMarksInstanceFieldObjectGraph();
  testCompileStringConcatenation();
  testCompileComparisonExpressions();
  testCompileLogicalNot();
  testCompileLogicalAndShortCircuit();
  testCompileLogicalOrShortCircuit();
  testCompileLogicalReturnsOperandValue();
  testCompileLogicalWithBuiltins();
  testDecodeInstructionReadsFeedbackOperands();
  testDecodeInstructionReadsSuperCallWithoutFeedbackOperand();
  testVerifyBytecodeAcceptsCompiledChunk();
  testVerifyBytecodeRejectsTruncatedInstruction();
  testVerifyBytecodeRejectsMismatchedFeedbackSlotKind();
  testVerifyBytecodeRejectsJumpIntoOperandBytes();
  testDisassembleArithmeticExpression();
  testDisassembleUnaryMinus();
  testDisassembleStringLiteral();
  testDisassembleBooleanNullUndefinedLiterals();
  testDisassembleLogicalAndJump();
  testDisassembleLogicalOrJump();
  testDisassembleClosureCapturesOuterLocal();
  testDisassembleClosureCapturesThroughUpvalue();
  testDisassembleClosureSetsUpvalue();
  testDisassembleClosureClosesBlockLocal();
  testDisassembleClassMethod();
  testDisassembleClassStaticMethod();
  testDisassembleClassInheritance();
  testDisassembleSuperMethodCall();
  testDisassembleBreakClosesUpvalueBeforeJump();
  testDisassembleLocalRecursionGetsCurrentClosure();
  testDisassembleParameterShadowsCurrentClosure();
  testDisassembleWhileBreakJump();
  testDisassembleWhileContinueLoop();
  testDisassembleForContinueJumpsToIncrement();
  testCompileGlobalLet();
  testCompileGlobalExpressionUsesVariable();
  testCompileGlobalAssignment();
  testCompileIfElseStatement();
  testCompileIfWithoutElseStatement();
  testCompileWhileStatement();
  testCompileWhileSkippedStatement();
  testCompileWhileWithNestedIfStatement();
  testCompileForStatement();
  testCompileForSkippedStatement();
  testCompileForWithoutInitializerStatement();
  testCompileWhileBreakStatement();
  testCompileWhileContinueStatement();
  testCompileForBreakStatement();
  testCompileForContinueStatement();
  testCompileBreakContinuePopBlockLocals();
  testCompileBreakOutsideLoop();
  testCompileContinueOutsideLoop();
  testBytecodeFunctionDoesNotInheritOuterBreakContext();
  testBytecodeFunctionDoesNotInheritOuterContinueContext();
  testBytecodeFunctionUsesOwnLoopContext();
  testCompileBlockLocalVariable();
  testCompileNestedBlockLocalVariables();
  testCompileLocalAssignment();
  testCompileLocalShadowsGlobal();
  testCompileDuplicateLocalDeclaration();
  testCompileBytecodeFunctionCall();
  testCompileBytecodeFunctionReturnsArgument();
  testCompileBytecodeFunctionParametersUseLocalSlots();
  testCompileBytecodeFunctionParametersAndLocalsUseSlots();
  testCompileBytecodeFunctionParameterRedeclaration();
  testBytecodeParameterShadowsCurrentFunctionName();
  testBytecodeLocalShadowsCurrentFunctionName();
  testCompileBytecodeFunctionReadsGlobalVariable();
  testCompileBytecodeFunctionAssignsGlobalVariable();
  testBytecodeCallReturnedFunctionDirectly();
  testBytecodeClassMethodCall();
  testBytecodeClassBoundMethodCall();
  testBytecodeClassStaticMethodCall();
  testBytecodeClassStaticFactoryMethod();
  testBytecodeClassStaticAndInstanceMethodsAreSeparate();
  testBytecodeClassInheritsStaticMethod();
  testBytecodeClassOverridesStaticMethod();
  testBytecodeClassInstanceDoesNotSeeStaticMethod();
  testBytecodeClassInstanceCannotCallStaticMethod();
  testBytecodeClassStaticMethodCannotUseThis();
  testBytecodeClassStaticMethodArity();
  testBytecodeClassMethodThisField();
  testBytecodeClassInstanceFieldsAreIndependent();
  testBytecodeClassFieldCanShadowMethod();
  testBytecodeClassMethodVisibleAfterDeletingShadowField();
  testBytecodeClassFieldCanShadowInheritedMethod();
  testBytecodeClassInitInitializesInstance();
  testBytecodeClassInitReturnValueIsIgnored();
  testBytecodeClassInitReturnsInstance();
  testBytecodeClassInitInstancesAreIndependent();
  testBytecodeClassMethodCallsAnotherMethod();
  testBytecodeClassInheritsMethod();
  testBytecodeClassOverridesInheritedMethod();
  testBytecodeClassInheritsInit();
  testBytecodeClassSuperclassMustBeClass();
  testBytecodeClassSuperMethodCall();
  testBytecodeClassSuperMethodUsesCurrentReceiver();
  testBytecodeClassSuperAcrossMultipleLevels();
  testBytecodeClassBoundMethodKeepsSuperBinding();
  testBytecodeClassSuperInitInitializesReceiver();
  testBytecodeClassSuperMissingMethod();
  testBytecodeClassSuperOutsideSubclassMethod();
  testBytecodeSuperOutsideMethod();
  testBytecodeThisOutsideMethod();
  testBytecodeClassMethodUsesDefinitionClosure();
  testBytecodeClassBoundMethodUsesDefinitionClosure();
  testBytecodeClassCallArity();
  testBytecodeClassInitArity();
  testBytecodeClassUnknownMethod();
  testBytecodeClassMissingFieldReturnsUndefined();
  testBytecodeClassMethodArity();
  testBytecodeClassBoundMethodArity();
  testBytecodeClosureKeepsOuterVariable();
  testBytecodeClosureInstancesAreIndependent();
  testBytecodeClosuresShareCapturedVariable();
  testBytecodeSharedUpvalueObservesMultipleWrites();
  testBytecodeClosureCapturesThroughMultipleLevels();
  testBytecodeClosureCapturesBlockLocal();
  testBytecodeBreakClosesCapturedLocal();
  testBytecodeContinueClosesCapturedLocal();
  testCompileBytecodeFunctionWithoutReturn();
  testBytecodeFunctionArityMismatch();
  testCompileBytecodeFunctionLocalVariable();
  testCompileBytecodeRecursiveFunction();
  testCompileBytecodeRecursiveFibonacci();
  testBytecodeReturnedLocalFunctionCanRecurse();
  testBytecodeReturnOutsideFunction();
  testBytecodeReturnInsideTopLevelBlock();
  testBytecodeCallNonFunction();
  testBytecodePrintBuiltin();
  testBytecodeBuiltinFunctionCanBeAssigned();
  testBytecodeGcKeepsBuiltinNativeFunction();
  testBytecodePrintArity();
  testBytecodeClockBuiltin();
  testBytecodeClockArity();
  testBytecodeLenArrayBuiltin();
  testBytecodeLenStringBuiltin();
  testBytecodeLenObjectBuiltin();
  testBytecodeLenInstanceBuiltin();
  testBytecodeLenArity();
  testBytecodeLenTypeError();
  testBytecodeTypeOfPrimitiveBuiltins();
  testBytecodeTypeOfContainers();
  testBytecodeTypeOfFunctions();
  testBytecodeTypeOfArity();
  testBytecodeHasBuiltinObjectProperty();
  testBytecodeHasBuiltinInstanceField();
  testBytecodeHasBuiltinArrayAndStringProperty();
  testBytecodeHasArity();
  testBytecodeHasKeyMustBeString();
  testBytecodeDelBuiltinObjectProperty();
  testBytecodeDelBuiltinInstanceField();
  testBytecodeDelBuiltinMissingOrNonObject();
  testBytecodeDelArity();
  testBytecodeDelKeyMustBeString();
  testBytecodeKeysBuiltinObject();
  testBytecodeGcCollectsTemporaryKeyStringsAfterKeysBuiltin();
  testBytecodeKeysBuiltinInstanceFields();
  testBytecodeKeysBuiltinArrayAndString();
  testBytecodeKeysArity();
  testBytecodeUndefinedGlobal();
  testBytecodeAssignUndefinedGlobal();
  testBytecodeProgramWithoutFinalExpressionReturnsNull();
  testDisassembleGlobalLetExpression();
  testDisassembleGlobalAssignment();
  testBytecodeDivisionByZero();
  testBytecodeModuloByZero();
  testCompileArrayLiteralIndex();
  testCompileArrayVariableIndex();
  testCompileArrayExpressionElements();
  testCompileArrayIndexAssignment();
  testCompileArrayReferenceSemantics();
  testBytecodeArrayIndexOutOfBounds();
  testBytecodeArrayIndexMustBeInteger();
  testBytecodeIndexNonArrayValue();
  testCompileObjectLiteralProperty();
  testCompileObjectExpressionProperty();
  testCompileMissingObjectProperty();
  testBytecodeGetPropertyFromNonObject();
  testCompileObjectPropertyAssignment();
  testBytecodeObjectPropertyHelpersPreserveBehavior();
  testBytecodeShapeModeObjectPropertiesPreserveSemantics();
  testBytecodeGetPropertyInlineCacheHitsRepeatedSameShapeAccess();
  testBytecodeGetPropertyInlineCacheMissesAndUpdatesOnShapeChange();
  testBytecodeGetPropertyInlineCacheCachesMultipleShapesAtSameAccessSite();
  testBytecodeGetPropertyInlineCacheBecomesMegamorphicAfterCapacity();
  testBytecodeGetPropertyInlineCacheBypassesDictionaryMode();
  testBytecodeGetPropertyInlineCacheDoesNotUpdateMissingProperty();
  testBytecodeSetPropertyInlineCacheHitsRepeatedExistingPropertyAssignment();
  testBytecodeSetPropertyInlineCacheUpdatesAfterAddingProperty();
  testBytecodeSetPropertyInlineCacheMissesAndUpdatesOnShapeChange();
  testBytecodeSetPropertyInlineCacheCachesMultipleShapesAtSameAccessSite();
  testBytecodeSetPropertyInlineCacheBecomesMegamorphicAfterCapacity();
  testBytecodePolymorphicPropertyInlineCacheSurvivesManualGc();
  testBytecodeSetPropertyInlineCacheBypassesDictionaryMode();
  testBytecodeShapeSetKeepsAssignedValuesDuringGcPressure();
  testBytecodeObjectDeleteFallsBackToDictionarySemantics();
  testBytecodeObjectsWithSamePropertyOrderShareShape();
  testBytecodeObjectsWithDifferentPropertyOrderUseDifferentShapes();
  testBytecodeReassigningExistingPropertyKeepsShape();
  testBytecodeDeletingPropertySwitchesObjectToDictionaryMode();
  testCompileObjectReferenceSemantics();
  testBytecodeArrayLengthProperty();
  testBytecodeStringLengthProperty();
  testBytecodeObjectOwnLengthProperty();
  testBytecodeArrayUnknownProperty();
  testBytecodeArrayPushMethod();
  testBytecodeArrayPopMethod();
  testBytecodeArrayPushArity();
  testBytecodeArrayPopArity();
  testBytecodeMethodCallOnNonArray();
  testBytecodeUnknownArrayMethod();
  testBytecodeMethodCallStatsCountsClassDispatches();
  testBytecodeMethodCallStatsCountsArrayBuiltins();
  testBytecodeMethodCallStatsCountsDispatchErrors();
  testBytecodeMethodCallStatsCountsInstanceArityErrors();
  testBytecodeMethodCallStatsCountsStaticArityErrors();
  testBytecodeMethodCallInlineCacheHitsRepeatedInstanceMethod();
  testBytecodeMethodCallInlineCacheHitsRepeatedStaticMethod();
  testBytecodeMethodCallInlineCacheHitSurvivesManualGc();
  testBytecodePolymorphicMethodCallInlineCacheSurvivesManualGc();
  testBytecodeMethodCallInlineCacheMissesAndUpdatesOnClassChange();
  testDisassembleArrayMethodCall();
  testDisassembleObjectLiteralProperty();
  testBytecodeMethodCallInlineCacheCachesMultipleReceiverClassesAtSameCallSite();
  testBytecodeMethodCallInlineCacheBecomesMegamorphicAfterCapacity();
  testBytecodeMethodCallInlineCacheCachesMultipleStaticClassesAtSameCallSite();
  testBytecodeSuperCallDoesNotPolluteMethodInlineCache();
  testJitCallThresholdCompilesSupportedFunction();
  testJitCallCountStaysColdBelowThreshold();
  testJitBackedgeThresholdCompilesSupportedFunction();
  testJitDisabledDoesNotCollectHeat();
  testJitArityErrorDoesNotCountCall();
  testJitCompilesCallOpcode();
  testJitCompilesMethodCallOpcode();
  testJitCompilesSuperCallOpcode();
  testJitCompilesClosureOpcode();
  testJitCompilesClassFamilyOpcodes();
  testBaselineExecutorRunsArithmeticAndLocals();
  testBaselineExecutorRunsSimpleLoop();
  testBaselineExecutorPreservesArithmeticErrors();
  testBaselineCompilerGeneratesNativeStubForAddFunction();
  testBaselineCompilerGeneratesNativeStubForArithmeticFunction();
  testBaselineCompilerGeneratesNativeStubForDefineGlobalOpcode();
  testBaselineCompilerGeneratesNativeStubForClosureAccessOpcodes();
  testBaselineCompilerGeneratesNativeStubForClassFamilyOpcodes();
#if defined(__aarch64__) || defined(_M_ARM64) || \
    (defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)))
  testBaselineCompilerNativeEntryRunsAddFunction();
  testBaselineCompilerNativeEntryRunsArithmeticFunction();
  testBaselineCompilerNativeEntryRunsEqualFunction();
  testBaselineCompilerNativeEntryRunsComparisonFunctions();
  testBaselineCompilerNativeEntryRunsNotFunction();
  testBaselineCompilerNativeEntryRunsControlFlowFunctions();
  testBaselineCompilerNativeEntryRunsGlobalFunctions();
  testBaselineCompilerNativeEntryRunsArrayAndIndexFunctions();
  testBaselineCompilerNativeEntryRunsObjectAndPropertyFunctions();
  testJitDispatchRunsNativeBaselineEntryForSupportedFunction();
  testJitDispatchRunsNativeBaselineEntryForEqualFunction();
  testJitDispatchRunsNativeBaselineEntryForComparisonFunctions();
  testJitDispatchRunsNativeBaselineEntryForNotFunction();
  testJitDispatchRunsNativeBaselineEntryForControlFlowFunction();
  testJitDispatchRunsNativeBaselineEntryForGlobalFunction();
#endif
  testBaselineRuntimeAbiRunsArithmeticEntry();
  testBaselineRuntimeAbiPushesConstantsAndSetsLocals();
  testBaselineRuntimeAbiPushesValuesByPointer();
  testBaselineRuntimeAbiTurnsHelperErrorsIntoFailedFrame();
  testBaselineRuntimeAbiRejectsInvalidLocalAccess();
  testBaselineRuntimeAbiRejectsMutationAfterReturn();
  testJitDispatchRunsGlobalOpcodes();
  testJitDispatchRunsArrayAndIndexOpcodes();
  testJitDispatchRunsObjectLiteralAndPropertyOpcodes();
  testJitDispatchRunsSetPropertyOpcode();
  testJitDispatchRunsUpvalueOpcodes();
  testJitDispatchRunsCurrentClosureOpcode();
  testJitDispatchEntersCompiledFunctionOnLaterCall();
  testJitDispatchRespectsDisabledJit();
  testJitDispatchPreservesRuntimeErrors();
  testJitDispatchComparisonHelpersPreserveTypeErrors();
  testBaselineNumberComparisonsHandleSpecialValues();
  testJitNumberComparisonsComposeWithControlFlowAndCalls();
  testJitInlineEqualFallbackAndBranching();
  testJitNumberLocalsPreserveTypeChangesAndCapturedSlots();
  testJitNumberLocalsSurviveGrowingRecursiveStack();
  testNativeNumberLocalsPreservePayloadAndRejectInvalidSlots();
  testNativeNumberConstantsPreserveExactBits();
  testJitNumberConstantsComposeWithOtherConstantTypes();
  testDiscardedValuesPreserveSurvivorsAcrossExecutors();
  testDiscardTopPreservesStackUnderflowError();
  testBaselineBooleanBranchesAndTruthyFallback();
  testJitBooleanBranchesPreserveLogicalOperandsAndSideEffects();
  testBooleanBranchFallbackPreservesStackUnderflow();
  testBaselineBooleanNotAndTruthyFallback();
  testJitBooleanNotComposesWithBranchesAndSideEffects();
  testBooleanNotFallbackPreservesStackUnderflow();
  testBaselineHelperProfileIsOptionalAndTracksFallback();
  testNativePairedLocalAndNumberPushPreservesValues();
  testNativePairedPushDoesNotFuseBranchTarget();
  testJitPairedPushExpansionAndObjectIdentity();
  testBaselineStackSyncPreservesSingleMultipleAndEmptyShrink();
  testBaselineStackSyncPreservesOversizeError();
  testNativeConstantBinaryFusionNumbersAndComparisons();
  testConstantBinaryFusionPreservesFallbackErrorsAndBranchEntry();
  testComparisonBranchFusionKeepsLogicalValuesAndSideEffects();
  testComparisonBranchFusionMatchesDecodedWithNaNAndFallback();
  testComparisonBranchFusionPreservesIndependentJumpEntry();
  testNativeLocalNumberUpdateFusionAndSpecialNumbers();
  testLocalNumberUpdatePreservesFallbackRetainedValuesAndCaptures();
  testLocalNumberUpdateDoesNotFuseIndependentPopEntry();
  testNativeLocalPairUpdateFusionNumbersAndZeroErrors();
  testLocalPairUpdateFallbackAliasAndClosedCapture();
  testLocalPairUpdateIndependentEntryAndInvalidRightSlot();
  testNativeLocalPairExpressionsSpecialNumbersAndExpansion();
  testLocalPairExpressionsKeepSourcesAndFallbackValues();
  testLocalPairExpressionPreservesIndependentOperationEntry();
  testNativeLocalReturnPreservesValuesAndUsesMergedHelper();
  testLocalCopyFusionPreservesValuesAndIdentity();
  testConstantAssignmentPreservesTypesCapturesAndRetainedResult();
  testLocalUnaryUpdatePreservesFallbackCapturesAndRetainedResults();
  testArithmeticAssignmentDifferentTargetsAndFallback();
  testComparisonAssignmentPreservesFallbackCapturesAndMethods();
  testLocalBranchPreservesLoopsShortCircuitAndCaptures();
  testComparisonBranchPreservesLoopsShortCircuitAndFallback();
  testConstantComparisonBranchSemanticsAndFallback();
  testBooleanConstantAssignmentTypesCapturesAndMethods();
  testNativeBooleanConstantAssignmentHelpersAndBoundaries();
  testBooleanConstantAssignmentAbiErrorOrder();
  testNativeConstantComparisonBranchSpecialNumbersAndCounts();
  testConstantComparisonBranchAbiErrorsAndIndependentEntries();
  testNativeComparisonBranchSpecialValuesAndMergedHelper();
  testComparisonBranchAbiPreservesTemporarySlotsAndErrors();
  testBooleanEqualityBranchSemanticsAndFallback();
  testNativeBooleanEqualityBranchGuardsAndEntries();
  testComparisonBranchKeepsIndependentEntries();
  testNativeLocalBranchValuesAndHelperCounts();
  testLocalTruthyAbiPreservesStackAndErrors();
  testLocalBranchIndependentEntriesAndInvalidSlot();
  testNativeComparisonAssignmentNumbersAliasesAndErrors();
  testBooleanEqualityAssignmentCapturesMethodsAndFallback();
  testComparisonAssignmentIndependentEntries();
  testNativeArithmeticAssignmentTargetsNumbersAndErrors();
  testArithmeticAssignmentIndependentEntries();
  testNativeLocalUnaryUpdatePayloadHelpersAndBoundaries();
  testLocalUnaryUpdateDoesNotFuseIndependentEntries();
  testConstantAssignmentAbiPreservesErrorOrderAndTemporarySlot();
  testNativeConstantAssignmentBitsAndIndependentEntries();
  testLocalCopyAbiPreservesFailureStackAndTemporaryTarget();
  testNativeLocalCopyPayloadFallbackAndBoundaries();
  testBooleanLocalCopyCapturesMethodsAndTypeChanges();
  testLocalReturnKeepsIdentityClosuresMethodsAndReceiverRule();
  testLocalReturnIndependentEntryAndInvalidSlot();
  testNumberReturnKeepsCapturedValuesAndOtherConstantTypes();
  testNumberReturnReceiverAndReturnSlotError();
  testNumberReturnPreservesIndependentReturnEntry();
  testLiteralReturnValuesMergedHelpersAndGcCleanup();
  testLiteralReturnKeepsCapturesMethodsAndStringLifetime();
  testLiteralReturnErrorsReceiverAndIndependentEntry();
  testJitDispatchInlineNegateFallbackPreservesTypeError();
  testJitDispatchInlineAddRunsNumberFastPath();
  testJitDispatchInlineAddFallbackPreservesStringConcat();
  testJitDispatchInlineSubRunsNumberFastPath();
  testJitDispatchInlineSubFallbackPreservesTypeError();
  testJitDispatchInlineMulRunsNumberFastPath();
  testJitDispatchInlineMulFallbackPreservesTypeError();
  testJitDispatchInlineDivRunsNumberFastPath();
  testJitDispatchInlineDivFallbackPreservesDivisionByZero();
  testJitDispatchInlineDivFallbackPreservesTypeError();
  testJitDispatchGuardedModRunsNumberHelper();
  testJitDispatchGuardedModFallbackPreservesModuloByZero();
  testJitDispatchGuardedModFallbackPreservesTypeError();
  testJitDispatchRunsAfterManualGc();
  testJitDispatchUsesMethodSlotStart();
  testJitDispatchPreservesInitReceiverReturn();
}
