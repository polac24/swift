//===--- MetadataValues.h - Compiler/runtime ABI Metadata -------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This header is shared between the runtime and the compiler and
// includes target-independent information which can be usefully shared
// between them.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_ABI_METADATAVALUES_H
#define SWIFT_ABI_METADATAVALUES_H

#include "swift/AST/Ownership.h"
#include "swift/Basic/LLVM.h"
#include "swift/Runtime/Unreachable.h"

#include <stdlib.h>
#include <stdint.h>

namespace swift {

enum {
  /// The number of words (pointers) in a value buffer.
  NumWords_ValueBuffer = 3,

  /// The number of words in a yield-once coroutine buffer.
  NumWords_YieldOnceBuffer = 4,

  /// The number of words in a yield-many coroutine buffer.
  NumWords_YieldManyBuffer = 8,
};

struct InProcess;
template <typename Runtime> struct TargetMetadata;
using Metadata = TargetMetadata<InProcess>;

/// Kinds of Swift metadata records.  Some of these are types, some
/// aren't.
enum class MetadataKind : uint32_t {
#define METADATAKIND(name, value) name = value,
#define ABSTRACTMETADATAKIND(name, start, end)                                 \
  name##_Start = start, name##_End = end,
#include "MetadataKind.def"
};

const unsigned LastEnumeratedMetadataKind = 2047;

/// Try to translate the 'isa' value of a type/heap metadata into a value
/// of the MetadataKind enum.
inline MetadataKind getEnumeratedMetadataKind(uint64_t kind) {
  if (kind > LastEnumeratedMetadataKind)
    return MetadataKind::Class;
  return MetadataKind(kind);
}

StringRef getStringForMetadataKind(MetadataKind kind);

/// Kinds of Swift nominal type descriptor records.
enum class NominalTypeKind : uint32_t {
#define NOMINALTYPEMETADATAKIND(name, value) name = value,
#include "MetadataKind.def"
};

/// Flags for dynamic-cast operations.
enum class DynamicCastFlags : size_t {
  /// All flags clear.
  Default = 0x0,

  /// True if the cast is not permitted to fail.
  Unconditional = 0x1,

  /// True if the cast should 'take' the source value on success;
  /// false if the value should be copied.
  TakeOnSuccess = 0x2,

  /// True if the cast should destroy the source value on failure;
  /// false if the value should be left in place.
  DestroyOnFailure = 0x4,
};
inline bool operator&(DynamicCastFlags a, DynamicCastFlags b) {
  return (size_t(a) & size_t(b)) != 0;
}
inline DynamicCastFlags operator|(DynamicCastFlags a, DynamicCastFlags b) {
  return DynamicCastFlags(size_t(a) | size_t(b));
}
inline DynamicCastFlags operator-(DynamicCastFlags a, DynamicCastFlags b) {
  return DynamicCastFlags(size_t(a) & ~size_t(b));
}
inline DynamicCastFlags &operator|=(DynamicCastFlags &a, DynamicCastFlags b) {
  return a = (a | b);
}

/// Swift class flags.
/// These flags are valid only when isTypeMetadata().
/// When !isTypeMetadata() these flags will collide with other Swift ABIs.
enum class ClassFlags : uint32_t {
  /// Is this a Swift class from the Darwin pre-stable ABI?
  /// This bit is clear in stable ABI Swift classes.
  /// The Objective-C runtime also reads this bit.
  IsSwiftPreStableABI = 0x1,

  /// Does this class use Swift refcounting?
  UsesSwiftRefcounting = 0x2,

  /// Has this class a custom name, specified with the @objc attribute?
  HasCustomObjCName = 0x4
};
inline bool operator&(ClassFlags a, ClassFlags b) {
  return (uint32_t(a) & uint32_t(b)) != 0;
}
inline ClassFlags operator|(ClassFlags a, ClassFlags b) {
  return ClassFlags(uint32_t(a) | uint32_t(b));
}
inline ClassFlags &operator|=(ClassFlags &a, ClassFlags b) {
  return a = (a | b);
}

/// Flags that go in a MethodDescriptor structure.
class MethodDescriptorFlags {
public:
  typedef uint32_t int_type;
  enum class Kind {
    Method,
    Init,
    Getter,
    Setter,
    MaterializeForSet,
  };

private:
  enum : int_type {
    KindMask = 0x0F,                // 16 kinds should be enough for anybody
    IsInstanceMask = 0x10,
    IsDynamicMask = 0x20,
  };

  int_type Value;

public:
  MethodDescriptorFlags(Kind kind) : Value(unsigned(kind)) {}

  MethodDescriptorFlags withIsInstance(bool isInstance) const {
    auto copy = *this;
    if (isInstance) {
      copy.Value |= IsInstanceMask;
    } else {
      copy.Value &= ~IsInstanceMask;
    }
    return copy;
  }

