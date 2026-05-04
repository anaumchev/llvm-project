// RUN: mlir-opt %s -verify-diagnostics -split-input-file | FileCheck %s

// Tests for the builtin `TokenTypeInterface` and the
// `Token` / `AnyType` / `AnyTypeOrToken` ODS predicates.
//
// `!test.test_token` is a test-dialect type that implements
// `TokenTypeInterface`. The default `AnyType` predicate excludes tokens, while
// `AnyTypeOrToken` and `Token` accept them.

// CHECK-LABEL: @token_produce_consume
func.func @token_produce_consume() {
  // CHECK: %[[T:.*]] = test.token.produce : !test.test_token
  %t = test.token.produce : !test.test_token
  // CHECK: test.token.consume %[[T]] : !test.test_token
  test.token.consume %t : !test.test_token
  // CHECK: test.token.any_or_token %[[T]] : !test.test_token
  test.token.any_or_token %t : !test.test_token
  return
}

// -----

// `AnyTypeOrToken` also accepts non-token types.
// CHECK-LABEL: @any_or_token_with_non_token
func.func @any_or_token_with_non_token(%arg0: i32) {
  // CHECK: test.token.any_or_token %{{.*}} : i32
  test.token.any_or_token %arg0 : i32
  return
}

// -----

// `AnyType` accepts arbitrary non-token types.
// CHECK-LABEL: @any_type_with_non_token
func.func @any_type_with_non_token(%arg0: i32) {
  // CHECK: test.token.any_type %{{.*}} : i32
  test.token.any_type %arg0 : i32
  return
}

// -----

// `AnyType` rejects tokens by default.
func.func @any_type_rejects_token() {
  %t = test.token.produce : !test.test_token
  // expected-error @below {{operand #0 must be any non-token type}}
  test.token.any_type %t : !test.test_token
  return
}

// -----

// `Token` rejects non-token types. The operand's cppType is
// `TokenTypeInterface`, so type resolution fails at parse time.
func.func @token_rejects_non_token(%arg0: i32) {
  // expected-error @below {{invalid kind of type specified}}
  test.token.consume %arg0 : i32
  return
}
