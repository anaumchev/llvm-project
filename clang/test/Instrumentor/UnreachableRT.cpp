//===-- examples/Instrumentor/stack_usage.c - An example Instrumentor use -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

extern "C" {
void __unreachable_pre_unreachable(int64_t ID) { 
  llvm::errs() << "Hit unreachable #" << ID << "\n";
  llvm::sys::PrintStackTrace(llvm::errs());
}
}