  MethodDescriptorFlags withIsDynamic(bool isDynamic) const {
    auto copy = *this;
    if (isDynamic)
      copy.Value |= IsDynamicMask;
    else
      copy.Value &= ~IsDynamicMask;
    return copy;
  }

  Kind getKind() const { return Kind(Value & KindMask); }

  /// Is the method marked 'dynamic'?
  bool isDynamic() const { return Value & IsDynamicMask; }

  /// Is the method an instance member?
  ///
  /// Note that 'init' is not considered an instance member.
  bool isInstance() const { return Value & IsInstanceMask; }

  int_type getIntValue() const { return Value; }
};

enum : unsigned {
  /// Number of words reserved in generic metadata patterns.
  NumGenericMetadataPrivateDataWords = 16,
};

/// Kinds of type metadata/protocol conformance records.
enum class TypeMetadataRecordKind : unsigned {
  /// The conformance is for a nominal type referenced directly;
  /// getNominalTypeDescriptor() points to the nominal type descriptor.
  DirectNominalTypeDescriptor = 0x00,

  /// The conformance is for a nominal type referenced indirectly;
  /// getNominalTypeDescriptor() points to the nominal type descriptor.
  IndirectNominalTypeDescriptor = 0x01,

  /// Reserved for future use.
  Reserved = 0x02,
  
  /// The conformance is for an Objective-C class that has no nominal type
  /// descriptor.
  /// getIndirectObjCClass() points to a variable that contains the pointer to
  /// the class object, which then requires a runtime call to get metadata.
  ///
  /// On platforms without Objective-C interoperability, this case is
  /// unused.
  IndirectObjCClass = 0x03,

  First_Kind = DirectNominalTypeDescriptor,
  Last_Kind = IndirectObjCClass,
};

/// Flag that indicates whether an existential type is class-constrained or not.
enum class ProtocolClassConstraint : bool {
  /// The protocol is class-constrained, so only class types can conform to it.
  ///
  /// This must be 0 for ABI compatibility with Objective-C protocol_t records.
  Class = false,
  /// Any type can conform to the protocol.
  Any = true,
};

/// Identifiers for protocols with special meaning to the Swift runtime.
enum class SpecialProtocol: uint8_t {
  /// Not a special protocol.
  ///
  /// This must be 0 for ABI compatibility with Objective-C protocol_t records.
  None = 0,
  /// The Error protocol.
  Error = 1,
};

/// Identifiers for protocol method dispatch strategies.
enum class ProtocolDispatchStrategy: uint8_t {
  /// Uses ObjC method dispatch.
  ///
  /// This must be 0 for ABI compatibility with Objective-C protocol_t records.
  ObjC = 0,
  
  /// Uses Swift protocol witness table dispatch.
  ///
  /// To invoke methods of this protocol, a pointer to a protocol witness table
  /// corresponding to the protocol conformance must be available.
  Swift = 1,
};

/// Flags for protocol descriptors.
class ProtocolDescriptorFlags {
  typedef uint32_t int_type;
  enum : int_type {
    IsSwift           =   1U <<  0U,
    ClassConstraint   =   1U <<  1U,

    DispatchStrategyMask  = 0xFU << 2U,
    DispatchStrategyShift = 2,

    SpecialProtocolMask  = 0x000003C0U,
    SpecialProtocolShift = 6,

    IsResilient       =   1U <<  10U,

    /// Reserved by the ObjC runtime.
    _ObjCReserved        = 0xFFFF0000U,
  };

  int_type Data;
  
  constexpr ProtocolDescriptorFlags(int_type Data) : Data(Data) {}
public:
  constexpr ProtocolDescriptorFlags() : Data(0) {}
  constexpr ProtocolDescriptorFlags withSwift(bool s) const {
    return ProtocolDescriptorFlags((Data & ~IsSwift) | (s ? IsSwift : 0));
  }
  constexpr ProtocolDescriptorFlags withClassConstraint(
                                              ProtocolClassConstraint c) const {
    return ProtocolDescriptorFlags((Data & ~ClassConstraint)
                                     | (bool(c) ? ClassConstraint : 0));
  }
  constexpr ProtocolDescriptorFlags withDispatchStrategy(
                                             ProtocolDispatchStrategy s) const {
    return ProtocolDescriptorFlags((Data & ~DispatchStrategyMask)
                                     | (int_type(s) << DispatchStrategyShift));
  }
  constexpr ProtocolDescriptorFlags
  withSpecialProtocol(SpecialProtocol sp) const {
    return ProtocolDescriptorFlags((Data & ~SpecialProtocolMask)
                                     | (int_type(sp) << SpecialProtocolShift));
  }
  constexpr ProtocolDescriptorFlags withResilient(bool s) const {
    return ProtocolDescriptorFlags((Data & ~IsResilient) | (s ? IsResilient : 0));
  }
  
