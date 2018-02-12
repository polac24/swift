var z:Int = 1
// CHECK: var z: Int
// CHECK-NEGATIVE-NOT: var z: Int

class Bar {
// CHECK: class Bar
// CHECK-NEGATIVE-NOT: class Bar
    
    func b() {}
    // CHECK: func b()
    // CHECK-NEGATIVE-NOT: func b()
}
