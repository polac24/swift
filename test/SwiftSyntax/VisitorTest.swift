// RUN: %target-run-simple-swift
// REQUIRES: executable_test
// REQUIRES: OS=macosx
// REQUIRES: objc_interop

import StdlibUnittest
import Foundation
import SwiftSyntax
import SwiftLang

func getInput(_ file: String) -> URL {
  var result = URL(fileURLWithPath: #file)
  result.deleteLastPathComponent()
  result.appendPathComponent("Inputs")
  result.appendPathComponent(file)
  return result
}

var VisitorTests = TestSuite("SyntaxVisitor")

VisitorTests.test("Basic") {
  class FuncCounter: SyntaxVisitor {
    var funcCount = 0
    override func visit(_ node: FunctionDeclSyntax) {
      funcCount += 1
      super.visit(node)
    }
  }
  expectDoesNotThrow({
    let parsed = try SourceFileSyntax.decodeSourceFileSyntax(try
      SwiftLang.parse(getInput("visitor.swift")))
    let counter = FuncCounter()
    let hashBefore = parsed.hashValue
    counter.visit(parsed)
    expectEqual(counter.funcCount, 3)
    expectEqual(hashBefore, parsed.hashValue)
  })
}

runAllTests()