  /// Was the protocol defined in Swift 1 or 2?
  bool isSwift() const { return Data & IsSwift; }

  /// Is the protocol class-constrained?
  ProtocolClassConstraint getClassConstraint() const {
    return ProtocolClassConstraint(bool(Data & ClassConstraint));
  }
  
  /// What dispatch strategy does this protocol use?
  ProtocolDispatchStrategy getDispatchStrategy() const {
    return ProtocolDispatchStrategy((Data & DispatchStrategyMask)
                                      >> DispatchStrategyShift);
  }
  
  /// Does the protocol require a witness table for method dispatch?
  bool needsWitnessTable() const {
    return needsWitnessTable(getDispatchStrategy());
  }
  
  static bool needsWitnessTable(ProtocolDispatchStrategy strategy) {
    switch (strategy) {
    case ProtocolDispatchStrategy::ObjC:
      return false;
    case ProtocolDispatchStrategy::Swift:
      return true;
    }

    swift_runtime_unreachable("Unhandled ProtocolDispatchStrategy in switch.");
  }
  
  /// Return the identifier if this is a special runtime-known protocol.
  SpecialProtocol getSpecialProtocol() const {
    return SpecialProtocol(uint8_t((Data & SpecialProtocolMask)
                                 >> SpecialProtocolShift));
  }
  
  /// Can new requirements with default witnesses be added resiliently?
  bool isResilient() const { return Data & IsResilient; }

  int_type getIntValue() const {
    return Data;
  }

#ifndef NDEBUG
  LLVM_ATTRIBUTE_DEPRECATED(void dump() const LLVM_ATTRIBUTE_USED,
                            "Only for use in the debugger");
#endif
};

/// Flags that go in a ProtocolRequirement structure.
class ProtocolRequirementFlags {
public:
  typedef uint32_t int_type;
  enum class Kind {
    BaseProtocol,
    Method,
    Init,
    Getter,
    Setter,
    MaterializeForSet,
    AssociatedTypeAccessFunction,
    AssociatedConformanceAccessFunction,
  };

private:
  enum : int_type {
    KindMask = 0x0F,                // 16 kinds should be enough for anybody
    IsInstanceMask = 0x10,
  };

  int_type Value;

public:
  ProtocolRequirementFlags(Kind kind) : Value(unsigned(kind)) {}

  ProtocolRequirementFlags withIsInstance(bool isInstance) const {
    auto copy = *this;
    if (isInstance) {
      copy.Value |= IsInstanceMask;
    } else {
      copy.Value &= ~IsInstanceMask;
    }
    return copy;
  }

  Kind getKind() const { return Kind(Value & KindMask); }

  /// Is the method an instance member?
  ///
  /// Note that 'init' is not considered an instance member.
  bool isInstance() const { return Value & IsInstanceMask; }

  int_type getIntValue() const { return Value; }
};

/// Flags that go in a TargetConformanceDescriptor structure.
class ConformanceFlags {
public:
  typedef uint32_t int_type;

  enum class ConformanceKind {
    /// A direct reference to a protocol witness table.
    WitnessTable,
    /// A function pointer that can be called to access the protocol witness
    /// table.
    WitnessTableAccessor,
    /// A function pointer that can be called to access the protocol witness
    /// table whose conformance is conditional on additional requirements that
    /// must first be evaluated and then provided to the accessor function.
    ConditionalWitnessTableAccessor,

    First_Kind = WitnessTable,
    Last_Kind = ConditionalWitnessTableAccessor,
  };

private:
  enum : int_type {
    ConformanceKindMask = 0x07,      // 8 conformance kinds

    TypeMetadataKindMask = 0x7 << 3, // 8 type reference kinds
    TypeMetadataKindShift = 3,

    IsRetroactiveMask = 0x01 << 6,
    IsSynthesizedNonUniqueMask = 0x01 << 7,

    NumConditionalRequirementsMask = 0xFF << 8,
    NumConditionalRequirementsShift = 8,
  };

  int_type Value;

public:
  ConformanceFlags(int_type value = 0) : Value(value) {}

  ConformanceFlags withConformanceKind(ConformanceKind kind) const {
    return ConformanceFlags((Value & ~ConformanceKindMask) | int_type(kind));
  }

  ConformanceFlags withTypeReferenceKind(TypeMetadataRecordKind kind) const {
    return ConformanceFlags((Value & ~TypeMetadataKindMask)
                            | (int_type(kind) << TypeMetadataKindShift));
  }

  ConformanceFlags withIsRetroactive(bool isRetroactive) const {
    return ConformanceFlags((Value & ~IsRetroactiveMask)
                            | (isRetroactive? IsRetroactiveMask : 0));
  }

  ConformanceFlags withIsSynthesizedNonUnique(
                                          bool isSynthesizedNonUnique) const {
    return ConformanceFlags(
                  (Value & ~IsSynthesizedNonUniqueMask)
                  | (isSynthesizedNonUnique ? IsSynthesizedNonUniqueMask : 0));
  }

