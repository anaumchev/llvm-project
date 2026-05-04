# Tokens

[TOC]

## Overview

Intuitively, a *token* value is a pointer to an operation (via an OpResult)
or a pointer to a region (via an entry block argument).

More precisely, a token is an SSA value whose purpose is to encode a
**static** def–use relationship between operations or regions. It carries
no runtime data and is not allowed to flow through "regular"
value-forwarding constructs. A token's provenance cannot be obscured through
value forwarding.

In MLIR, "token" is not a single concrete builtin type. Instead, any type
that implements the builtin `TokenTypeInterface` is treated as a token by
the framework. Dialects can define their own dialect-specific token types.

## `TokenTypeInterface`

`TokenTypeInterface` is a parameterless, methodless marker type interface.

A type opts in by attaching the interface in TableGen:

```tablegen
def MyDialect_Token : TypeDef<MyDialect, "MyToken", [TokenTypeInterface]> {
  let mnemonic = "token";
}
```

## Structural Contract

A token value is, by construction:

1. **Not value-forwarding.** In particular, a token must not appear as a
   forwarded value. E.g.:
    * a forwarded result/operand of a `CallOpInterface` op,
    * an argument or result type of a `FunctionOpInterface` op (a token
      block argument *inside* a function body is fine — what is disallowed
      is forwarding tokens across the call/return boundary),
    * a successor operand or successor block argument of a
      `BranchOpInterface` op,
    * a forwarded operand to/from any region of a `RegionBranchOpInterface`
      op (iter-args, region results, yielded values), or
    * the result of any op that selects or merges values it does not
      understand (e.g. `arith.select`).

2. **Statically resolvable.** Walking the def–use chain from any token use
   reaches a producing op without crossing a forwarding boundary.

3. **Cannot constant-fold.** No constant of token type exists.

These properties mirror what LLVM IR already documents for its own
[`token` type](https://llvm.org/docs/LangRef.html#token-type).

## ODS Integration

Tokens are excluded from the default `AnyType` predicate, so an op that has
not opted in cannot accept a token as an arbitrary operand or result.
Three predicates are provided in `CommonTypeConstraints.td`:

| Predicate          | Accepts                              | Use when …                                                            |
| ------------------ | ------------------------------------ | ----------------------------------------------------------------------|
| `AnyType`          | any non-token type                   | the default; matches the historical meaning of "any type" pre-tokens. |
| `AnyTypeOrToken`   | any type, including tokens           | the op legitimately accepts arbitrary types (including tokens).       |
| `Token`            | only types implementing `TokenTypeInterface` | the op specifically takes a token operand/result.             |


## Examples

### Rejected: tokens in `AnyType` positions

```mlir
// error: 'scf.if' op result #0 must be variadic of any non-token type,
//        but got '!my.token'
%t = scf.if %cond -> !my.token {
  %a = my.token.produce : !my.token
  scf.yield %a : !my.token
} else {
  %b = my.token.produce : !my.token
  scf.yield %b : !my.token
}
```

`scf.if`'s results are declared with `Variadic<AnyType>` and `scf.yield`'s
operands likewise use `AnyType`. Because `AnyType` excludes tokens by
default, yielding (or returning) a token through a `scf.if` (or any other
op that has not explicitly opted in via `AnyTypeOrToken`) is rejected.
