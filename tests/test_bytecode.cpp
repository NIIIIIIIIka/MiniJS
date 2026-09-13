#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

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

void testJitUnsupportedCallOpcodeFailsAndFallsBackToVm() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);

  const minijs::Value result = runBytecodeProgramOnVm(vm, R"(
    function addOne(value) { return value + 1; }
    function callAdd(value) { return addOne(value); }
    callAdd(1);
  )");

  EXPECT(result.isNumber());
  EXPECT(result.asNumber() == 2);

  const minijs::JitFeedback* feedback = vm.debugGlobalFunctionJitFeedback("callAdd");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->callCount == 1);
  EXPECT(feedback->baselineEntryCount == 0);
  EXPECT(feedback->state == minijs::JitState::Failed);
  EXPECT(vm.debugGlobalFunctionBaselineCode("callAdd") == nullptr);
  EXPECT(vm.debugGlobalFunctionJitCompileError("callAdd").find("OP_CALL") !=
         std::string::npos);
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

void testBaselineCompilerGeneratesArm64ForAddFunction() {
  const minijs::Chunk chunk = compileProgram("function addOne(value) { return value + 1; }");
  const auto function = lastBytecodeFunctionNamed(chunk, "addOne");
  EXPECT(function != nullptr);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
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

void testBaselineCompilerRejectsUnsupportedOpcode() {
  const minijs::Chunk chunk = compileProgram("function sub(a, b) { return a - b; }");
  const auto function = lastBytecodeFunctionNamed(chunk, "sub");
  EXPECT(function != nullptr);

  minijs::BaselineCompiler compiler;
  const minijs::BaselineCompileResult result = compiler.compile(*function);
  EXPECT(!result.succeeded());
  EXPECT(result.code == nullptr);
  EXPECT(result.error.find("OP_SUB") != std::string::npos);
}

#if defined(__aarch64__) || defined(_M_ARM64)
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
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline runtime helper failed");
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
    EXPECT(std::string_view(error.what()) == "RuntimeError: baseline runtime helper failed");
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
  testJitUnsupportedCallOpcodeFailsAndFallsBackToVm();
  testBaselineExecutorRunsArithmeticAndLocals();
  testBaselineExecutorRunsSimpleLoop();
  testBaselineExecutorPreservesArithmeticErrors();
  testBaselineCompilerGeneratesArm64ForAddFunction();
  testBaselineCompilerRejectsUnsupportedOpcode();
#if defined(__aarch64__) || defined(_M_ARM64)
  testBaselineCompilerNativeEntryRunsAddFunction();
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
  testJitDispatchEntersCompiledFunctionOnLaterCall();
  testJitDispatchRespectsDisabledJit();
  testJitDispatchPreservesRuntimeErrors();
  testJitDispatchRunsAfterManualGc();
  testJitDispatchUsesMethodSlotStart();
  testJitDispatchPreservesInitReceiverReturn();
}