  ConformanceFlags withNumConditionalRequirements(unsigned n) const {
    return ConformanceFlags((Value & ~NumConditionalRequirementsMask)
                            | (n << NumConditionalRequirementsShift));
  }

  /// Retrieve the conformance kind.
  ConformanceKind getConformanceKind() const {
    return ConformanceKind(Value & ConformanceKindMask);
  }

  /// Retrieve the type reference kind kind.
  TypeMetadataRecordKind getTypeReferenceKind() const {
    return TypeMetadataRecordKind(
                      (Value & TypeMetadataKindMask) >> TypeMetadataKindShift);
  }

  /// Is the conformance "retroactive"?
  ///
  /// A conformance is retroactive when it occurs in a module that is
  /// neither the module in which the protocol is defined nor the module
  /// in which the conforming type is defined. With retroactive conformance,
  /// it is possible to detect a conflict at run time.
  bool isRetroactive() const { return Value & IsRetroactiveMask; }

  /// Is the conformance synthesized in a non-unique manner?
  ///
  /// The Swift compiler will synthesize conformances on behalf of some
  /// imported entities (e.g., C typedefs with the swift_wrapper attribute).
  /// Such conformances are retroactive by nature, but the presence of multiple
  /// such conformances is not a conflict because all synthesized conformances
  /// will be equivalent.
  bool isSynthesizedNonUnique() const {
    return Value & IsSynthesizedNonUniqueMask;
  }

  /// Retrieve the # of conditional requirements.
  unsigned getNumConditionalRequirements() const {
    return (Value & NumConditionalRequirementsMask)
              >> NumConditionalRequirementsShift;
  }

  int_type getIntValue() const { return Value; }
};

/// Flags in an existential type metadata record.
class ExistentialTypeFlags {
  typedef size_t int_type;
  enum : int_type {
    NumWitnessTablesMask  = 0x00FFFFFFU,
    ClassConstraintMask   = 0x80000000U,
    HasSuperclassMask     = 0x40000000U,
    SpecialProtocolMask   = 0x3F000000U,
    SpecialProtocolShift  = 24U,
  };
  int_type Data;

public:
  constexpr ExistentialTypeFlags(int_type Data) : Data(Data) {}
  constexpr ExistentialTypeFlags() : Data(0) {}
  constexpr ExistentialTypeFlags withNumWitnessTables(unsigned numTables) const {
    return ExistentialTypeFlags((Data & ~NumWitnessTablesMask) | numTables);
  }
  constexpr ExistentialTypeFlags
  withClassConstraint(ProtocolClassConstraint c) const {
    return ExistentialTypeFlags((Data & ~ClassConstraintMask)
                                  | (bool(c) ? ClassConstraintMask : 0));
  }
  constexpr ExistentialTypeFlags
  withHasSuperclass(bool hasSuperclass) const {
    return ExistentialTypeFlags((Data & ~HasSuperclassMask)
                                  | (hasSuperclass ? HasSuperclassMask : 0));
  }
  constexpr ExistentialTypeFlags
  withSpecialProtocol(SpecialProtocol sp) const {
    return ExistentialTypeFlags((Data & ~SpecialProtocolMask)
                                  | (int_type(sp) << SpecialProtocolShift));
  }
  
  unsigned getNumWitnessTables() const {
    return Data & NumWitnessTablesMask;
  }
  
  ProtocolClassConstraint getClassConstraint() const {
    return ProtocolClassConstraint(bool(Data & ClassConstraintMask));
  }

  bool hasSuperclassConstraint() const {
    return bool(Data & HasSuperclassMask);
  }

  /// Return whether this existential type represents an uncomposed special
  /// protocol.
  SpecialProtocol getSpecialProtocol() const {
    return SpecialProtocol(uint8_t((Data & SpecialProtocolMask)
                                     >> SpecialProtocolShift));
  }
  
  int_type getIntValue() const {
    return Data;
  }
};

/// Convention values for function type metadata.
enum class FunctionMetadataConvention: uint8_t {
  Swift = 0,
  Block = 1,
  Thin = 2,
  CFunctionPointer = 3,
};

/// Flags in a function type metadata record.
template <typename int_type>
class TargetFunctionTypeFlags {
  // If we were ever to run out of space for function flags (8 bits)
  // one of the flag bits could be used to identify that the rest of
  // the flags is going to be stored somewhere else in the metadata.
  enum : int_type {
    NumParametersMask = 0x0000FFFFU,
    ConventionMask    = 0x00FF0000U,
    ConventionShift   = 16U,
    ThrowsMask        = 0x01000000U,
    ParamFlagsMask    = 0x02000000U,
    EscapingMask      = 0x04000000U,
  };
  int_type Data;
  
