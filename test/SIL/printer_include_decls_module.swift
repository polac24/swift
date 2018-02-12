// RUN: rm -f %t.*
// RUN: %target-swift-frontend -emit-sil %s %S/Inputs/printer_include_decls_module_helper.swift -o %t.sil -module-name main
// RUN: %FileCheck --input-file=%t.sil %s
// RUN: %FileCheck --input-file=%t.sil %S/Inputs/printer_include_decls_module_helper.swift

// RUN: %target-swift-frontend -emit-sil -primary-file %s %S/Inputs/printer_include_decls_module_helper.swift -o %t.sil -module-name main
// RUN: %FileCheck --input-file=%t.sil %s
// RUN: %FileCheck --input-file=%t.sil %S/Inputs/printer_include_decls_module_helper.swift -check-prefix=CHECK-NEGATIVE

var x: Int = 1
// CHECK: var x: Int
// CHECK-NEGATIVE-NOT: var x: Int

class Foo {
// FIXME: The constructors and destructors without bodies cannot be parsed.
  init(i: Int) {
    self.y = i
  }
// CHECK: init(i: Int)
// CHECK-NEGATIVE-NOT: init(i: Int)

  subscript(x: Int, y: Int) -> Int {
    get {
      return 0
    }
    set {}
  }
// CHECK: subscript(x: Int, y: Int) -> Int
// CHECK-NEGATIVE-NOT: subscript(x: Int, y: Int) -> Int

  final var y : Int
// CHECK: var y: Int
// CHECK-NEGATIVE-NOT: var y: Int

  func m() {}
// CHECK: func m()
// CHECK-NEGATIVE-NOT: func m()
}

func fooF(x: Foo) -> Int {
  return x.y
}
// CHECK: func fooF(x: Foo) -> Int
// CHECK-NEGATIVE-NOT: func fooF(x: Foo) -> Int