  constexpr TargetFunctionTypeFlags(int_type Data) : Data(Data) {}
public:
  constexpr TargetFunctionTypeFlags() : Data(0) {}

  constexpr TargetFunctionTypeFlags
  withNumParameters(unsigned numParams) const {
    return TargetFunctionTypeFlags((Data & ~NumParametersMask) | numParams);
  }
  
  constexpr TargetFunctionTypeFlags<int_type>
  withConvention(FunctionMetadataConvention c) const {
    return TargetFunctionTypeFlags((Data & ~ConventionMask)
                             | (int_type(c) << ConventionShift));
  }
  
  constexpr TargetFunctionTypeFlags<int_type>
  withThrows(bool throws) const {
    return TargetFunctionTypeFlags<int_type>((Data & ~ThrowsMask) |
                                             (throws ? ThrowsMask : 0));
  }

  constexpr TargetFunctionTypeFlags<int_type>
  withParameterFlags(bool hasFlags) const {
    return TargetFunctionTypeFlags<int_type>((Data & ~ParamFlagsMask) |
                                             (hasFlags ? ParamFlagsMask : 0));
  }

  constexpr TargetFunctionTypeFlags<int_type>
  withEscaping(bool isEscaping) const {
    return TargetFunctionTypeFlags<int_type>((Data & ~EscapingMask) |
                                             (isEscaping ? EscapingMask : 0));
  }

  unsigned getNumParameters() const { return Data & NumParametersMask; }

  FunctionMetadataConvention getConvention() const {
    return FunctionMetadataConvention((Data&ConventionMask) >> ConventionShift);
  }
  
  bool throws() const {
    return bool(Data & ThrowsMask);
  }

  bool isEscaping() const {
    return bool (Data & EscapingMask);
  }

  bool hasParameterFlags() const { return bool(Data & ParamFlagsMask); }

  int_type getIntValue() const {
    return Data;
  }
  
  static TargetFunctionTypeFlags<int_type> fromIntValue(int_type Data) {
    return TargetFunctionTypeFlags(Data);
  }
  
  bool operator==(TargetFunctionTypeFlags<int_type> other) const {
    return Data == other.Data;
  }
  bool operator!=(TargetFunctionTypeFlags<int_type> other) const {
    return Data != other.Data;
  }
};
using FunctionTypeFlags = TargetFunctionTypeFlags<size_t>;

template <typename int_type>
class TargetParameterTypeFlags {
  enum : int_type {
    InOutMask    = 1 << 0,
    SharedMask   = 1 << 1,
    VariadicMask = 1 << 2,
  };
  int_type Data;

  constexpr TargetParameterTypeFlags(int_type Data) : Data(Data) {}

public:
  constexpr TargetParameterTypeFlags() : Data(0) {}

  constexpr TargetParameterTypeFlags<int_type> withInOut(bool isInOut) const {
    return TargetParameterTypeFlags<int_type>((Data & ~InOutMask) |
                                              (isInOut ? InOutMask : 0));
  }

  constexpr TargetParameterTypeFlags<int_type> withShared(bool isShared) const {
    return TargetParameterTypeFlags<int_type>((Data & ~SharedMask) |
                                              (isShared ? SharedMask : 0));
  }

  constexpr TargetParameterTypeFlags<int_type>
  withVariadic(bool isVariadic) const {
    return TargetParameterTypeFlags<int_type>((Data & ~VariadicMask) |
                                              (isVariadic ? VariadicMask : 0));
  }

  bool isNone() const { return Data == 0; }
  bool isInOut() const { return Data & InOutMask; }
  bool isShared() const { return Data & SharedMask; }
  bool isVariadic() const { return Data & VariadicMask; }

  int_type getIntValue() const { return Data; }

  static TargetParameterTypeFlags<int_type> fromIntValue(int_type Data) {
    return TargetParameterTypeFlags(Data);
  }

  bool operator==(TargetParameterTypeFlags<int_type> other) const {
    return Data == other.Data;
  }
  bool operator!=(TargetParameterTypeFlags<int_type> other) const {
    return Data != other.Data;
  }
};
using ParameterFlags = TargetParameterTypeFlags<uint32_t>;

template <typename int_type>
class TargetTupleTypeFlags {
  enum : int_type {
    NumElementsMask = 0x0000FFFFU,
    NonConstantLabelsMask = 0x00010000U,
  };
  int_type Data;

public:
  constexpr TargetTupleTypeFlags() : Data(0) {}
  constexpr TargetTupleTypeFlags(int_type Data) : Data(Data) {}

  constexpr TargetTupleTypeFlags
  withNumElements(unsigned numElements) const {
    return TargetTupleTypeFlags((Data & ~NumElementsMask) | numElements);
  }

  constexpr TargetTupleTypeFlags<int_type> withNonConstantLabels(
                                             bool hasNonConstantLabels) const {
    return TargetTupleTypeFlags<int_type>(
                        (Data & ~NonConstantLabelsMask) |
                          (hasNonConstantLabels ? NonConstantLabelsMask : 0));
  }

  unsigned getNumElements() const { return Data & NumElementsMask; }

  bool hasNonConstantLabels() const { return Data & NonConstantLabelsMask; }

  int_type getIntValue() const { return Data; }

  static TargetTupleTypeFlags<int_type> fromIntValue(int_type Data) {
    return TargetTupleTypeFlags(Data);
  }

  bool operator==(TargetTupleTypeFlags<int_type> other) const {
    return Data == other.Data;
  }
  bool operator!=(TargetTupleTypeFlags<int_type> other) const {
    return Data != other.Data;
  }
};
using TupleTypeFlags = TargetTupleTypeFlags<size_t>;

/// Field types and flags as represented in a nominal type's field/case type
/// vector.
class FieldType {
  typedef uintptr_t int_type;
  // Type metadata is always at least pointer-aligned, so we get at least two
  // low bits to stash flags. We could use three low bits on 64-bit, and maybe
  // some high bits as well.
  enum : int_type {
    Indirect = 1,
    Weak = 2,

    TypeMask = ((uintptr_t)-1) & ~(alignof(void*) - 1),
  };
  int_type Data;

  constexpr FieldType(int_type Data) : Data(Data) {}
public:
  constexpr FieldType() : Data(0) {}
  FieldType withType(const Metadata *T) const {
    return FieldType((Data & ~TypeMask) | (uintptr_t)T);
  }

  constexpr FieldType withIndirect(bool indirect) const {
    return FieldType((Data & ~Indirect)
                     | (indirect ? Indirect : 0));
  }

  constexpr FieldType withWeak(bool weak) const {
    return FieldType((Data & ~Weak)
                     | (weak ? Weak : 0));
  }

  bool isIndirect() const {
    return bool(Data & Indirect);
  }

  bool isWeak() const {
    return bool(Data & Weak);
  }

  const Metadata *getType() const {
    return (const Metadata *)(Data & TypeMask);
  }

  int_type getIntValue() const {
    return Data;
  }
};

/// Flags for exclusivity-checking operations.
enum class ExclusivityFlags : uintptr_t {
  Read             = 0x0,
  Modify           = 0x1,
  // Leave space for other actions.
  // Don't rely on ActionMask in stable ABI.
  ActionMask       = 0x1,

  // Downgrade exclusivity failures to a warning.
  WarningOnly      = 0x10
};
static inline ExclusivityFlags operator|(ExclusivityFlags lhs,
                                         ExclusivityFlags rhs) {
  return ExclusivityFlags(uintptr_t(lhs) | uintptr_t(rhs));
}
static inline ExclusivityFlags &operator|=(ExclusivityFlags &lhs,
                                           ExclusivityFlags rhs) {
  return (lhs = (lhs | rhs));
}
static inline ExclusivityFlags getAccessAction(ExclusivityFlags flags) {
  return ExclusivityFlags(uintptr_t(flags)
                        & uintptr_t(ExclusivityFlags::ActionMask));
}
static inline bool isWarningOnly(ExclusivityFlags flags) {
  return uintptr_t(flags) & uintptr_t(ExclusivityFlags::WarningOnly);
}

/// Flags for struct layout.
enum class StructLayoutFlags : uintptr_t {
  /// Reserve space for 256 layout algorithms.
  AlgorithmMask     = 0xff,

  /// The ABI baseline algorithm, i.e. the algorithm implemented in Swift 5.
  Swift5Algorithm   = 0x00,

  /// Is the value-witness table mutable in place, or does layout need to
  /// clone it?
  IsVWTMutable      = 0x100,
};
static inline StructLayoutFlags operator|(StructLayoutFlags lhs,
                                          StructLayoutFlags rhs) {
  return StructLayoutFlags(uintptr_t(lhs) | uintptr_t(rhs));
}
static inline StructLayoutFlags &operator|=(StructLayoutFlags &lhs,
                                            StructLayoutFlags rhs) {
  return (lhs = (lhs | rhs));
}
static inline StructLayoutFlags getLayoutAlgorithm(StructLayoutFlags flags) {
  return StructLayoutFlags(uintptr_t(flags)
                             & uintptr_t(StructLayoutFlags::AlgorithmMask));
}
static inline bool isValueWitnessTableMutable(StructLayoutFlags flags) {
  return uintptr_t(flags) & uintptr_t(StructLayoutFlags::IsVWTMutable);
}

/// Flags for enum layout.
enum class EnumLayoutFlags : uintptr_t {
  /// Reserve space for 256 layout algorithms.
  AlgorithmMask     = 0xff,

  /// The ABI baseline algorithm, i.e. the algorithm implemented in Swift 5.
  Swift5Algorithm   = 0x00,

  /// Is the value-witness table mutable in place, or does layout need to
  /// clone it?
  IsVWTMutable      = 0x100,
};
static inline EnumLayoutFlags operator|(EnumLayoutFlags lhs,
                                        EnumLayoutFlags rhs) {
  return EnumLayoutFlags(uintptr_t(lhs) | uintptr_t(rhs));
}
static inline EnumLayoutFlags &operator|=(EnumLayoutFlags &lhs,
                                          EnumLayoutFlags rhs) {
  return (lhs = (lhs | rhs));
}
static inline EnumLayoutFlags getLayoutAlgorithm(EnumLayoutFlags flags) {
  return EnumLayoutFlags(uintptr_t(flags)
                           & uintptr_t(EnumLayoutFlags::AlgorithmMask));
}
static inline bool isValueWitnessTableMutable(EnumLayoutFlags flags) {
  return uintptr_t(flags) & uintptr_t(EnumLayoutFlags::IsVWTMutable);
}

/// The number of arguments that will be passed directly to a generic
/// nominal type access function. The remaining arguments (if any) will be
/// passed as an array. That array has enough storage for all of the arguments,
/// but only fills in the elements not passed directly. The callee may
/// mutate the array to fill in the direct arguments.
constexpr unsigned NumDirectGenericTypeMetadataAccessFunctionArgs = 3;

/// The offset (in pointers) to the first requirement in a witness table.
constexpr unsigned WitnessTableFirstRequirementOffset = 1;

/// Kinds of context descriptor.
enum class ContextDescriptorKind : uint8_t {
  /// This context descriptor represents a module.
  Module = 0,
  
  /// This context descriptor represents an extension.
  Extension = 1,
  
  /// This context descriptor represents an anonymous possibly-generic context
  /// such as a function body.
  Anonymous = 2,
  
  /// First kind that represents a type of any sort.
  Type_First = 16,
  
  /// This context descriptor represents a class.
  Class = Type_First,
  
  /// This context descriptor represents a struct.
  Struct = Type_First + 1,
  
  /// This context descriptor represents an enum.
  Enum = Type_First + 2,
  
  /// Last kind that represents a type of any sort.
  Type_Last = 31,
};

/// Common flags stored in the first 32-bit word of any context descriptor.
struct ContextDescriptorFlags {
private:
  uint32_t Value;

  explicit constexpr ContextDescriptorFlags(uint32_t Value)
    : Value(Value) {}
public:
  constexpr ContextDescriptorFlags() : Value(0) {}
  constexpr ContextDescriptorFlags(ContextDescriptorKind kind,
                                   bool isGeneric,
                                   bool isUnique,
                                   uint8_t version,
                                   uint16_t kindSpecificFlags)
    : ContextDescriptorFlags(ContextDescriptorFlags()
                               .withKind(kind)
                               .withGeneric(isGeneric)
                               .withUnique(isUnique)
                               .withVersion(version)
                               .withKindSpecificFlags(kindSpecificFlags))
  {}

  /// The kind of context this descriptor describes.
  constexpr ContextDescriptorKind getKind() const {
    return ContextDescriptorKind(Value & 0x1Fu);
  }
  
  /// Whether the context being described is generic.
  constexpr bool isGeneric() const {
    return (Value & 0x80u) != 0;
  }
  
  /// Whether this is a unique record describing the referenced context.
  constexpr bool isUnique() const {
    return (Value & 0x40u) != 0;
  }
  
  /// The format version of the descriptor. Higher version numbers may have
  /// additional fields that aren't present in older versions.
  constexpr uint8_t getVersion() const {
    return (Value >> 8u) & 0xFFu;
  }
  
  /// The most significant two bytes of the flags word, which can have
  /// kind-specific meaning.
  constexpr uint16_t getKindSpecificFlags() const {
    return (Value >> 16u) & 0xFFFFu;
  }
  
  constexpr ContextDescriptorFlags withKind(ContextDescriptorKind kind) const {
    return assert((uint8_t(kind) & 0x1F) == uint8_t(kind)),
      ContextDescriptorFlags((Value & 0xFFFFFFE0u) | uint8_t(kind));
  }
  
  constexpr ContextDescriptorFlags withGeneric(bool isGeneric) const {
    return ContextDescriptorFlags((Value & 0xFFFFFF7Fu)
                                  | (isGeneric ? 0x80u : 0));
  }

  constexpr ContextDescriptorFlags withUnique(bool isUnique) const {
    return ContextDescriptorFlags((Value & 0xFFFFFFBFu)
                                  | (isUnique ? 0x40u : 0));
  }

  constexpr ContextDescriptorFlags withVersion(uint8_t version) const {
    return ContextDescriptorFlags((Value & 0xFFFF00FFu) | (version << 8u));
  }

  constexpr ContextDescriptorFlags
  withKindSpecificFlags(uint16_t flags) const {
    return ContextDescriptorFlags((Value & 0xFFFFu) | (flags << 16u));
  }
  
  constexpr uint32_t getIntValue() const {
    return Value;
  }
};

/// Flags for nominal type context descriptors. These values are used as the
/// kindSpecificFlags of the ContextDescriptorFlags for the type.
enum class TypeContextDescriptorFlags: uint16_t {
  /// Set if the context descriptor is includes metadata for dynamically
  /// constructing a class's vtables at metadata instantiation time.
  HasVTable = 0x8000u,
  
  /// Set if the context descriptor is for a class with resilient ancestry.
  HasResilientSuperclass = 0x4000u,
  
  /// Set if the type represents an imported C tag type.
  IsCTag = 0x2000u,
  
  /// Set if the type represents an imported C typedef type.
  IsCTypedef = 0x1000u,
};

enum class GenericParamKind : uint8_t {
  /// A type parameter.
  Type = 0,
  
  Max = 0x3F,
};

class GenericParamDescriptor {
  uint8_t Value;
  
  explicit constexpr GenericParamDescriptor(uint8_t Value)
    : Value(Value) {}
public:
  constexpr GenericParamDescriptor(GenericParamKind kind,
                                   bool hasKeyArgument,
                                   bool hasExtraArgument)
    : GenericParamDescriptor(GenericParamDescriptor(0)
                         .withKind(kind)
                         .withKeyArgument(hasKeyArgument)
                         .withExtraArgument(hasExtraArgument))
  {}
  
  constexpr bool hasKeyArgument() const {
    return (Value & 0x80u) != 0;
  }

  constexpr bool hasExtraArgument() const {
    return (Value & 0x40u) != 0;
  }

  constexpr GenericParamKind getKind() const {
    return GenericParamKind(Value & 0x3Fu);
  }
  
  constexpr GenericParamDescriptor
  withKeyArgument(bool hasKeyArgument) const {
    return GenericParamDescriptor((Value & 0x7Fu)
      | (hasKeyArgument ? 0x80u : 0));
  }
  
  constexpr GenericParamDescriptor
  withExtraArgument(bool hasExtraArgument) const {
    return GenericParamDescriptor((Value & 0xBFu)
      | (hasExtraArgument ? 0x40u : 0));
  }
  
  constexpr GenericParamDescriptor withKind(GenericParamKind kind) const {
    return assert((uint8_t(kind) & 0x3Fu) == uint8_t(kind)),
      GenericParamDescriptor((Value & 0xC0u) | uint8_t(kind));
  }
  
  constexpr uint8_t getIntValue() const {
    return Value;
  }
};

enum class GenericRequirementKind : uint8_t {
  /// A protocol requirement.
  Protocol = 0,
  /// A same-type requirement.
  SameType = 1,
  /// A base class requirement.
  BaseClass = 2,
  /// A "same-conformance" requirement, implied by a same-type or base-class
  /// constraint that binds a parameter with protocol requirements.
  SameConformance = 3,
  /// A layout constraint.
  Layout = 0x1F,
};

class GenericRequirementFlags {
  uint32_t Value;
  
  explicit constexpr GenericRequirementFlags(uint32_t Value)
    : Value(Value) {}
public:
  constexpr GenericRequirementFlags(GenericRequirementKind kind,
                                    bool hasKeyArgument,
                                    bool hasExtraArgument)
    : GenericRequirementFlags(GenericRequirementFlags(0)
                         .withKind(kind)
                         .withKeyArgument(hasKeyArgument)
                         .withExtraArgument(hasExtraArgument))
  {}
  
  constexpr bool hasKeyArgument() const {
    return (Value & 0x80u) != 0;
  }

  constexpr bool hasExtraArgument() const {
    return (Value & 0x40u) != 0;
  }

  constexpr GenericRequirementKind getKind() const {
    return GenericRequirementKind(Value & 0x1Fu);
  }
  
  constexpr GenericRequirementFlags
  withKeyArgument(bool hasKeyArgument) const {
    return GenericRequirementFlags((Value & 0x7Fu)
      | (hasKeyArgument ? 0x80u : 0));
  }
  
  constexpr GenericRequirementFlags
  withExtraArgument(bool hasExtraArgument) const {
    return GenericRequirementFlags((Value & 0xBFu)
      | (hasExtraArgument ? 0x40u : 0));
  }
  
  constexpr GenericRequirementFlags
  withKind(GenericRequirementKind kind) const {
    return assert((uint8_t(kind) & 0x1Fu) == uint8_t(kind)),
      GenericRequirementFlags((Value & 0xE0u) | uint8_t(kind));
  }
  
  constexpr uint32_t getIntValue() const {
    return Value;
  }
};

enum class GenericRequirementLayoutKind : uint32_t {
  // A class constraint.
  Class = 0,
};

} // end namespace swift

#endif /* SWIFT_ABI_METADATAVALUES_H */
