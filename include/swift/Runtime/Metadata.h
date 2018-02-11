//===--- Metadata.h - Swift Language ABI Metadata Support -------*- C++ -*-===//
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
// Swift ABI for generating and uniquing metadata.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_RUNTIME_METADATA_H
#define SWIFT_RUNTIME_METADATA_H

#include <atomic>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <string.h>
#include "llvm/ADT/ArrayRef.h"
#include "swift/Runtime/Config.h"
#include "swift/ABI/MetadataValues.h"
#include "swift/ABI/System.h"
#include "swift/ABI/TrailingObjects.h"
#include "swift/Basic/Malloc.h"
#include "swift/Basic/FlaggedPointer.h"
#include "swift/Basic/RelativePointer.h"
#include "swift/Demangling/ManglingMacros.h"
#include "swift/Runtime/Unreachable.h"
#include "../../../stdlib/public/SwiftShims/HeapObject.h"
#if SWIFT_OBJC_INTEROP
#include <objc/runtime.h>
#endif
#include "llvm/Support/Casting.h"

namespace swift {

#if SWIFT_OBJC_INTEROP

  // Const cast shorthands for ObjC types.

  /// Cast to id, discarding const if necessary.
  template <typename T>
  static inline id id_const_cast(const T* value) {
    return reinterpret_cast<id>(const_cast<T*>(value));
  }

  /// Cast to Class, discarding const if necessary.
  template <typename T>
  static inline Class class_const_cast(const T* value) {
    return reinterpret_cast<Class>(const_cast<T*>(value));
  }

  /// Cast to Protocol*, discarding const if necessary.
  template <typename T>
  static inline Protocol* protocol_const_cast(const T* value) {
    return reinterpret_cast<Protocol *>(const_cast<T*>(value));
  }

  /// Cast from a CF type, discarding const if necessary.
  template <typename T>
  static inline T cf_const_cast(const void* value) {
    return reinterpret_cast<T>(const_cast<void *>(value));
  }

#endif


template <unsigned PointerSize>
struct RuntimeTarget;

template <>
struct RuntimeTarget<4> {
  using StoredPointer = uint32_t;
  using StoredSize = uint32_t;
  static constexpr size_t PointerSize = 4;
};

template <>
struct RuntimeTarget<8> {
  using StoredPointer = uint64_t;
  using StoredSize = uint64_t;
  static constexpr size_t PointerSize = 8;
};

/// In-process native runtime target.
///
/// For interactions in the runtime, this should be the equivalent of working
/// with a plain old pointer type.
struct InProcess {
  static constexpr size_t PointerSize = sizeof(uintptr_t);
  using StoredPointer = uintptr_t;
  using StoredSize = size_t;
  
  template <typename T>
  using Pointer = T*;
  
  template <typename T, bool Nullable = false>
  using FarRelativeDirectPointer = FarRelativeDirectPointer<T, Nullable>;

  template <typename T, bool Nullable = false>
  using FarRelativeIndirectablePointer =
    FarRelativeIndirectablePointer<T, Nullable>;
  
  template <typename T, bool Nullable = true>
  using RelativeDirectPointer = RelativeDirectPointer<T, Nullable>;
};

/// Represents a pointer in another address space.
///
/// This type should not have * or -> operators -- you must as a memory reader
/// to read the data at the stored address on your behalf.
template <typename Runtime, typename Pointee>
struct ExternalPointer {
  using StoredPointer = typename Runtime::StoredPointer;
  StoredPointer PointerValue;
};

/// An external process's runtime target, which may be a different architecture.
template <typename Runtime>
struct External {
  using StoredPointer = typename Runtime::StoredPointer;
  using StoredSize = typename Runtime::StoredSize;
  static constexpr size_t PointerSize = Runtime::PointerSize;
  const StoredPointer PointerValue;
  
  template <typename T>
  using Pointer = StoredPointer;
  
  template <typename T, bool Nullable = false>
  using FarRelativeDirectPointer = StoredPointer;

  template <typename T, bool Nullable = false>
  using FarRelativeIndirectablePointer = StoredSize;
  
  template <typename T, bool Nullable = true>
  using RelativeDirectPointer = int32_t;
};

/// Template for branching on native pointer types versus external ones
template <typename Runtime, template <typename> class Pointee>
using TargetMetadataPointer
  = typename Runtime::template Pointer<Pointee<Runtime>>;
  
template <typename Runtime, template <typename> class Pointee>
using ConstTargetMetadataPointer
  = typename Runtime::template Pointer<const Pointee<Runtime>>;
  
template <typename Runtime, typename T>
using TargetPointer = typename Runtime::template Pointer<T>;
  
template <typename Runtime, template <typename> class Pointee,
          bool Nullable = true>
using ConstTargetFarRelativeDirectPointer
  = typename Runtime::template FarRelativeDirectPointer<const Pointee<Runtime>,
                                                        Nullable>;

template <typename Runtime, typename Pointee, bool Nullable = true>
using TargetRelativeDirectPointer
  = typename Runtime::template RelativeDirectPointer<Pointee, Nullable>;

template <typename Runtime, typename Pointee, bool Nullable = true>
using TargetFarRelativeIndirectablePointer
  = typename Runtime::template FarRelativeIndirectablePointer<Pointee,Nullable>;

struct HeapObject;
class WeakReference;
  
template <typename Runtime> struct TargetMetadata;
using Metadata = TargetMetadata<InProcess>;

template <typename Runtime> struct TargetProtocolConformanceDescriptor;

/// Storage for an arbitrary value.  In C/C++ terms, this is an
/// 'object', because it is rooted in memory.
///
/// The context dictates what type is actually stored in this object,
/// and so this type is intentionally incomplete.
///
/// An object can be in one of two states:
///  - An uninitialized object has a completely unspecified state.
///  - An initialized object holds a valid value of the type.
struct OpaqueValue;

/// A fixed-size buffer for local values.  It is capable of owning
/// (possibly in side-allocated memory) the storage necessary
/// to hold a value of an arbitrary type.  Because it is fixed-size,
/// it can be allocated in places that must be agnostic to the
/// actual type: for example, within objects of existential type,
/// or for local variables in generic functions.
///
/// The context dictates its type, which ultimately means providing
/// access to a value witness table by which the value can be
/// accessed and manipulated.
///
/// A buffer can directly store three pointers and is pointer-aligned.
/// Three pointers is a sweet spot for Swift, because it means we can
/// store a structure containing a pointer, a size, and an owning
/// object, which is a common pattern in code due to ARC.  In a GC
/// environment, this could be reduced to two pointers without much loss.
///
/// A buffer can be in one of three states:
///  - An unallocated buffer has a completely unspecified state.
///  - An allocated buffer has been initialized so that it
///    owns uninitialized value storage for the stored type.
///  - An initialized buffer is an allocated buffer whose value
///    storage has been initialized.
struct ValueBuffer {
  void *PrivateData[NumWords_ValueBuffer];
};

/// Can a value with the given size and alignment be allocated inline?
constexpr inline bool canBeInline(size_t size, size_t alignment) {
  return size <= sizeof(ValueBuffer) && alignment <= alignof(ValueBuffer);
}

template <class T>
constexpr inline bool canBeInline() {
  return canBeInline(sizeof(T), alignof(T));
}

struct ValueWitnessTable;

/// Flags stored in the value-witness table.
class ValueWitnessFlags {
  typedef size_t int_type;
  
  // The polarity of these bits is chosen so that, when doing struct layout, the
  // flags of the field types can be mostly bitwise-or'ed together to derive the
  // flags for the struct. (The "non-inline" and "has-extra-inhabitants" bits
  // still require additional fixup.)
  enum : int_type {
    AlignmentMask = 0x0000FFFF,
    IsNonPOD =      0x00010000,
    IsNonInline =   0x00020000,
    HasExtraInhabitants = 0x00040000,
    HasSpareBits =  0x00080000,
    IsNonBitwiseTakable = 0x00100000,
    HasEnumWitnesses = 0x00200000,
    // Everything else is reserved.
  };
  int_type Data;

  constexpr ValueWitnessFlags(int_type data) : Data(data) {}
public:
  constexpr ValueWitnessFlags() : Data(0) {}

  /// The required alignment of the first byte of an object of this
  /// type, expressed as a mask of the low bits that must not be set
  /// in the pointer.
  ///
  /// This representation can be easily converted to the 'alignof'
  /// result by merely adding 1, but it is more directly useful for
  /// performing dynamic structure layouts, and it grants an
  /// additional bit of precision in a compact field without needing
  /// to switch to an exponent representation.
  ///
  /// For example, if the type needs to be 8-byte aligned, the
  /// appropriate alignment mask should be 0x7.
  size_t getAlignmentMask() const {
    return (Data & AlignmentMask);
  }
  constexpr ValueWitnessFlags withAlignmentMask(size_t alignMask) const {
    return ValueWitnessFlags((Data & ~AlignmentMask) | alignMask);
  }

  size_t getAlignment() const { return getAlignmentMask() + 1; }
  constexpr ValueWitnessFlags withAlignment(size_t alignment) const {
    return withAlignmentMask(alignment - 1);
  }

  /// True if the type requires out-of-line allocation of its storage.
  bool isInlineStorage() const { return !(Data & IsNonInline); }
  constexpr ValueWitnessFlags withInlineStorage(bool isInline) const {
    return ValueWitnessFlags((Data & ~IsNonInline) |
                               (isInline ? 0 : IsNonInline));
  }

  /// True if values of this type can be copied with memcpy and
  /// destroyed with a no-op.
  bool isPOD() const { return !(Data & IsNonPOD); }
  constexpr ValueWitnessFlags withPOD(bool isPOD) const {
    return ValueWitnessFlags((Data & ~IsNonPOD) |
                               (isPOD ? 0 : IsNonPOD));
  }
  
  /// True if values of this type can be taken with memcpy. Unlike C++ 'move',
  /// 'take' is a destructive operation that invalidates the source object, so
  /// most types can be taken with a simple bitwise copy. Only types with side
  /// table references, like @weak references, or types with opaque value
  /// semantics, like imported C++ types, are not bitwise-takable.
  bool isBitwiseTakable() const { return !(Data & IsNonBitwiseTakable); }
  constexpr ValueWitnessFlags withBitwiseTakable(bool isBT) const {
    return ValueWitnessFlags((Data & ~IsNonBitwiseTakable) |
                               (isBT ? 0 : IsNonBitwiseTakable));
  }
  /// True if this type's binary representation has extra inhabitants, that is,
  /// bit patterns that do not form valid values of the type.
  ///
  /// If true, then the extra inhabitant value witness table entries are
  /// available in this type's value witness table.
  bool hasExtraInhabitants() const { return Data & HasExtraInhabitants; }
  /// True if this type's binary representation is that of an enum, and the
  /// enum value witness table entries are available in this type's value
  /// witness table.
  bool hasEnumWitnesses() const { return Data & HasEnumWitnesses; }
  constexpr ValueWitnessFlags
  withExtraInhabitants(bool hasExtraInhabitants) const {
    return ValueWitnessFlags((Data & ~HasExtraInhabitants) |
                               (hasExtraInhabitants ? HasExtraInhabitants : 0));
  }
  constexpr ValueWitnessFlags
  withEnumWitnesses(bool hasEnumWitnesses) const {
    return ValueWitnessFlags((Data & ~HasEnumWitnesses) |
                             (hasEnumWitnesses ? HasEnumWitnesses : 0));
  }
};
  
/// Flags stored in a value-witness table with extra inhabitants.
class ExtraInhabitantFlags {
  typedef size_t int_type;
  enum : int_type {
    NumExtraInhabitantsMask = 0x7FFFFFFFU,
  };
  int_type Data;
  
  constexpr ExtraInhabitantFlags(int_type data) : Data(data) {}

public:
  constexpr ExtraInhabitantFlags() : Data(0) {}
  
  /// The number of extra inhabitants in the type's representation.
  int getNumExtraInhabitants() const { return Data & NumExtraInhabitantsMask; }
  
  constexpr ExtraInhabitantFlags
  withNumExtraInhabitants(unsigned numExtraInhabitants) const {
    return ExtraInhabitantFlags((Data & ~NumExtraInhabitantsMask) |
                                  numExtraInhabitants);
  }
};

namespace value_witness_types {

// Note that, for now, we aren't strict about 'const'.
#define WANT_ALL_VALUE_WITNESSES
#define DATA_VALUE_WITNESS(lowerId, upperId, type)
#define FUNCTION_VALUE_WITNESS(lowerId, upperId, returnType, paramTypes) \
  typedef returnType (*lowerId) paramTypes;
#define MUTABLE_VALUE_TYPE OpaqueValue *
#define IMMUTABLE_VALUE_TYPE const OpaqueValue *
#define MUTABLE_BUFFER_TYPE ValueBuffer *
#define IMMUTABLE_BUFFER_TYPE const ValueBuffer *
#define TYPE_TYPE const Metadata *
#define SIZE_TYPE size_t
#define INT_TYPE int
#define UINT_TYPE unsigned
#define VOID_TYPE void
#include "swift/ABI/ValueWitness.def"

  // Handle the data witnesses explicitly so we can use more specific
  // types for the flags enums.
  typedef size_t size;
  typedef ValueWitnessFlags flags;
  typedef size_t stride;
  typedef ExtraInhabitantFlags extraInhabitantFlags;

} // end namespace value_witness_types

/// A standard routine, suitable for placement in the value witness
/// table, for copying an opaque POD object.
SWIFT_RUNTIME_EXPORT
OpaqueValue *swift_copyPOD(OpaqueValue *dest,
                           OpaqueValue *src,
                           const Metadata *self);

struct TypeLayout;

/// A value-witness table.  A value witness table is built around
/// the requirements of some specific type.  The information in
/// a value-witness table is intended to be sufficient to lay out
/// and manipulate values of an arbitrary type.
struct ValueWitnessTable {
  // For the meaning of all of these witnesses, consult the comments
  // on their associated typedefs, above.

#define WANT_ONLY_REQUIRED_VALUE_WITNESSES
#define VALUE_WITNESS(LOWER_ID, UPPER_ID) \
  value_witness_types::LOWER_ID LOWER_ID;
#include "swift/ABI/ValueWitness.def"

  /// Would values of a type with the given layout requirements be
  /// allocated inline?
  static bool isValueInline(size_t size, size_t alignment) {
    return (size <= sizeof(ValueBuffer) &&
            alignment <= alignof(ValueBuffer));
  }

  /// Are values of this type allocated inline?
  bool isValueInline() const {
    return flags.isInlineStorage();
  }

  /// Is this type POD?
  bool isPOD() const {
    return flags.isPOD();
  }

  /// Is this type bitwise-takable?
  bool isBitwiseTakable() const {
    return flags.isBitwiseTakable();
  }

  /// Return the size of this type.  Unlike in C, this has not been
  /// padded up to the alignment; that value is maintained as
  /// 'stride'.
  size_t getSize() const {
    return size;
  }

  /// Return the stride of this type.  This is the size rounded up to
  /// be a multiple of the alignment.
  size_t getStride() const {
    return stride;
  }

  /// Return the alignment required by this type, in bytes.
  size_t getAlignment() const {
    return flags.getAlignment();
  }

  /// The alignment mask of this type.  An offset may be rounded up to
  /// the required alignment by adding this mask and masking by its
  /// bit-negation.
  ///
  /// For example, if the type needs to be 8-byte aligned, the value
  /// of this witness is 0x7.
  size_t getAlignmentMask() const {
    return flags.getAlignmentMask();
  }
  
  /// The number of extra inhabitants, that is, bit patterns that do not form
  /// valid values of the type, in this type's binary representation.
  unsigned getNumExtraInhabitants() const;

  /// Assert that this value witness table is an extra-inhabitants
  /// value witness table and return it as such.
  ///
  /// This has an awful name because it's supposed to be internal to
  /// this file.  Code outside this file should use LLVM's cast/dyn_cast.
  /// We don't want to use those here because we need to avoid accidentally
  /// introducing ABI dependencies on LLVM structures.
  const struct ExtraInhabitantsValueWitnessTable *_asXIVWT() const;

  /// Assert that this value witness table is an enum value witness table
  /// and return it as such.
  ///
  /// This has an awful name because it's supposed to be internal to
  /// this file.  Code outside this file should use LLVM's cast/dyn_cast.
  /// We don't want to use those here because we need to avoid accidentally
  /// introducing ABI dependencies on LLVM structures.
  const struct EnumValueWitnessTable *_asEVWT() const;

  /// Get the type layout record within this value witness table.
  const TypeLayout *getTypeLayout() const {
    return reinterpret_cast<const TypeLayout *>(&size);
  }
};
  
/// A value-witness table with extra inhabitants entry points.
/// These entry points are available only if the HasExtraInhabitants flag bit is
/// set in the 'flags' field.
struct ExtraInhabitantsValueWitnessTable : ValueWitnessTable {
#define WANT_ONLY_EXTRA_INHABITANT_VALUE_WITNESSES
#define VALUE_WITNESS(LOWER_ID, UPPER_ID) \
  value_witness_types::LOWER_ID LOWER_ID;
#include "swift/ABI/ValueWitness.def"

#define SET_WITNESS(NAME) base.NAME,

  constexpr ExtraInhabitantsValueWitnessTable()
    : ValueWitnessTable{}, extraInhabitantFlags(),
      storeExtraInhabitant(nullptr),
      getExtraInhabitantIndex(nullptr) {}
  constexpr ExtraInhabitantsValueWitnessTable(
                            const ValueWitnessTable &base,
                            value_witness_types::extraInhabitantFlags eif,
                            value_witness_types::storeExtraInhabitant sei,
                            value_witness_types::getExtraInhabitantIndex geii)
    : ValueWitnessTable(base),
      extraInhabitantFlags(eif),
      storeExtraInhabitant(sei),
      getExtraInhabitantIndex(geii) {}

  static bool classof(const ValueWitnessTable *table) {
    return table->flags.hasExtraInhabitants();
  }
};

/// A value-witness table with enum entry points.
/// These entry points are available only if the HasEnumWitnesses flag bit is
/// set in the 'flags' field.
struct EnumValueWitnessTable : ExtraInhabitantsValueWitnessTable {
#define WANT_ONLY_ENUM_VALUE_WITNESSES
#define VALUE_WITNESS(LOWER_ID, UPPER_ID) \
  value_witness_types::LOWER_ID LOWER_ID;
#include "swift/ABI/ValueWitness.def"

  constexpr EnumValueWitnessTable()
    : ExtraInhabitantsValueWitnessTable(),
      getEnumTag(nullptr),
      destructiveProjectEnumData(nullptr),
      destructiveInjectEnumTag(nullptr) {}
  constexpr EnumValueWitnessTable(
          const ExtraInhabitantsValueWitnessTable &base,
          value_witness_types::getEnumTag getEnumTag,
          value_witness_types::destructiveProjectEnumData destructiveProjectEnumData,
          value_witness_types::destructiveInjectEnumTag destructiveInjectEnumTag)
    : ExtraInhabitantsValueWitnessTable(base),
      getEnumTag(getEnumTag),
      destructiveProjectEnumData(destructiveProjectEnumData),
      destructiveInjectEnumTag(destructiveInjectEnumTag) {}

  static bool classof(const ValueWitnessTable *table) {
    return table->flags.hasEnumWitnesses();
  }
};

/// A type layout record. This is the subset of the value witness table that is
/// necessary to perform dependent layout of generic value types. It excludes
/// the value witness functions and includes only the size, alignment,
/// extra inhabitants, and miscellaneous flags about the type.
struct TypeLayout {
  value_witness_types::size size;
  value_witness_types::flags flags;
  value_witness_types::stride stride;

private:
  // Only available if the "hasExtraInhabitants" flag is set.
  value_witness_types::extraInhabitantFlags extraInhabitantFlags;

  void _static_assert_layout();
public:
  value_witness_types::extraInhabitantFlags getExtraInhabitantFlags() const {
    assert(flags.hasExtraInhabitants());
    return extraInhabitantFlags;
  }

  const TypeLayout *getTypeLayout() const { return this; }

  /// The number of extra inhabitants, that is, bit patterns that do not form
  /// valid values of the type, in this type's binary representation.
  unsigned getNumExtraInhabitants() const;
};

inline void TypeLayout::_static_assert_layout() {
  #define CHECK_TYPE_LAYOUT_OFFSET(FIELD)                               \
    static_assert(offsetof(ExtraInhabitantsValueWitnessTable, FIELD)    \
                    - offsetof(ExtraInhabitantsValueWitnessTable, size) \
                  == offsetof(TypeLayout, FIELD),                       \
                  "layout of " #FIELD " in TypeLayout doesn't match "   \
                  "value witness table")
  CHECK_TYPE_LAYOUT_OFFSET(size);
  CHECK_TYPE_LAYOUT_OFFSET(flags);
  CHECK_TYPE_LAYOUT_OFFSET(stride);
  CHECK_TYPE_LAYOUT_OFFSET(extraInhabitantFlags);

  #undef CHECK_TYPE_LAYOUT_OFFSET
}

inline const ExtraInhabitantsValueWitnessTable *
ValueWitnessTable::_asXIVWT() const {
  assert(ExtraInhabitantsValueWitnessTable::classof(this));
  return static_cast<const ExtraInhabitantsValueWitnessTable *>(this);
}
  
inline const EnumValueWitnessTable *
ValueWitnessTable::_asEVWT() const {
  assert(EnumValueWitnessTable::classof(this));
  return static_cast<const EnumValueWitnessTable *>(this);
}

inline unsigned ValueWitnessTable::getNumExtraInhabitants() const {
  // If the table does not have extra inhabitant witnesses, then there are zero.
  if (!flags.hasExtraInhabitants())
    return 0;
  return this->_asXIVWT()->extraInhabitantFlags.getNumExtraInhabitants();
}

inline unsigned TypeLayout::getNumExtraInhabitants() const {
  // If the table does not have extra inhabitant witnesses, then there are zero.
  if (!flags.hasExtraInhabitants())
    return 0;
  return extraInhabitantFlags.getNumExtraInhabitants();
}

// Standard value-witness tables.

// The "Int" tables are used for arbitrary POD data with the matching
// size/alignment characteristics.
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi8_);   // Builtin.Int8
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi16_);  // Builtin.Int16
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi32_);  // Builtin.Int32
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi64_);  // Builtin.Int64
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi128_); // Builtin.Int128
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi256_); // Builtin.Int256
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(Bi512_); // Builtin.Int512

// The object-pointer table can be used for arbitrary Swift refcounted
// pointer types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable VALUE_WITNESS_SYM(Bo); // Builtin.NativeObject
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable UNOWNED_VALUE_WITNESS_SYM(Bo); // unowned Builtin.NativeObject
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable WEAK_VALUE_WITNESS_SYM(Bo); // weak Builtin.NativeObject?

SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable VALUE_WITNESS_SYM(Bb); // Builtin.BridgeObject

SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable VALUE_WITNESS_SYM(Bp); // Builtin.RawPointer

#if SWIFT_OBJC_INTEROP
// The ObjC-pointer table can be used for arbitrary ObjC pointer types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable VALUE_WITNESS_SYM(BO); // Builtin.UnknownObject
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable UNOWNED_VALUE_WITNESS_SYM(BO); // unowned Builtin.UnknownObject
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable WEAK_VALUE_WITNESS_SYM(BO); // weak Builtin.UnknownObject?
#endif

// The () -> () table can be used for arbitrary function types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable
  VALUE_WITNESS_SYM(FUNCTION_MANGLING);     // () -> ()

// The @escaping () -> () table can be used for arbitrary escaping function types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable
  VALUE_WITNESS_SYM(NOESCAPE_FUNCTION_MANGLING);     // @noescape () -> ()

// The @convention(thin) () -> () table can be used for arbitrary thin function types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable
  VALUE_WITNESS_SYM(THIN_FUNCTION_MANGLING);    // @convention(thin) () -> ()

// The () table can be used for arbitrary empty types.
SWIFT_RUNTIME_EXPORT
const ValueWitnessTable VALUE_WITNESS_SYM(EMPTY_TUPLE_MANGLING);        // ()

// The table for aligned-pointer-to-pointer types.
SWIFT_RUNTIME_EXPORT
const ExtraInhabitantsValueWitnessTable METATYPE_VALUE_WITNESS_SYM(Bo); // Builtin.NativeObject.Type

/// Return the value witnesses for unmanaged pointers.
static inline const ValueWitnessTable &getUnmanagedPointerValueWitnesses() {
#if __POINTER_WIDTH__ == 64
  return VALUE_WITNESS_SYM(Bi64_);
#else
  return VALUE_WITNESS_SYM(Bi32_);
#endif
}

/// Return value witnesses for a pointer-aligned pointer type.
static inline
const ExtraInhabitantsValueWitnessTable &
getUnmanagedPointerPointerValueWitnesses() {
  return METATYPE_VALUE_WITNESS_SYM(Bo);
}

/// The header before a metadata object which appears on all type
/// metadata.  Note that heap metadata are not necessarily type
/// metadata, even for objects of a heap type: for example, objects of
/// Objective-C type possess a form of heap metadata (an Objective-C
/// Class pointer), but this metadata lacks the type metadata header.
/// This case can be distinguished using the isTypeMetadata() flag
/// on ClassMetadata.
struct TypeMetadataHeader {
  /// A pointer to the value-witnesses for this type.  This is only
  /// present for type metadata.
  const ValueWitnessTable *ValueWitnesses;
};

/// A "full" metadata pointer is simply an adjusted address point on a
/// metadata object; it points to the beginning of the metadata's
/// allocation, rather than to the canonical address point of the
/// metadata object.
template <class T> struct FullMetadata : T::HeaderType, T {
  typedef typename T::HeaderType HeaderType;

  FullMetadata() = default;
  constexpr FullMetadata(const HeaderType &header, const T &metadata)
    : HeaderType(header), T(metadata) {}
};

/// Given a canonical metadata pointer, produce the adjusted metadata pointer.
template <class T>
static inline FullMetadata<T> *asFullMetadata(T *metadata) {
  return (FullMetadata<T>*) (((typename T::HeaderType*) metadata) - 1);
}
template <class T>
static inline const FullMetadata<T> *asFullMetadata(const T *metadata) {
  return asFullMetadata(const_cast<T*>(metadata));
}

// std::result_of is busted in Xcode 5. This is a simplified reimplementation
// that isn't SFINAE-safe.
namespace {
  template<typename T> struct _ResultOf;
  
  template<typename R, typename...A>
  struct _ResultOf<R(*)(A...)> {
    using type = R;
  };
}

template <typename Runtime> struct TargetGenericMetadata;
template <typename Runtime> struct TargetClassMetadata;
template <typename Runtime> struct TargetStructMetadata;
template <typename Runtime> struct TargetOpaqueMetadata;
template <typename Runtime> struct TargetValueMetadata;
template <typename Runtime> struct TargetForeignClassMetadata;
template <typename Runtime> struct TargetTypeContextDescriptor;

// FIXME: https://bugs.swift.org/browse/SR-1155
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

/// The common structure of all type metadata.
template <typename Runtime>
struct TargetMetadata {
  using StoredPointer = typename Runtime::StoredPointer;

  constexpr TargetMetadata()
    : Kind(static_cast<StoredPointer>(MetadataKind::Class)) {}
  constexpr TargetMetadata(MetadataKind Kind)
    : Kind(static_cast<StoredPointer>(Kind)) {}
  
  /// The basic header type.
  typedef TypeMetadataHeader HeaderType;

private:
  /// The kind. Only valid for non-class metadata; getKind() must be used to get
  /// the kind value.
  StoredPointer Kind;
public:
  /// Get the metadata kind.
  MetadataKind getKind() const {
    return getEnumeratedMetadataKind(Kind);
  }
  
  /// Set the metadata kind.
  void setKind(MetadataKind kind) {
    Kind = static_cast<StoredPointer>(kind);
  }

  /// Is this a class object--the metadata record for a Swift class (which also
  /// serves as the class object), or the class object for an ObjC class (which
  /// is not metadata)?
  bool isClassObject() const {
    return static_cast<MetadataKind>(getKind()) == MetadataKind::Class;
  }
  
  /// Does the given metadata kind represent metadata for some kind of class?
  static bool isAnyKindOfClass(MetadataKind k) {
    switch (k) {
    case MetadataKind::Class:
    case MetadataKind::ObjCClassWrapper:
    case MetadataKind::ForeignClass:
      return true;

    case MetadataKind::Function:
    case MetadataKind::Struct:
    case MetadataKind::Enum:
    case MetadataKind::Optional:
    case MetadataKind::Opaque:
    case MetadataKind::Tuple:
    case MetadataKind::Existential:
    case MetadataKind::Metatype:
    case MetadataKind::ExistentialMetatype:
    case MetadataKind::HeapLocalVariable:
    case MetadataKind::HeapGenericLocalVariable:
    case MetadataKind::ErrorObject:
      return false;
    }
    
    swift_runtime_unreachable("Unhandled MetadataKind in switch.");
  }
  
  /// Is this metadata for an existential type?
  bool isAnyExistentialType() const {
    switch (getKind()) {
    case MetadataKind::ExistentialMetatype:
    case MetadataKind::Existential:
      return true;
        
    case MetadataKind::Metatype:
    case MetadataKind::Class:
    case MetadataKind::ObjCClassWrapper:
    case MetadataKind::ForeignClass:
    case MetadataKind::Struct:
    case MetadataKind::Enum:
    case MetadataKind::Optional:
    case MetadataKind::Opaque:
    case MetadataKind::Tuple:
    case MetadataKind::Function:
    case MetadataKind::HeapLocalVariable:
    case MetadataKind::HeapGenericLocalVariable:
    case MetadataKind::ErrorObject:
      return false;
    }

    swift_runtime_unreachable("Unhandled MetadataKind in switch.");
  }
  
  /// Is this either type metadata or a class object for any kind of class?
  bool isAnyClass() const {
    return isAnyKindOfClass(getKind());
  }

  const ValueWitnessTable *getValueWitnesses() const {
    return asFullMetadata(this)->ValueWitnesses;
  }

  const TypeLayout *getTypeLayout() const {
    return getValueWitnesses()->getTypeLayout();
  }

  void setValueWitnesses(const ValueWitnessTable *table) {
    asFullMetadata(this)->ValueWitnesses = table;
  }
  
  // Define forwarders for value witnesses. These invoke this metadata's value
  // witness table with itself as the 'self' parameter.
  #define WANT_ONLY_REQUIRED_VALUE_WITNESSES
  #define FUNCTION_VALUE_WITNESS(WITNESS, UPPER, RET_TYPE, PARAM_TYPES)    \
    template<typename...A>                                                 \
    _ResultOf<value_witness_types::WITNESS>::type                          \
    vw_##WITNESS(A &&...args) const {                                      \
      return getValueWitnesses()->WITNESS(std::forward<A>(args)..., this); \
    }
  #define DATA_VALUE_WITNESS(LOWER, UPPER, TYPE)
  #include "swift/ABI/ValueWitness.def"

  int vw_getExtraInhabitantIndex(const OpaqueValue *value) const  {
    return getValueWitnesses()->_asXIVWT()->getExtraInhabitantIndex(value, this);
  }
  void vw_storeExtraInhabitant(OpaqueValue *value, int index) const {
    getValueWitnesses()->_asXIVWT()->storeExtraInhabitant(value, index, this);
  }

  int vw_getEnumTag(const OpaqueValue *value) const {
    return getValueWitnesses()->_asEVWT()->getEnumTag(const_cast<OpaqueValue*>(value), this);
  }
  void vw_destructiveProjectEnumData(OpaqueValue *value) const {
    getValueWitnesses()->_asEVWT()->destructiveProjectEnumData(value, this);
  }
  void vw_destructiveInjectEnumTag(OpaqueValue *value, unsigned tag) const {
    getValueWitnesses()->_asEVWT()->destructiveInjectEnumTag(value, tag, this);
  }

  /// Allocate an out-of-line buffer if values of this type don't fit in the
  /// ValueBuffer.
  /// NOTE: This is not a box for copy-on-write existentials.
  OpaqueValue *allocateBufferIn(ValueBuffer *buffer) const;

  /// Deallocate an out-of-line buffer stored in 'buffer' if values of this type
  /// are not stored inline in the ValueBuffer.
  void deallocateBufferIn(ValueBuffer *buffer) const;

  // Allocate an out-of-line buffer box (reference counted) if values of this
  // type don't fit in the ValueBuffer.
  // NOTE: This *is* a box for copy-on-write existentials.
  OpaqueValue *allocateBoxForExistentialIn(ValueBuffer *Buffer) const;

  /// Get the nominal type descriptor if this metadata describes a nominal type,
  /// or return null if it does not.
  ConstTargetMetadataPointer<Runtime, TargetTypeContextDescriptor>
  getTypeContextDescriptor() const {
    switch (getKind()) {
    case MetadataKind::Class: {
      const auto cls = static_cast<const TargetClassMetadata<Runtime> *>(this);
      if (!cls->isTypeMetadata())
        return nullptr;
      if (cls->isArtificialSubclass())
        return nullptr;
      return cls->getDescription();
    }
    case MetadataKind::Struct:
    case MetadataKind::Enum:
    case MetadataKind::Optional:
      return static_cast<const TargetValueMetadata<Runtime> *>(this)
          ->Description;
    case MetadataKind::ForeignClass:
      return static_cast<const TargetForeignClassMetadata<Runtime> *>(this)
          ->Description;
    case MetadataKind::Opaque:
    case MetadataKind::Tuple:
    case MetadataKind::Function:
    case MetadataKind::Existential:
    case MetadataKind::ExistentialMetatype:
    case MetadataKind::Metatype:
    case MetadataKind::ObjCClassWrapper:
    case MetadataKind::HeapLocalVariable:
    case MetadataKind::HeapGenericLocalVariable:
    case MetadataKind::ErrorObject:
      return nullptr;
    }

    swift_runtime_unreachable("Unhandled MetadataKind in switch.");
  }

  /// Get the class object for this type if it has one, or return null if the
  /// type is not a class (or not a class with a class object).
  const TargetClassMetadata<Runtime> *getClassObject() const;

  /// Retrieve the generic arguments of this type, if it has any.
  ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> const *
  getGenericArgs() const {
    auto description = getTypeContextDescriptor();
    if (!description)
      return nullptr;

    auto generics = description->getGenericContext();
    if (!generics)
      return nullptr;

    auto asWords = reinterpret_cast<
      ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> const *>(this);
    return asWords + description->getGenericArgumentOffset(this);
  }

#if SWIFT_OBJC_INTEROP
  /// Get the ObjC class object for this type if it has one, or return null if
  /// the type is not a class (or not a class with a class object).
  /// This is allowed for InProcess values only.
  template <typename R = Runtime>
  typename std::enable_if<std::is_same<R, InProcess>::value, Class>::type
  getObjCClassObject() const {
    return reinterpret_cast<Class>(
      const_cast<TargetClassMetadata<InProcess>*>(
        getClassObject()));
  }
#endif

#ifndef NDEBUG
  LLVM_ATTRIBUTE_DEPRECATED(void dump() const LLVM_ATTRIBUTE_USED,
                            "Only meant for use in the debugger");
#endif

protected:
  friend struct TargetOpaqueMetadata<Runtime>;
  
  /// Metadata should not be publicly copied or moved.
  constexpr TargetMetadata(const TargetMetadata &) = default;
  TargetMetadata &operator=(const TargetMetadata &) = default;
  constexpr TargetMetadata(TargetMetadata &&) = default;
  TargetMetadata &operator=(TargetMetadata &&) = default;
};

/// The common structure of opaque metadata.  Adds nothing.
template <typename Runtime>
struct TargetOpaqueMetadata {
  typedef TypeMetadataHeader HeaderType;

  // We have to represent this as a member so we can list-initialize it.
  TargetMetadata<Runtime> base;
};
using OpaqueMetadata = TargetOpaqueMetadata<InProcess>;

// Standard POD opaque metadata.
// The "Int" metadata are used for arbitrary POD data with the
// matching characteristics.
using FullOpaqueMetadata = FullMetadata<OpaqueMetadata>;
#define BUILTIN_TYPE(Symbol, Name) \
    SWIFT_RUNTIME_EXPORT \
    const FullOpaqueMetadata METADATA_SYM(Symbol);
#include "swift/Runtime/BuiltinTypes.def"

/// The prefix on a heap metadata.
struct HeapMetadataHeaderPrefix {
  /// Destroy the object, returning the allocated size of the object
  /// or 0 if the object shouldn't be deallocated.
  SWIFT_CC(swift) void (*destroy)(SWIFT_CONTEXT HeapObject *);
};

/// The header present on all heap metadata.
struct HeapMetadataHeader : HeapMetadataHeaderPrefix, TypeMetadataHeader {
  constexpr HeapMetadataHeader(const HeapMetadataHeaderPrefix &heapPrefix,
                               const TypeMetadataHeader &typePrefix)
    : HeapMetadataHeaderPrefix(heapPrefix), TypeMetadataHeader(typePrefix) {}
};

/// The common structure of all metadata for heap-allocated types.  A
/// pointer to one of these can be retrieved by loading the 'isa'
/// field of any heap object, whether it was managed by Swift or by
/// Objective-C.  However, when loading from an Objective-C object,
/// this metadata may not have the heap-metadata header, and it may
/// not be the Swift type metadata for the object's dynamic type.
template <typename Runtime>
struct TargetHeapMetadata : TargetMetadata<Runtime> {
  typedef HeapMetadataHeader HeaderType;

  TargetHeapMetadata() = default;
  constexpr TargetHeapMetadata(const TargetMetadata<Runtime> &base)
    : TargetMetadata<Runtime>(base) {}
};
using HeapMetadata = TargetHeapMetadata<InProcess>;

template <typename Runtime>
struct TargetMethodDescriptor {
  /// The method implementation.
  TargetRelativeDirectPointer<Runtime, void> Impl;

  /// Flags describing the method.
  MethodDescriptorFlags Flags;

  // TODO: add method types or anything else needed for reflection.
};

/// Header for a class vtable descriptor. This is a variable-sized
/// structure that describes how to find and parse a vtable
/// within the type metadata for a class.
template <typename Runtime>
struct TargetVTableDescriptorHeader {
  using StoredPointer = typename Runtime::StoredPointer;

private:
  /// The offset of the vtable for this class in its metadata, if any,
  /// in words.
  ///
  /// If this class has a resilient superclass, this offset is relative to the
  /// the start of the immediate class's metadata. Otherwise, it is relative
  /// to the metadata address point.
  uint32_t VTableOffset;

public:
  /// The number of vtable entries. This is the number of MethodDescriptor
  /// records following the vtable header in the class's nominal type
  /// descriptor, which is equal to the number of words this subclass's vtable
  /// entries occupy in instantiated class metadata.
  uint32_t VTableSize;

  uint32_t getVTableOffset(const TargetClassMetadata<Runtime> *metadata) const {
    const auto *description = metadata->getDescription();
    if (description->hasResilientSuperclass())
      return metadata->SuperClass->getSizeInWords() + VTableOffset;
    return VTableOffset;
  }
};

typedef SWIFT_CC(swift) void (*ClassIVarDestroyer)(SWIFT_CONTEXT HeapObject *);

/// The structure of all class metadata.  This structure is embedded
/// directly within the class's heap metadata structure and therefore
/// cannot be extended without an ABI break.
///
/// Note that the layout of this type is compatible with the layout of
/// an Objective-C class.
template <typename Runtime>
struct TargetClassMetadata : public TargetHeapMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  using StoredSize = typename Runtime::StoredSize;
  friend class ReflectionContext;
  TargetClassMetadata() = default;
  constexpr TargetClassMetadata(const TargetHeapMetadata<Runtime> &base,
             ConstTargetMetadataPointer<Runtime, swift::TargetClassMetadata> superClass,
             StoredPointer data,
             ClassFlags flags,
             StoredPointer ivarDestroyer,
             StoredPointer size, StoredPointer addressPoint,
             StoredPointer alignMask,
             StoredPointer classSize, StoredPointer classAddressPoint)
    : TargetHeapMetadata<Runtime>(base), SuperClass(superClass),
      CacheData {0, 0}, Data(data),
      Flags(flags), InstanceAddressPoint(addressPoint),
      InstanceSize(size), InstanceAlignMask(alignMask),
      Reserved(0), ClassSize(classSize), ClassAddressPoint(classAddressPoint),
      Description(nullptr), IVarDestroyer(ivarDestroyer) {}

  // Description's copy ctor is deleted so we have to do this the hard way.
  TargetClassMetadata(const TargetClassMetadata &other)
      : TargetHeapMetadata<Runtime>(other),
        SuperClass(other.SuperClass),
        CacheData{other.CacheData[0], other.CacheData[1]},
        Data(other.Data), Flags(other.Flags),
        InstanceAddressPoint(other.InstanceAddressPoint),
        InstanceSize(other.InstanceSize),
        InstanceAlignMask(other.InstanceAlignMask), Reserved(other.Reserved),
        ClassSize(other.ClassSize), ClassAddressPoint(other.ClassAddressPoint),
        Description(other.Description), IVarDestroyer(other.IVarDestroyer) {}

  /// The metadata for the superclass.  This is null for the root class.
  ConstTargetMetadataPointer<Runtime, swift::TargetClassMetadata> SuperClass;

  /// The cache data is used for certain dynamic lookups; it is owned
  /// by the runtime and generally needs to interoperate with
  /// Objective-C's use.
  StoredPointer CacheData[2];

  /// The data pointer is used for out-of-line metadata and is
  /// generally opaque, except that the compiler sets the low bit in
  /// order to indicate that this is a Swift metatype and therefore
  /// that the type metadata header is present.
  StoredPointer Data;
  
  static constexpr StoredPointer offsetToData() {
    return offsetof(TargetClassMetadata, Data);
  }

  /// Is this object a valid swift type metadata?
  bool isTypeMetadata() const {
    return (Data & SWIFT_CLASS_IS_SWIFT_MASK);
  }
  /// A different perspective on the same bit
  bool isPureObjC() const {
    return !isTypeMetadata();
  }

private:
  // The remaining fields are valid only when isTypeMetadata().
  // The Objective-C runtime knows the offsets to some of these fields.
  // Be careful when changing them.

  /// Swift-specific class flags.
  ClassFlags Flags;

  /// The address point of instances of this type.
  uint32_t InstanceAddressPoint;

  /// The required size of instances of this type.
  /// 'InstanceAddressPoint' bytes go before the address point;
  /// 'InstanceSize - InstanceAddressPoint' bytes go after it.
  uint32_t InstanceSize;

  /// The alignment mask of the address point of instances of this type.
  uint16_t InstanceAlignMask;

  /// Reserved for runtime use.
  uint16_t Reserved;

  /// The total size of the class object, including prefix and suffix
  /// extents.
  uint32_t ClassSize;

  /// The offset of the address point within the class object.
  uint32_t ClassAddressPoint;

  /// An out-of-line Swift-specific description of the type, or null
  /// if this is an artificial subclass.  We currently provide no
  /// supported mechanism for making a non-artificial subclass
  /// dynamically.
  ConstTargetMetadataPointer<Runtime, TargetTypeContextDescriptor> Description;

  /// A function for destroying instance variables, used to clean up
  /// after an early return from a constructor.
  StoredPointer IVarDestroyer;

  // After this come the class members, laid out as follows:
  //   - class members for the superclass (recursively)
  //   - metadata reference for the parent, if applicable
  //   - generic parameters for this class
  //   - class variables (if we choose to support these)
  //   - "tabulated" virtual methods

public:
  ConstTargetMetadataPointer<Runtime, TargetTypeContextDescriptor>
  getDescription() const {
    assert(isTypeMetadata());
    return Description;
  }

  void setDescription(const TargetTypeContextDescriptor<Runtime> *
                      description) {
    Description = description;
  }

  /// Only valid if the target is in-process.
  ClassIVarDestroyer getIVarDestroyer() const {
    assert(isTypeMetadata());
    return reinterpret_cast<ClassIVarDestroyer>(IVarDestroyer);
  }

  /// Is this class an artificial subclass, such as one dynamically
  /// created for various dynamic purposes like KVO?
  bool isArtificialSubclass() const {
    assert(isTypeMetadata());
    return Description == 0;
  }
  void setArtificialSubclass() {
    assert(isTypeMetadata());
    Description = 0;
  }

  ClassFlags getFlags() const {
    assert(isTypeMetadata());
    return Flags;
  }
  void setFlags(ClassFlags flags) {
    assert(isTypeMetadata());
    Flags = flags;
  }

  StoredSize getInstanceSize() const {
    assert(isTypeMetadata());
    return InstanceSize;
  }
  void setInstanceSize(StoredSize size) {
    assert(isTypeMetadata());
    InstanceSize = size;
  }

  StoredPointer getInstanceAddressPoint() const {
    assert(isTypeMetadata());
    return InstanceAddressPoint;
  }
  void setInstanceAddressPoint(StoredSize size) {
    assert(isTypeMetadata());
    InstanceAddressPoint = size;
  }

  StoredPointer getInstanceAlignMask() const {
    assert(isTypeMetadata());
    return InstanceAlignMask;
  }
  void setInstanceAlignMask(StoredSize mask) {
    assert(isTypeMetadata());
    InstanceAlignMask = mask;
  }

  StoredPointer getClassSize() const {
    assert(isTypeMetadata());
    return ClassSize;
  }
  void setClassSize(StoredSize size) {
    assert(isTypeMetadata());
    ClassSize = size;
  }

  StoredPointer getClassAddressPoint() const {
    assert(isTypeMetadata());
    return ClassAddressPoint;
  }
  void setClassAddressPoint(StoredSize offset) {
    assert(isTypeMetadata());
    ClassAddressPoint = offset;
  }

  uint16_t getRuntimeReservedData() const {
    assert(isTypeMetadata());
    return Reserved;
  }
  void setRuntimeReservedData(uint16_t data) {
    assert(isTypeMetadata());
    Reserved = data;
  }

  /// Get a pointer to the field offset vector, if present, or null.
  const StoredPointer *getFieldOffsets() const {
    assert(isTypeMetadata());
    auto offset = getDescription()->Class.getFieldOffsetVectorOffset(this);
    if (offset == 0)
      return nullptr;
    auto asWords = reinterpret_cast<const void * const*>(this);
    return reinterpret_cast<const StoredPointer *>(asWords + offset);
  }
  
  /// Get a pointer to the field type vector, if present, or null.
  const FieldType *getFieldTypes() const {
    assert(isTypeMetadata());
    auto *getter = getDescription()->Class.GetFieldTypes.get();
    if (!getter)
      return nullptr;
    
    return getter(this);
  }

  uint32_t getSizeInWords() const {
    assert(isTypeMetadata());
    uint32_t size = getClassSize() - getClassAddressPoint();
    assert(size % sizeof(StoredPointer) == 0);
    return size / sizeof(StoredPointer);
  }

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Class;
  }
};
using ClassMetadata = TargetClassMetadata<InProcess>;

/// The structure of metadata for heap-allocated local variables.
/// This is non-type metadata.
template <typename Runtime>
struct TargetHeapLocalVariableMetadata
  : public TargetHeapMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  uint32_t OffsetToFirstCapture;
  TargetPointer<Runtime, const char> CaptureDescription;

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::HeapLocalVariable;
  }
  constexpr TargetHeapLocalVariableMetadata()
      : TargetHeapMetadata<Runtime>(MetadataKind::HeapLocalVariable),
        OffsetToFirstCapture(0), CaptureDescription(nullptr) {}
};
using HeapLocalVariableMetadata
  = TargetHeapLocalVariableMetadata<InProcess>;

/// The structure of wrapper metadata for Objective-C classes.  This
/// is used as a type metadata pointer when the actual class isn't
/// Swift-compiled.
template <typename Runtime>
struct TargetObjCClassWrapperMetadata : public TargetMetadata<Runtime> {
  ConstTargetMetadataPointer<Runtime, TargetClassMetadata> Class;

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::ObjCClassWrapper;
  }
};
using ObjCClassWrapperMetadata
  = TargetObjCClassWrapperMetadata<InProcess>;

/// The structure of metadata for foreign types where the source
/// language doesn't provide any sort of more interesting metadata for
/// us to use.
template <typename Runtime>
struct TargetForeignTypeMetadata : public TargetMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  using StoredSize = typename Runtime::StoredSize;
  using InitializationFunction_t =
    void (TargetForeignTypeMetadata<Runtime> *selectedMetadata);
  using RuntimeMetadataPointer =
      ConstTargetMetadataPointer<Runtime, swift::TargetForeignTypeMetadata>;

  /// An invasive cache for the runtime-uniqued lookup structure that is stored
  /// in the header prefix of foreign metadata records.
  ///
  /// Prior to initialization, as emitted by the compiler, this contains the
  /// initialization flags.
  /// After initialization, it holds a pointer to the actual, runtime-uniqued
  /// metadata for this type.
  struct CacheValue {
    StoredSize Value;
    
    /// Work around a bug in libstdc++'s std::atomic that requires the type to
    /// be default-constructible.
    CacheValue() = default;
    
    explicit CacheValue(RuntimeMetadataPointer p)
      : Value(reinterpret_cast<StoredSize>(p))
    {}

    /// Various flags. The largest flag bit should be less than 4096 so that
    /// a flag set is distinguishable from a valid pointer.
    enum : StoredSize {
      /// This metadata has an initialization callback function.  If
      /// this flag is not set, the metadata object needn't actually
      /// have a InitializationFunction field, and that field will be
      /// undefined.
      HasInitializationFunction = 0x1,
      
      LargestFlagMask = 0xFFF,
    };    

    /// True if the metadata record associated with this cache has not been
    /// initialized, so contains a flag set describing parameters to the
    /// initialization operation. isFlags() == !isInitialized()
    bool isFlags() const {
      return Value <= LargestFlagMask;
    }
    /// True if the metadata record associated with this cache has an
    /// initialization function which must be run if it is picked as the
    /// canonical metadata record for its key.
    ///
    /// Undefined if !isFlags().
    bool hasInitializationFunction() const {
      assert(isFlags());
      return Value & HasInitializationFunction;
    }
    
    /// True if the metadata record associated with this cache has been
    /// initialized, so the cache contains an absolute pointer to the
    /// canonical metadata record for its key. isInitialized() == !isFlags()
    bool isInitialized() const {
      return !isFlags();
    }
    
    /// Gets the cached pointer to the unique canonical metadata record for
    /// this metadata record's key.
    ///
    /// Undefined if !isInitialized().
    RuntimeMetadataPointer getCachedUniqueMetadata() const {
      assert(isInitialized());
      return RuntimeMetadataPointer(Value);
    }
  };


  /// Foreign type metadata may have extra header fields depending on
  /// the flags.
  struct HeaderPrefix {
    /// An optional callback performed when a particular metadata object
    /// is chosen as the unique structure.
    ///
    /// If there is no initialization function, this metadata record can be
    /// assumed to be immutable (except for the \c Unique invasive cache
    /// field). The field is not present unless the HasInitializationFunction
    /// flag is set.
    RelativeDirectPointer<InitializationFunction_t> InitializationFunction;
    
    /// The uniquing key for the metadata record. Metadata records with the
    /// same Name string are considered equivalent by the runtime, and the
    /// runtime will pick one to be canonical.
    RelativeDirectPointer<const char> Name;

    mutable std::atomic<CacheValue> Cache;
  };

  struct HeaderType : HeaderPrefix, TypeMetadataHeader {};

  TargetPointer<Runtime, const char> getName() const {
    return reinterpret_cast<TargetPointer<Runtime, const char>>(
      asFullMetadata(this)->Name.get());
  }

  CacheValue getCacheValue() const {
    /// NB: This can be a relaxed-order load if there is no initialization
    /// function. On platforms Swift currently targets, consume is no more
    /// expensive than relaxed, so there's no reason to branch here (and LLVM
    /// isn't smart enough to eliminate it when it's not needed).
    ///
    /// A port to a platform where relaxed is significantly less expensive than
    /// consume (historically, Alpha) would probably want to preserve the
    /// 'hasInitializationFunction' bit in its own word to be able to avoid
    /// the consuming load when not needed.
    return asFullMetadata(this)->Cache
      .load(SWIFT_MEMORY_ORDER_CONSUME);
  }

  void setCachedUniqueMetadata(RuntimeMetadataPointer unique) const {
    auto cache = getCacheValue();

    // If the cache was already set to a pointer, we're done. We ought to
    // converge on a single unique pointer.
    if (cache.isInitialized()) {
      assert(cache.getCachedUniqueMetadata() == unique
             && "already set unique metadata to something else");
      return;
    }
    
    auto newCache = CacheValue(unique);

    // If there is no initialization function, this can be a relaxed store.
    if (cache.hasInitializationFunction())
      asFullMetadata(this)->Cache.store(newCache, std::memory_order_relaxed);
    
    // Otherwise, we need a release store to publish the result of
    // initialization.
    else
      asFullMetadata(this)->Cache.store(newCache, std::memory_order_release);
  }
  
  /// Return the initialization function for this metadata record.
  ///
  /// As a prerequisite, the metadata record must not have been initialized yet,
  /// and must have an initialization function to begin with, otherwise the
  /// result is undefined.
  InitializationFunction_t *getInitializationFunction() const {
#ifndef NDEBUG
    auto cache = getCacheValue();
    assert(cache.hasInitializationFunction());
#endif

    return asFullMetadata(this)->InitializationFunction;
  }
};
using ForeignTypeMetadata = TargetForeignTypeMetadata<InProcess>;

/// The structure of metadata objects for foreign class types.
/// A foreign class is a foreign type with reference semantics and
/// Swift-supported reference counting.  Generally this requires
/// special logic in the importer.
///
/// We assume for now that foreign classes are entirely opaque
/// to Swift introspection.
template <typename Runtime>
struct TargetForeignClassMetadata
  : public TargetForeignTypeMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;

  /// An out-of-line description of the type.
  const TargetTypeContextDescriptor<Runtime> *Description;

  /// The superclass of the foreign class, if any.
  ConstTargetMetadataPointer<Runtime, swift::TargetForeignClassMetadata>
    SuperClass;

  /// Reserved space.  For now, these should be zero-initialized.
  StoredPointer Reserved[3];

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::ForeignClass;
  }
};
using ForeignClassMetadata = TargetForeignClassMetadata<InProcess>;

/// The common structure of metadata for structs and enums.
template <typename Runtime>
struct TargetValueMetadata : public TargetMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  TargetValueMetadata(MetadataKind Kind,
                      const TargetTypeContextDescriptor<Runtime> *description)
      : TargetMetadata<Runtime>(Kind), Description(description) {}

  /// An out-of-line description of the type.
  const TargetTypeContextDescriptor<Runtime> *Description;

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Struct
      || metadata->getKind() == MetadataKind::Enum
      || metadata->getKind() == MetadataKind::Optional;
  }

  const TargetTypeContextDescriptor<Runtime> *getDescription() const {
    return Description;
  }
};
using ValueMetadata = TargetValueMetadata<InProcess>;

/// The structure of type metadata for structs.
template <typename Runtime>
struct TargetStructMetadata : public TargetValueMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  using TargetValueMetadata<Runtime>::TargetValueMetadata;
  
  /// Get a pointer to the field offset vector, if present, or null.
  const StoredPointer *getFieldOffsets() const {
    auto offset = this->Description->Struct.FieldOffsetVectorOffset;
    if (offset == 0)
      return nullptr;
    auto asWords = reinterpret_cast<const void * const*>(this);
    return reinterpret_cast<const StoredPointer *>(asWords + offset);
  }
  
  /// Get a pointer to the field type vector, if present, or null.
  const FieldType *getFieldTypes() const {
    auto *getter = this->Description->Struct.GetFieldTypes.get();
    if (!getter)
      return nullptr;
    
    return getter(this);
  }

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Struct;
  }
};
using StructMetadata = TargetStructMetadata<InProcess>;

/// The structure of type metadata for enums.
template <typename Runtime>
struct TargetEnumMetadata : public TargetValueMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  using StoredSize = typename Runtime::StoredSize;
  using TargetValueMetadata<Runtime>::TargetValueMetadata;

  /// True if the metadata records the size of the payload area.
  bool hasPayloadSize() const {
    return this->Description->Enum.hasPayloadSizeOffset();
  }

  /// Retrieve the size of the payload area.
  ///
  /// `hasPayloadSize` must be true for this to be valid.
  StoredSize getPayloadSize() const {
    assert(hasPayloadSize());
    auto offset = this->Description->Enum.getPayloadSizeOffset();
    const StoredSize *asWords = reinterpret_cast<const StoredSize *>(this);
    asWords += offset;
    return *asWords;
  }

  StoredSize &getPayloadSize() {
    assert(hasPayloadSize());
    auto offset = this->Description->Enum.getPayloadSizeOffset();
    StoredSize *asWords = reinterpret_cast<StoredSize *>(this);
    asWords += offset;
    return *asWords;
  }

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Enum
      || metadata->getKind() == MetadataKind::Optional;
  }
};
using EnumMetadata = TargetEnumMetadata<InProcess>;

/// The structure of function type metadata.
template <typename Runtime>
struct TargetFunctionTypeMetadata : public TargetMetadata<Runtime> {
  using StoredSize = typename Runtime::StoredSize;
  using Parameter = ConstTargetMetadataPointer<Runtime, swift::TargetMetadata>;

  TargetFunctionTypeFlags<StoredSize> Flags;

  /// The type metadata for the result type.
  ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> ResultType;

  Parameter *getParameters() { return reinterpret_cast<Parameter *>(this + 1); }

  const Parameter *getParameters() const {
    return reinterpret_cast<const Parameter *>(this + 1);
  }

  Parameter getParameter(unsigned index) const {
    assert(index < getNumParameters());
    return getParameters()[index];
  }

  ParameterFlags getParameterFlags(unsigned index) const {
    assert(index < getNumParameters());
    auto flags = hasParameterFlags() ? getParameterFlags()[index] : 0;
    return ParameterFlags::fromIntValue(flags);
  }

  StoredSize getNumParameters() const {
    return Flags.getNumParameters();
  }
  FunctionMetadataConvention getConvention() const {
    return Flags.getConvention();
  }
  bool throws() const { return Flags.throws(); }
  bool hasParameterFlags() const { return Flags.hasParameterFlags(); }
  bool isEscaping() const { return Flags.isEscaping(); }

  static constexpr StoredSize OffsetToFlags = sizeof(TargetMetadata<Runtime>);

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Function;
  }

  uint32_t *getParameterFlags() {
    return reinterpret_cast<uint32_t *>(getParameters() + getNumParameters());
  }

  const uint32_t *getParameterFlags() const {
    return reinterpret_cast<const uint32_t *>(getParameters() +
                                              getNumParameters());
  }
};
using FunctionTypeMetadata = TargetFunctionTypeMetadata<InProcess>;

/// The structure of metadata for metatypes.
template <typename Runtime>
struct TargetMetatypeMetadata : public TargetMetadata<Runtime> {
  /// The type metadata for the element.
  ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> InstanceType;

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Metatype;
  }
};
using MetatypeMetadata = TargetMetatypeMetadata<InProcess>;

/// The structure of tuple type metadata.
template <typename Runtime>
struct TargetTupleTypeMetadata : public TargetMetadata<Runtime> {
  using StoredSize = typename Runtime::StoredSize;
  TargetTupleTypeMetadata() = default;
  constexpr TargetTupleTypeMetadata(const TargetMetadata<Runtime> &base,
                                    StoredSize numElements,
                                    TargetPointer<Runtime, const char> labels)
    : TargetMetadata<Runtime>(base),
      NumElements(numElements),
      Labels(labels) {}

  /// The number of elements.
  StoredSize NumElements;

  /// The labels string;  see swift_getTupleTypeMetadata.
  TargetPointer<Runtime, const char> Labels;

  struct Element {
    /// The type of the element.
    ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> Type;

    /// The offset of the tuple element within the tuple.
    StoredSize Offset;

    OpaqueValue *findIn(OpaqueValue *tuple) const {
      return (OpaqueValue*) (((char*) tuple) + Offset);
    }
  };

  Element *getElements() {
    return reinterpret_cast<Element*>(this + 1);
  }

  const Element *getElements() const {
    return reinterpret_cast<const Element*>(this + 1);
  }

  const Element &getElement(unsigned i) const {
    return getElements()[i];
  }

  Element &getElement(unsigned i) {
    return getElements()[i];
  }

  static constexpr StoredSize OffsetToNumElements = sizeof(TargetMetadata<Runtime>);

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Tuple;
  }
};
using TupleTypeMetadata = TargetTupleTypeMetadata<InProcess>;
  
/// The standard metadata for the empty tuple type.
SWIFT_RUNTIME_EXPORT
const
  FullMetadata<TupleTypeMetadata> METADATA_SYM(EMPTY_TUPLE_MANGLING);

template <typename Runtime> struct TargetProtocolDescriptor;
  
/// An array of protocol descriptors with a header and tail-allocated elements.
template <typename Runtime>
struct TargetProtocolDescriptorList {
  using StoredPointer = typename Runtime::StoredPointer;
  StoredPointer NumProtocols;

  ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptor> *
  getProtocols() {
    return reinterpret_cast<
      ConstTargetMetadataPointer<
        Runtime, TargetProtocolDescriptor> *>(this + 1);
  }
  
  ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptor> const *
  getProtocols() const {
    return reinterpret_cast<
      ConstTargetMetadataPointer<
        Runtime, TargetProtocolDescriptor> const *>(this + 1);
  }
  
  ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptor> const &
  operator[](size_t i) const {
    return getProtocols()[i];
  }
  
  ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptor> &
  operator[](size_t i) {
    return getProtocols()[i];
  }

  constexpr TargetProtocolDescriptorList() : NumProtocols(0) {}
  
protected:
  constexpr TargetProtocolDescriptorList(StoredPointer NumProtocols)
    : NumProtocols(NumProtocols) {}
};
using ProtocolDescriptorList = TargetProtocolDescriptorList<InProcess>;
  
/// A literal class for creating constant protocol descriptors in the runtime.
template<typename Runtime, uintptr_t NUM_PROTOCOLS>
struct TargetLiteralProtocolDescriptorList
  : TargetProtocolDescriptorList<Runtime> {
  const TargetProtocolDescriptorList<Runtime> *Protocols[NUM_PROTOCOLS];
  
  template<typename...DescriptorPointers>
  constexpr TargetLiteralProtocolDescriptorList(DescriptorPointers...elements)
    : TargetProtocolDescriptorList<Runtime>(NUM_PROTOCOLS),
      Protocols{elements...}
  {}
};
using LiteralProtocolDescriptorList = TargetProtocolDescriptorList<InProcess>;

template <typename Runtime>
struct TargetProtocolRequirement {
  ProtocolRequirementFlags Flags;
  // TODO: name, type

  /// The optional default implementation of the protocol.
  RelativeDirectPointer<void, /*nullable*/ true> DefaultImplementation;
};

using ProtocolRequirement = TargetProtocolRequirement<InProcess>;

/// A protocol descriptor. This is not type metadata, but is referenced by
/// existential type metadata records to describe a protocol constraint.
/// Its layout is compatible with the Objective-C runtime's 'protocol_t' record
/// layout.
template <typename Runtime>
struct TargetProtocolDescriptor {
  using StoredPointer = typename Runtime::StoredPointer;
  /// Unused by the Swift runtime.
  TargetPointer<Runtime, const void> _ObjC_Isa;
  
  /// The mangled name of the protocol.
  TargetPointer<Runtime, const char> Name;
  
  /// The list of protocols this protocol refines.
  ConstTargetMetadataPointer<Runtime, TargetProtocolDescriptorList>
    InheritedProtocols;
  
  /// Unused by the Swift runtime.
  TargetPointer<Runtime, const void>
    _ObjC_InstanceMethods,
    _ObjC_ClassMethods,
    _ObjC_OptionalInstanceMethods,
    _ObjC_OptionalClassMethods,
    _ObjC_InstanceProperties;
  
  /// Size of the descriptor record.
  uint32_t DescriptorSize;
  
  /// Additional flags.
  ProtocolDescriptorFlags Flags;

  /// The number of non-defaultable requirements in the protocol.
  uint16_t NumMandatoryRequirements;

  /// The number of requirements described by the Requirements array.
  /// If any requirements beyond MinimumWitnessTableSizeInWords are present
  /// in the witness table template, they will be not be overwritten with
  /// defaults.
  uint16_t NumRequirements;

  /// Requirement descriptions.
  RelativeDirectPointer<TargetProtocolRequirement<Runtime>> Requirements;

  /// The superclass of which all conforming types must be a subclass.
  RelativeDirectPointer<const TargetClassMetadata<Runtime>, /*Nullable=*/true>
    Superclass;

  /// Associated type names, as a space-separated list in the same order
  /// as the requirements.
  RelativeDirectPointer<const char, /*Nullable=*/true> AssociatedTypeNames;

  void *getDefaultWitness(unsigned index) const {
    return Requirements.get()[index].DefaultImplementation.get();
  }

  // This is only used in unittests/Metadata.cpp.
  constexpr TargetProtocolDescriptor<Runtime>(const char *Name,
                      const TargetProtocolDescriptorList<Runtime> *Inherited,
                      ProtocolDescriptorFlags Flags)
    : _ObjC_Isa(nullptr), Name(Name), InheritedProtocols(Inherited),
      _ObjC_InstanceMethods(nullptr), _ObjC_ClassMethods(nullptr),
      _ObjC_OptionalInstanceMethods(nullptr),
      _ObjC_OptionalClassMethods(nullptr),
      _ObjC_InstanceProperties(nullptr),
      DescriptorSize(sizeof(TargetProtocolDescriptor<Runtime>)),
      Flags(Flags),
      NumMandatoryRequirements(0),
      NumRequirements(0),
      Requirements(nullptr),
      Superclass(nullptr),
      AssociatedTypeNames(nullptr)
  {}

#ifndef NDEBUG
  LLVM_ATTRIBUTE_DEPRECATED(void dump() const LLVM_ATTRIBUTE_USED,
                            "only for use in the debugger");
#endif
};
using ProtocolDescriptor = TargetProtocolDescriptor<InProcess>;
  
/// A witness table for a protocol.
///
/// With the exception of the initial protocol conformance descriptor,
/// the layout of a witness table is dependent on the protocol being
/// represented.
template <typename Runtime>
struct TargetWitnessTable {
  /// The protocol conformance descriptor from which this witness table
  /// was generated.
  const TargetProtocolConformanceDescriptor<Runtime> *Description;
};

using WitnessTable = TargetWitnessTable<InProcess>;

/// The basic layout of an opaque (non-class-bounded) existential type.
template <typename Runtime>
struct TargetOpaqueExistentialContainer {
  ValueBuffer Buffer;
  const TargetMetadata<Runtime> *Type;
  // const void *WitnessTables[];

  const TargetWitnessTable<Runtime> **getWitnessTables() {
    return reinterpret_cast<const TargetWitnessTable<Runtime> **>(this + 1);
  }

  const TargetWitnessTable<Runtime> * const *getWitnessTables() const {
    return reinterpret_cast<const TargetWitnessTable<Runtime> * const *>(
                                                                      this + 1);
  }

  void copyTypeInto(swift::TargetOpaqueExistentialContainer<Runtime> *dest,
                    unsigned numTables) const {
    dest->Type = Type;
    for (unsigned i = 0; i != numTables; ++i)
      dest->getWitnessTables()[i] = getWitnessTables()[i];
  }
};
using OpaqueExistentialContainer
  = TargetOpaqueExistentialContainer<InProcess>;

/// The basic layout of a class-bounded existential type.
template <typename ContainedValue>
struct ClassExistentialContainerImpl {
  ContainedValue Value;

  const WitnessTable **getWitnessTables() {
    return reinterpret_cast<const WitnessTable**>(this + 1);
  }
  const WitnessTable * const *getWitnessTables() const {
    return reinterpret_cast<const WitnessTable* const *>(this + 1);
  }

  void copyTypeInto(ClassExistentialContainerImpl *dest,
                    unsigned numTables) const {
    for (unsigned i = 0; i != numTables; ++i)
      dest->getWitnessTables()[i] = getWitnessTables()[i];
  }
};
using ClassExistentialContainer = ClassExistentialContainerImpl<void *>;
using WeakClassExistentialContainer =
  ClassExistentialContainerImpl<WeakReference>;

/// The possible physical representations of existential types.
enum class ExistentialTypeRepresentation {
  /// The type uses an opaque existential representation.
  Opaque,
  /// The type uses a class existential representation.
  Class,
  /// The type uses the Error boxed existential representation.
  Error,
};

/// The structure of existential type metadata.
template <typename Runtime>
struct TargetExistentialTypeMetadata : public TargetMetadata<Runtime> {
  using StoredPointer = typename Runtime::StoredPointer;
  /// The number of witness tables and class-constrained-ness of the type.
  ExistentialTypeFlags Flags;
  /// The protocol constraints.
  TargetProtocolDescriptorList<Runtime> Protocols;
  
  /// NB: Protocols has a tail-emplaced array; additional fields cannot follow.
  
  constexpr TargetExistentialTypeMetadata()
    : TargetMetadata<Runtime>(MetadataKind::Existential),
      Flags(ExistentialTypeFlags()), Protocols() {}
  
  /// Get the representation form this existential type uses.
  ExistentialTypeRepresentation getRepresentation() const;
  
  /// True if it's valid to take ownership of the value in the existential
  /// container if we own the container.
  bool mayTakeValue(const OpaqueValue *container) const;
  
  /// Clean up an existential container whose value is uninitialized.
  void deinitExistentialContainer(OpaqueValue *container) const;
  
  /// Project the value pointer from an existential container of the type
  /// described by this metadata.
  const OpaqueValue *projectValue(const OpaqueValue *container) const;
  
  OpaqueValue *projectValue(OpaqueValue *container) const {
    return const_cast<OpaqueValue *>(projectValue((const OpaqueValue*)container));
  }
  /// Get the dynamic type from an existential container of the type described
  /// by this metadata.
  const TargetMetadata<Runtime> *
  getDynamicType(const OpaqueValue *container) const;
  
  /// Get a witness table from an existential container of the type described
  /// by this metadata.
  const TargetWitnessTable<Runtime> * getWitnessTable(
                                                  const OpaqueValue *container,
                                                  unsigned i) const;

  /// Return true iff all the protocol constraints are @objc.
  bool isObjC() const {
    return isClassBounded() && Flags.getNumWitnessTables() == 0;
  }

  bool isClassBounded() const {
    return Flags.getClassConstraint() == ProtocolClassConstraint::Class;
  }

  const TargetMetadata<Runtime> *getSuperclassConstraint() const {
    if (!Flags.hasSuperclassConstraint())
      return nullptr;

    // Get a pointer to tail-allocated storage for this metadata record.
    auto Pointer = reinterpret_cast<
      ConstTargetMetadataPointer<Runtime, TargetMetadata> const *>(this + 1);

    // The superclass immediately follows the list of protocol descriptors.
    return Pointer[Protocols.NumProtocols];
  }

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::Existential;
  }

  static constexpr StoredPointer
  OffsetToNumProtocols = sizeof(TargetMetadata<Runtime>) + sizeof(ExistentialTypeFlags);

};
using ExistentialTypeMetadata
  = TargetExistentialTypeMetadata<InProcess>;

/// The basic layout of an existential metatype type.
template <typename Runtime>
struct TargetExistentialMetatypeContainer {
  const TargetMetadata<Runtime> *Value;

  const TargetWitnessTable<Runtime> **getWitnessTables() {
    return reinterpret_cast<const TargetWitnessTable<Runtime>**>(this + 1);
  }
  const TargetWitnessTable<Runtime> * const *getWitnessTables() const {
    return reinterpret_cast<const TargetWitnessTable<Runtime>* const *>(this + 1);
  }

  void copyTypeInto(TargetExistentialMetatypeContainer *dest,
                    unsigned numTables) const {
    for (unsigned i = 0; i != numTables; ++i)
      dest->getWitnessTables()[i] = getWitnessTables()[i];
  }
};
using ExistentialMetatypeContainer
  = TargetExistentialMetatypeContainer<InProcess>;

/// The structure of metadata for existential metatypes.
template <typename Runtime>
struct TargetExistentialMetatypeMetadata
  : public TargetMetadata<Runtime> {
  /// The type metadata for the element.
  ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> InstanceType;

  /// The number of witness tables and class-constrained-ness of the
  /// underlying type.
  ExistentialTypeFlags Flags;

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::ExistentialMetatype;
  }

  /// Return true iff all the protocol constraints are @objc.
  bool isObjC() const {
    return isClassBounded() && Flags.getNumWitnessTables() == 0;
  }

  bool isClassBounded() const {
    return Flags.getClassConstraint() == ProtocolClassConstraint::Class;
  }
};
using ExistentialMetatypeMetadata
  = TargetExistentialMetatypeMetadata<InProcess>;

/// \brief The header in front of a generic metadata template.
///
/// This is optimized so that the code generation pattern
/// requires the minimal number of independent arguments.
/// For example, we want to be able to allocate a generic class
/// Dictionary<T,U> like so:
///   extern GenericMetadata Dictionary_metadata_header;
///   void *arguments[] = { typeid(T), typeid(U) };
///   void *metadata = swift_getGenericMetadata(&Dictionary_metadata_header,
///                                             &arguments);
///   void *object = swift_allocObject(metadata);
///
/// Note that the metadata header is *not* const data; it includes 8
/// pointers worth of implementation-private data.
///
/// Both the metadata header and the arguments buffer are guaranteed
/// to be pointer-aligned.
template <typename Runtime>
struct TargetGenericMetadata {
  /// The fill function. Receives a pointer to the instantiated metadata and
  /// the argument pointer passed to swift_getGenericMetadata.
  TargetMetadata<Runtime> *(*CreateFunction)
  (TargetGenericMetadata<Runtime> *pattern, const void *arguments);
  
  /// The size of the template in bytes.
  uint32_t TemplateSize;

  /// The number of generic arguments that we need to unique on,
  /// in words.  The first 'NumArguments * sizeof(void*)' bytes of
  /// the arguments buffer are the key. There may be additional private-contract
  /// data used by FillFunction not used for uniquing.
  uint16_t NumKeyArguments;

  /// The offset of the address point in the template in bytes.
  uint16_t AddressPoint;

  /// Data that the runtime can use for its own purposes.  It is guaranteed
  /// to be zero-filled by the compiler.
  TargetPointer<Runtime, void>
  PrivateData[swift::NumGenericMetadataPrivateDataWords];

  // Here there is a variably-sized field:
  // char alignas(void*) MetadataTemplate[TemplateSize];

  /// Return the starting address of the metadata template data.
  TargetPointer<Runtime, const void> getMetadataTemplate() const {
    return reinterpret_cast<TargetPointer<Runtime, const void>>(this + 1);
  }

  /// Return the nominal type descriptor for the template metadata
  ConstTargetMetadataPointer<Runtime, TargetTypeContextDescriptor>
  getTemplateDescription() const {
    auto bytes = reinterpret_cast<const uint8_t *>(getMetadataTemplate());
    auto metadata = reinterpret_cast<
      const TargetMetadata<Runtime> *>(bytes + AddressPoint);
    return metadata->getTypeContextDescriptor();
  }
};
using GenericMetadata = TargetGenericMetadata<InProcess>;

/// Heap metadata for a box, which may have been generated statically by the
/// compiler or by the runtime.
template <typename Runtime>
struct TargetBoxHeapMetadata : public TargetHeapMetadata<Runtime> {
  /// The offset from the beginning of a box to its value.
  unsigned Offset;

  constexpr TargetBoxHeapMetadata(MetadataKind kind, unsigned offset)
  : TargetHeapMetadata<Runtime>(kind), Offset(offset) {}
};
using BoxHeapMetadata = TargetBoxHeapMetadata<InProcess>;

/// Heap metadata for runtime-instantiated generic boxes.
template <typename Runtime>
struct TargetGenericBoxHeapMetadata : public TargetBoxHeapMetadata<Runtime> {
  using super = TargetBoxHeapMetadata<Runtime>;
  using super::Offset;

  /// The type inside the box.
  ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> BoxedType;

  constexpr
  TargetGenericBoxHeapMetadata(MetadataKind kind, unsigned offset,
    ConstTargetMetadataPointer<Runtime, swift::TargetMetadata> boxedType)
  : TargetBoxHeapMetadata<Runtime>(kind, offset), BoxedType(boxedType)
  {}

  static unsigned getHeaderOffset(const Metadata *boxedType) {
    // Round up the header size to alignment.
    unsigned alignMask = boxedType->getValueWitnesses()->getAlignmentMask();
    return (sizeof(HeapObject) + alignMask) & ~alignMask;
  }

  /// Project the value out of a box of this type.
  OpaqueValue *project(HeapObject *box) const {
    auto bytes = reinterpret_cast<char*>(box);
    return reinterpret_cast<OpaqueValue *>(bytes + Offset);
  }

  /// Get the allocation size of this box.
  unsigned getAllocSize() const {
    return Offset + BoxedType->getValueWitnesses()->getSize();
  }

  /// Get the allocation alignment of this box.
  unsigned getAllocAlignMask() const {
    // Heap allocations are at least pointer aligned.
    return BoxedType->getValueWitnesses()->getAlignmentMask()
      | (alignof(void*) - 1);
  }

  static bool classof(const TargetMetadata<Runtime> *metadata) {
    return metadata->getKind() == MetadataKind::HeapGenericLocalVariable;
  }
};
using GenericBoxHeapMetadata = TargetGenericBoxHeapMetadata<InProcess>;

/// \brief The control structure of a generic or resilient protocol
/// conformance.
///
/// Witness tables need to be instantiated at runtime in these cases:
/// - For a generic conforming type, associated type requirements might be
///   dependent on the conforming type.
/// - For a type conforming to a resilient protocol, the runtime size of
///   the witness table is not known because default requirements can be
///   added resiliently.
///
/// One per conformance.
template <typename Runtime>
struct TargetGenericWitnessTable {
  /// The size of the witness table in words.  This amount is copied from
  /// the witness table template into the instantiated witness table.
  uint16_t WitnessTableSizeInWords;

  /// The amount of private storage to allocate before the address point,
  /// in words. This memory is zeroed out in the instantiated witness table
  /// template.
  uint16_t WitnessTablePrivateSizeInWords;

  /// The protocol descriptor. Only used for resilient conformances.
  RelativeIndirectablePointer<ProtocolDescriptor,
                              /*nullable*/ true> Protocol;

  /// The pattern.
  RelativeDirectPointer<const TargetWitnessTable<Runtime>> Pattern;

  /// The instantiation function, which is called after the template is copied.
  RelativeDirectPointer<void(TargetWitnessTable<Runtime> *instantiatedTable,
                             const TargetMetadata<Runtime> *type,
                             void * const *instantiationArgs),
                        /*nullable*/ true> Instantiator;

  using PrivateDataType = void *[swift::NumGenericMetadataPrivateDataWords];

  /// Private data for the instantiator.  Out-of-line so that the rest
  /// of this structure can be constant.
  RelativeDirectPointer<PrivateDataType> PrivateData;
};
using GenericWitnessTable = TargetGenericWitnessTable<InProcess>;

/// The structure of a type metadata record.
///
/// This contains enough static information to recover type metadata from a
/// name.
template <typename Runtime>
struct TargetTypeMetadataRecord {
private:
  union {
    /// A direct reference to a nominal type descriptor.
    RelativeDirectPointerIntPair<TargetTypeContextDescriptor<Runtime>,
                                 TypeMetadataRecordKind>
      DirectNominalTypeDescriptor;

    /// An indirect reference to a nominal type descriptor.
    RelativeDirectPointerIntPair<TargetTypeContextDescriptor<Runtime> * const,
                                 TypeMetadataRecordKind>
      IndirectNominalTypeDescriptor;
  };

public:
  TypeMetadataRecordKind getTypeKind() const {
    return DirectNominalTypeDescriptor.getInt();
  }
  
  const TargetTypeContextDescriptor<Runtime> *
  getTypeContextDescriptor() const {
    switch (getTypeKind()) {
    case TypeMetadataRecordKind::DirectNominalTypeDescriptor:
      return DirectNominalTypeDescriptor.getPointer();

    case TypeMetadataRecordKind::Reserved:
    case TypeMetadataRecordKind::IndirectObjCClass:
      return nullptr;

    case TypeMetadataRecordKind::IndirectNominalTypeDescriptor:
      return *IndirectNominalTypeDescriptor.getPointer();
    }
    
    return nullptr;
  }
};

using TypeMetadataRecord = TargetTypeMetadataRecord<InProcess>;

template<typename Runtime> struct TargetContextDescriptor;

template<typename Runtime>
using RelativeContextPointer =
  RelativeIndirectablePointer<const TargetContextDescriptor<Runtime>,
                              /*nullable*/ true>;

/// The structure of a protocol reference record.
template <typename Runtime>
struct TargetProtocolRecord {
  /// The protocol referenced.
  ///
  /// The remaining low bit is reserved for future use.
  RelativeIndirectablePointerIntPair<TargetProtocolDescriptor<Runtime>,
                                     /*reserved=*/bool>
    Protocol;
};
using ProtocolRecord = TargetProtocolRecord<InProcess>;

template<typename Runtime> class TargetGenericRequirementDescriptor;

/// The structure of a protocol conformance.
///
/// This contains enough static information to recover the witness table for a
/// type's conformance to a protocol.
template <typename Runtime>
struct TargetProtocolConformanceDescriptor final
  : public swift::ABI::TrailingObjects<
             TargetProtocolConformanceDescriptor<Runtime>,
             RelativeContextPointer<Runtime>,
             TargetGenericRequirementDescriptor<Runtime>> {

  using TrailingObjects = swift::ABI::TrailingObjects<
                             TargetProtocolConformanceDescriptor<Runtime>,
                             RelativeContextPointer<Runtime>,
                             TargetGenericRequirementDescriptor<Runtime>>;
  friend TrailingObjects;

  template<typename T>
  using OverloadToken = typename TrailingObjects::template OverloadToken<T>;

public:
  using WitnessTableAccessorFn
    = const TargetWitnessTable<Runtime> *(const TargetMetadata<Runtime>*,
                                          const TargetWitnessTable<Runtime> **,
                                          size_t);

  using GenericRequirementDescriptor =
    TargetGenericRequirementDescriptor<Runtime>;

private:
  /// The protocol being conformed to.
  ///
  /// The remaining low bit is reserved for future use.
  RelativeIndirectablePointer<ProtocolDescriptor> Protocol;
  
  // Some description of the type that conforms to the protocol.
  union {
    /// A direct reference to a nominal type descriptor.
    RelativeDirectPointer<TargetTypeContextDescriptor<Runtime>>
      DirectNominalTypeDescriptor;

    /// An indirect reference to a nominal type descriptor.
    RelativeDirectPointer<TargetTypeContextDescriptor<Runtime> * const>
      IndirectNominalTypeDescriptor;

    /// An indirect reference to the metadata.
    RelativeDirectPointer<const TargetClassMetadata<Runtime> *>
      IndirectObjCClass;
  };

  // The conformance, or a generator function for the conformance.
  union {
    /// A direct reference to the witness table for the conformance.
    RelativeDirectPointer<const TargetWitnessTable<Runtime>> WitnessTable;
    
    /// A function that produces the witness table given an instance of the
    /// type.
    RelativeDirectPointer<WitnessTableAccessorFn> WitnessTableAccessor;
  };

  /// Various flags, including the kind of conformance.
  ConformanceFlags Flags;

public:
  const ProtocolDescriptor *getProtocol() const {
    return Protocol;
  }

  TypeMetadataRecordKind getTypeKind() const {
    return Flags.getTypeReferenceKind();
  }

  typename ConformanceFlags::ConformanceKind getConformanceKind() const {
    return Flags.getConformanceKind();
  }
  
  const TargetClassMetadata<Runtime> * const *getIndirectObjCClass() const {
    switch (getTypeKind()) {
    case TypeMetadataRecordKind::IndirectObjCClass:
      break;
        
    case TypeMetadataRecordKind::Reserved:
      return nullptr;

    case TypeMetadataRecordKind::DirectNominalTypeDescriptor:
    case TypeMetadataRecordKind::IndirectNominalTypeDescriptor:
      assert(false && "not indirect class object");
    }
    
    return IndirectObjCClass.get();
  }
  
  const TargetTypeContextDescriptor<Runtime> *
  getTypeContextDescriptor() const {
    switch (getTypeKind()) {
    case TypeMetadataRecordKind::DirectNominalTypeDescriptor:
      return DirectNominalTypeDescriptor;

    case TypeMetadataRecordKind::IndirectNominalTypeDescriptor:
      return *IndirectNominalTypeDescriptor;

    case TypeMetadataRecordKind::Reserved:
    case TypeMetadataRecordKind::IndirectObjCClass:
      return nullptr;
    }
    
    return nullptr;
  }

  /// Retrieve the context of a retroactive conformance.
  const TargetContextDescriptor<Runtime> *getRetroactiveContext() const {
    if (!Flags.isRetroactive()) return nullptr;

    return this->template getTrailingObjects<RelativeContextPointer<Runtime>>();
  }

  /// Retrieve the conditional requirements that must also be
  /// satisfied
  llvm::ArrayRef<GenericRequirementDescriptor>
  getConditionalRequirements() const {
    return {this->template getTrailingObjects<GenericRequirementDescriptor>(),
            Flags.getNumConditionalRequirements()};
  }

  /// Get the directly-referenced static witness table.
  const swift::TargetWitnessTable<Runtime> *getStaticWitnessTable() const {
    switch (getConformanceKind()) {
    case ConformanceFlags::ConformanceKind::WitnessTable:
      break;
        
    case ConformanceFlags::ConformanceKind::WitnessTableAccessor:
    case ConformanceFlags::ConformanceKind::ConditionalWitnessTableAccessor:
      assert(false && "not witness table");
    }
    return WitnessTable;
  }
  
  WitnessTableAccessorFn *getWitnessTableAccessor() const {
    switch (getConformanceKind()) {
    case ConformanceFlags::ConformanceKind::WitnessTableAccessor:
    case ConformanceFlags::ConformanceKind::ConditionalWitnessTableAccessor:
      break;
        
    case ConformanceFlags::ConformanceKind::WitnessTable:
      assert(false && "not witness table accessor");
    }
    return WitnessTableAccessor;
  }
  
  /// Get the canonical metadata for the type referenced by this record, or
  /// return null if the record references a generic or universal type.
  const TargetMetadata<Runtime> *getCanonicalTypeMetadata() const;
  
  /// Get the witness table for the specified type, realizing it if
  /// necessary, or return null if the conformance does not apply to the
  /// type.
  const swift::TargetWitnessTable<Runtime> *
  getWitnessTable(const TargetMetadata<Runtime> *type) const;

#if !defined(NDEBUG) && SWIFT_OBJC_INTEROP
  void dump() const;
#endif

#ifndef NDEBUG
  /// Verify that the protocol descriptor obeys all invariants.
  ///
  /// We currently check that the descriptor:
  ///
  /// 1. Has a valid TypeMetadataRecordKind.
  /// 2. Has a valid conformance kind.
  void verify() const LLVM_ATTRIBUTE_USED;
#endif

private:
  size_t numTrailingObjects(
                        OverloadToken<RelativeContextPointer<Runtime>>) const {
    return Flags.isRetroactive() ? 1 : 0;
  }

  size_t numTrailingObjects(OverloadToken<GenericRequirementDescriptor>) const {
    return Flags.getNumConditionalRequirements();
  }
};
using ProtocolConformanceDescriptor
  = TargetProtocolConformanceDescriptor<InProcess>;

template<typename Runtime>
using TargetProtocolConformanceRecord =
  RelativeDirectPointer<TargetProtocolConformanceDescriptor<Runtime>,
                        /*Nullable=*/false>;

using ProtocolConformanceRecord = TargetProtocolConformanceRecord<InProcess>;

template<typename Runtime>
struct TargetGenericContext;

/// Base class for all context descriptors.
template<typename Runtime>
struct TargetContextDescriptor {
  /// Flags describing the context, including its kind and format version.
  ContextDescriptorFlags Flags;
  
  /// The parent context, or null if this is a top-level context.
  RelativeContextPointer<Runtime> Parent;

  bool isGeneric() const { return Flags.isGeneric(); }
  bool isUnique() const { return Flags.isUnique(); }
  ContextDescriptorKind getKind() const { return Flags.getKind(); }

  /// Get the generic context information for this context, or null if the
  /// context is not generic.
  const TargetGenericContext<Runtime> *getGenericContext() const;

private:
  TargetContextDescriptor(const TargetContextDescriptor &) = delete;
  TargetContextDescriptor(TargetContextDescriptor &&) = delete;
  TargetContextDescriptor &operator=(const TargetContextDescriptor &) = delete;
  TargetContextDescriptor &operator=(TargetContextDescriptor &&) = delete;
};

using ContextDescriptor = TargetContextDescriptor<InProcess>;

/// True if two context descriptors in the currently running program describe
/// the same context.
bool equalContexts(const ContextDescriptor *a, const ContextDescriptor *b);

/// Descriptor for a module context.
template<typename Runtime>
struct TargetModuleContextDescriptor final : TargetContextDescriptor<Runtime> {
  /// The module name.
  RelativeDirectPointer<const char, /*nullable*/ false> Name;

  static bool classof(const TargetContextDescriptor<Runtime> *cd) {
    return cd->getKind() == ContextDescriptorKind::Module;
  }
};

using ModuleContextDescriptor = TargetModuleContextDescriptor<InProcess>;

struct GenericContextDescriptorHeader {
  unsigned NumParams, NumRequirements, NumKeyArguments, NumExtraArguments;
  
  unsigned getNumArguments() const {
    return NumKeyArguments + NumExtraArguments;
  }
  
  bool hasArguments() const {
    return getNumArguments() > 0;
  }
};

/// A reference to a generic parameter that is the subject of a requirement.
/// This can refer either directly to a generic parameter or to a path to an
/// associated type.
template<typename Runtime>
class TargetGenericParamRef {
  union {
    /// The word of storage, whose low bit indicates whether there is an
    /// associated type path stored out-of-line and whose upper bits describe
    /// the generic parameter at root of the path.
    uint32_t Word;

    /// This is the associated type path stored out-of-line. The \c bool
    /// is used for masking purposes and is otherwise unused; instead, check
    /// the low bit of \c Word.
    RelativeDirectPointerIntPair<const void, bool> AssociatedTypePath;
  };

public:
  /// Index of the parameter being referenced. 0 is the first generic parameter
  /// of the root of the context hierarchy, and subsequent parameters are
  /// numbered breadth-first from there.
  unsigned getRootParamIndex() const {
    // If there is no path, retrieve the index directly.
    if ((Word & 0x01) == 0) return Word >> 1;

    // Otherwise, the index is at the start of the associated type path.
    return *reinterpret_cast<const unsigned *>(AssociatedTypePath.getPointer());
  }
  
  /// A reference to an associated type along the reference path.
  struct AssociatedTypeRef {
    /// The protocol the associated type belongs to.
    RelativeIndirectablePointer<TargetProtocolDescriptor<Runtime>,
                                /*nullable*/ false> Protocol;
    /// The index of the associated type metadata within a witness table for
    /// the protocol.
    unsigned Index;
  };
  
  /// A forward iterator that walks through the associated type path, which is
  /// a zero-terminated array of AssociatedTypeRefs.
  class AssociatedTypeIterator {
    const void *addr;
    
    explicit AssociatedTypeIterator(const void *startAddr) : addr(startAddr) {}
    
    bool isEnd() const {
      if (addr == nullptr)
        return true;
      unsigned word;
      memcpy(&word, addr, sizeof(unsigned));
      if (word == 0)
        return true;
      return false;
    }

    template <class> friend class TargetGenericParamRef;

  public:
    AssociatedTypeIterator() : addr(nullptr) {}

    using iterator_category = std::forward_iterator_tag;
    using value_type = AssociatedTypeRef;
    using difference_type = std::ptrdiff_t;
    using pointer = const AssociatedTypeRef *;
    using reference = const AssociatedTypeRef &;
    
    bool operator==(AssociatedTypeIterator i) const {
      // Iterators are same if they both point at the same place, or are both
      // at the end (either by being initialized as an end iterator with a
      // null address, or by being advanced to the null terminator of an
      // associated type list).
      if (addr == i.addr)
        return true;
      
      if (isEnd() && i.isEnd())
        return true;
      
      return false;
    }
    
    bool operator!=(AssociatedTypeIterator i) const {
      return !(*this == i);
    }
    
    reference operator*() const {
      return *reinterpret_cast<pointer>(addr);
    }
    pointer operator->() const {
      return reinterpret_cast<pointer>(addr);
    }
    
    AssociatedTypeIterator &operator++() {
      addr = reinterpret_cast<const char*>(addr) + sizeof(AssociatedTypeRef);
      return *this;
    }
    
    AssociatedTypeIterator operator++(int) {
      auto copy = *this;
      ++*this;
      return copy;
    }
  };
  
  /// Iterators for going through the associated type path from the root param.
  AssociatedTypeIterator begin() const {
    if (Word & 0x01) {
      // The associated types start after the first word, which holds the
      // root param index.
      return AssociatedTypeIterator(
        reinterpret_cast<const char*>(AssociatedTypePath.getPointer()) +
                                        sizeof(unsigned));
    } else {
      // This is a direct param reference, so there are no associated types.
      return end();
    }
  }
  
  AssociatedTypeIterator end() const {
    return AssociatedTypeIterator{};
  }
};

using GenericParamRef = TargetGenericParamRef<InProcess>;

template<typename Runtime>
class TargetGenericRequirementDescriptor {
  GenericRequirementFlags Flags;
  /// The generic parameter or associated type that's constrained.
  TargetGenericParamRef<Runtime> Param;
  
  union {
    /// A mangled representation of the same-type or base class the param is
    /// constrained to.
    ///
    /// Only valid if the requirement has SameType or BaseClass kind.
    RelativeDirectPointer<const char, /*nullable*/ false> Type;
    
    /// The protocol the param is constrained to.
    ///
    /// Only valid if the requirement has Protocol kind.
    RelativeIndirectablePointer<TargetProtocolDescriptor<Runtime>,
                                /*nullable*/ false> Protocol;
    
    /// The conformance the param is constrained to use.
    ///
    /// Only valid if the requirement has SameConformance kind.
    RelativeIndirectablePointer<TargetProtocolConformanceRecord<Runtime>,
                                /*nullable*/ false> Conformance;
    
    /// The kind of layout constraint.
    ///
    /// Only valid if the requirement has Layout kind.
    GenericRequirementLayoutKind Layout;
  };

public:
  constexpr GenericRequirementFlags getFlags() const {
    return Flags;
  }

  constexpr GenericRequirementKind getKind() const {
    return getFlags().getKind();
  }

  /// Retrieve the generic parameter that is the subject of this requirement.
  const TargetGenericParamRef<Runtime> &getParam() const {
    return Param;
  }

  /// Retrieve the protocol descriptor for a Protocol requirement.
  const TargetProtocolDescriptor<Runtime> *getProtocol() const {
    assert(getKind() == GenericRequirementKind::Protocol);
    return Protocol;
  }

  /// Retrieve the right-hand type for a SameType or BaseClass requirement.
  const char *getMangledTypeName() const {
    assert(getKind() == GenericRequirementKind::SameType ||
           getKind() == GenericRequirementKind::BaseClass);
    return Type;
  }

  /// Retrieve the protocol conformance record for a SameConformance
  /// requirement.
  const TargetProtocolConformanceRecord<Runtime> *getConformance() const {
    assert(getKind() == GenericRequirementKind::SameConformance);
    return Conformance;
  }

  /// Retrieve the layout constraint.
  GenericRequirementLayoutKind getLayout() const {
    assert(getKind() == GenericRequirementKind::Layout);
    return Layout;
  }

  /// Determine whether this generic requirement has a known kind.
  ///
  /// \returns \c false for any future generic requirement kinds.
  bool hasKnownKind() const {
    switch (getKind()) {
    case GenericRequirementKind::BaseClass:
    case GenericRequirementKind::Layout:
    case GenericRequirementKind::Protocol:
    case GenericRequirementKind::SameConformance:
    case GenericRequirementKind::SameType:
      return true;
    }

    return false;
  }
};
using GenericRequirementDescriptor =
  TargetGenericRequirementDescriptor<InProcess>;

/// CRTP class for a context descriptor that includes trailing generic
/// context description.
template<typename Self,
         typename HeaderType = GenericContextDescriptorHeader,
         typename...FollowingTrailingObjects>
class TrailingGenericContextObjects;

template<template<typename> class TargetSelf,
         typename Runtime,
         typename HeaderType,
         typename...FollowingTrailingObjects>
class TrailingGenericContextObjects<
  TargetSelf<Runtime>,
  HeaderType,
  FollowingTrailingObjects...
> : protected swift::ABI::TrailingObjects<TargetSelf<Runtime>,
      HeaderType,
      GenericParamDescriptor,
      TargetGenericRequirementDescriptor<Runtime>,
      FollowingTrailingObjects...>
{
protected:
  using Self = TargetSelf<Runtime>;
  using GenericRequirementDescriptor =
    TargetGenericRequirementDescriptor<Runtime>;

  using TrailingObjects = swift::ABI::TrailingObjects<Self,
    HeaderType,
    GenericParamDescriptor,
    GenericRequirementDescriptor,
    FollowingTrailingObjects...>;
  friend TrailingObjects;

  template<typename T>
  using OverloadToken = typename TrailingObjects::template OverloadToken<T>;
  
  const Self *asSelf() const {
    return static_cast<const Self *>(this);
  }
public:
  const HeaderType &getFullGenericContextHeader() const {
    assert(asSelf()->isGeneric());
    return *this->template getTrailingObjects<HeaderType>();
  }

  const GenericContextDescriptorHeader &getGenericContextHeader() const {
    /// HeaderType ought to be convertible to GenericContextDescriptorHeader.
    return getFullGenericContextHeader();
  }
  
  const TargetGenericContext<Runtime> *getGenericContext() const {
    if (!asSelf()->isGeneric())
      return nullptr;
    // The generic context header should always be immediately followed in
    // memory by trailing parameter and requirement descriptors.
    auto *header = reinterpret_cast<const char *>(&getGenericContextHeader());
    return reinterpret_cast<const TargetGenericContext<Runtime> *>(
      header - sizeof(TargetGenericContext<Runtime>));
  }

  llvm::ArrayRef<GenericParamDescriptor> getGenericParams() const {
    if (!asSelf()->isGeneric())
      return {};

    return {this->template getTrailingObjects<GenericParamDescriptor>(),
            getGenericContextHeader().NumParams};
  }
  
  llvm::ArrayRef<GenericRequirementDescriptor> getGenericRequirements() const {
    if (!asSelf()->isGeneric())
      return {};
    return {this->template getTrailingObjects<GenericRequirementDescriptor>(),
            getGenericContextHeader().NumRequirements};
  }

protected:
  size_t numTrailingObjects(OverloadToken<HeaderType>) const {
    return asSelf()->isGeneric() ? 1 : 0;
  }
  
  size_t numTrailingObjects(OverloadToken<GenericParamDescriptor>) const {
    return asSelf()->isGeneric() ? getGenericContextHeader().NumParams : 0;
  }

  size_t numTrailingObjects(OverloadToken<GenericRequirementDescriptor>) const {
    return asSelf()->isGeneric() ? getGenericContextHeader().NumRequirements : 0;
  }
};

/// Reference to a generic context.
template<typename Runtime>
struct TargetGenericContext final
  : TrailingGenericContextObjects<TargetGenericContext<Runtime>>
{
  // This struct is supposed to be empty, but TrailingObjects respects the
  // unique-address-per-object C++ rule, so even if this type is empty, the
  // trailing objects will come after one byte of padding. This dummy field
  // takes up space to make the offset of the trailing objects portable.
  unsigned _dummy;

  bool isGeneric() const { return true; }
};

/// Descriptor for an extension context.
template<typename Runtime>
struct TargetExtensionContextDescriptor final
    : TargetContextDescriptor<Runtime>,
      TrailingGenericContextObjects<TargetExtensionContextDescriptor<Runtime>>
{
private:
  using TrailingGenericContextObjects
    = TrailingGenericContextObjects<TargetExtensionContextDescriptor>;

public:
  using TrailingGenericContextObjects::getGenericContext;

  /// A mangling of the `Self` type context that the extension extends.
  /// The mangled name represents the type in the generic context encoded by
  /// this descriptor. For example, a nongeneric nominal type extension will
  /// encode the nominal type name. A generic nominal type extension will encode
  /// the instance of the type with any generic arguments bound.
  ///
  /// Note that the Parent of the extension will be the module context the
  /// extension is declared inside.
  RelativeDirectPointer<const char> ExtendedContext;
  
  static bool classof(const TargetContextDescriptor<Runtime> *cd) {
    return cd->getKind() == ContextDescriptorKind::Extension;
  }
};

using ExtensionContextDescriptor = TargetExtensionContextDescriptor<InProcess>;

template<typename Runtime>
struct TargetAnonymousContextDescriptor final
    : TargetContextDescriptor<Runtime>,
      TrailingGenericContextObjects<TargetAnonymousContextDescriptor<Runtime>>
{
private:
  using TrailingGenericContextObjects
    = TrailingGenericContextObjects<TargetAnonymousContextDescriptor<Runtime>>;

public:
  using TrailingGenericContextObjects::getGenericContext;

  static bool classof(const TargetContextDescriptor<Runtime> *cd) {
    return cd->getKind() == ContextDescriptorKind::Anonymous;
  }
};

struct TypeGenericContextDescriptorHeader {
  /// Indicates the offset of the instantiation arguments for a type's generic
  /// contexts in instances of its type metadata. For a value type or class
  /// without resilient superclasses, this the the offset from the address
  /// point of the metadata. For a class with a resilient superclass, this
  /// offset is relative to the end of the superclass metadata.
  unsigned ArgumentOffset;
  GenericContextDescriptorHeader Base;
  
  operator const GenericContextDescriptorHeader &() const {
    return Base;
  }
};
  
/// Wrapper class for the pointer to a metadata access function that provides
/// operator() overloads to call it with the right calling convention.
class MetadataAccessFunction {
  const Metadata * (*Function)(...);

  static_assert(NumDirectGenericTypeMetadataAccessFunctionArgs == 3,
                "Need to account for change in number of direct arguments");

  template<typename T>
  const Metadata *applyN(const void *arg0,
                         const void *arg1,
                         const void *arg2,
                         llvm::ArrayRef<T *> argRest) const {
    using FnN = const Metadata *(const void *,
                                 const void *,
                                 const void *,
                                 const void *);
    return reinterpret_cast<FnN*>(Function)(arg0, arg1, arg2, argRest.data());
  }
  
  template<typename...Args>
  const Metadata *variadic_apply(const void *arg0,
                                 const void *arg1,
                                 const void *arg2,
                                 llvm::MutableArrayRef<const void *> argRest,
                                 unsigned n,
                                 const void *arg3,
                                 Args...argN) const {
    argRest[n] = arg3;
    return variadic_apply(arg0, arg1, arg2, argRest, n+1, argN...);
  }
  
  const Metadata *variadic_apply(const void *arg0,
                                 const void *arg1,
                                 const void *arg2,
                                 llvm::MutableArrayRef<const void *> argRest,
                                 unsigned n) const {
    return applyN(arg0, arg1, arg2, argRest);
  }

public:
  explicit MetadataAccessFunction(const Metadata * (*Function)(...))
    : Function(Function)
  {}
  
  explicit operator bool() const {
    return Function != nullptr;
  }
  
  // Invoke with an array of arguments.
  template<typename T>
  const Metadata *operator()(llvm::ArrayRef<T *> args) const {
    switch (args.size()) {
    case 0:
      return (*this)();
    case 1:
      return (*this)(args[0]);
    case 2:
      return (*this)(args[0], args[1]);
    case 3:
      return (*this)(args[0], args[1], args[2]);
    default:
      return applyN(args[0], args[1], args[2], args);
    }
  }
  
  // Invoke with n arguments.
  const Metadata *operator()() const {
    using Fn0 = const Metadata *();
    return reinterpret_cast<Fn0*>(Function)();
  }
  const Metadata *operator()(const void *arg0) const {
    using Fn1 = const Metadata *(const void *);
    return reinterpret_cast<Fn1*>(Function)(arg0);

  }
  const Metadata *operator()(const void *arg0,
                             const void *arg1) const {
    using Fn2 = const Metadata *(const void *, const void *);
    return reinterpret_cast<Fn2*>(Function)(arg0, arg1);
  }
  const Metadata *operator()(const void *arg0,
                             const void *arg1,
                             const void *arg2) const {
    using Fn3 = const Metadata *(const void *, const void *, const void *);
    return reinterpret_cast<Fn3*>(Function)(arg0, arg1, arg2);
  }
  
  template<typename...Args>
  const Metadata *operator()(const void *arg0,
                             const void *arg1,
                             const void *arg2,
                             Args...argN) const {
    const void *args[3 + sizeof...(Args)];
    return variadic_apply(arg0, arg1, arg2, args, 3, argN...);
  }
};

template<typename Runtime>
struct TargetTypeContextDescriptor final
    : TargetContextDescriptor<Runtime>,
      TrailingGenericContextObjects<TargetTypeContextDescriptor<Runtime>,
                                    TypeGenericContextDescriptorHeader,
                                    /*additional trailing objects:*/
                                    TargetVTableDescriptorHeader<Runtime>,
                                    TargetMethodDescriptor<Runtime>>
{
private:
  using TrailingGenericContextObjects =
    TrailingGenericContextObjects<TargetTypeContextDescriptor<Runtime>,
                                  TypeGenericContextDescriptorHeader,
                                  TargetVTableDescriptorHeader<Runtime>,
                                  TargetMethodDescriptor<Runtime>>;
  
  using TrailingObjects =
    typename TrailingGenericContextObjects::TrailingObjects;
  
public:
  using MethodDescriptor = TargetMethodDescriptor<Runtime>;
  using VTableDescriptorHeader = TargetVTableDescriptorHeader<Runtime>;

  using TrailingGenericContextObjects::getGenericContext;
  using TrailingGenericContextObjects::getFullGenericContextHeader;

  /// This bit is set in the context descriptor header's kind-specific flags
  /// if this is a class descriptor with a vtable descriptor for runtime
  /// vtable instantiation.
  static constexpr const uint16_t HasVTableFlag =
    uint16_t(TypeContextDescriptorFlags::HasVTable);
  /// This bit is set in the context descriptor header's kind-specific flags
  /// if this is a class descriptor with a resilient superclass.
  static constexpr const uint16_t HasResilientSuperclassFlag =
    uint16_t(TypeContextDescriptorFlags::HasResilientSuperclass);
  
  /// The name of the type.
  TargetRelativeDirectPointer<Runtime, const char, /*nullable*/ false> Name;
  
  /// A pointer to the metadata access function for this type.
  ///
  /// The function type here is a stand-in. You should use getAccessFunction()
  /// to wrap the function pointer in an accessor that uses the proper calling
  /// convention for a given number of arguments.
  TargetRelativeDirectPointer<Runtime, const Metadata *(...),
                              /*Nullable*/ true> AccessFunctionPtr;
  
  MetadataAccessFunction getAccessFunction() const {
    return MetadataAccessFunction(AccessFunctionPtr.get());
  }

  // ABI TODO: These fields ought to be superseded by remote mirror metadata for
  // the type.
  union {
    /// Information about class types.
    struct {
      /// The number of stored properties in the class, not including its
      /// superclasses. If there is a field offset vector, this is its length.
      uint32_t NumFields;

    private:
      /// The offset of the field offset vector for this class's stored
      /// properties in its metadata, in words. 0 means there is no field offset
      /// vector.
      ///
      /// If this class has a resilient superclass, this offset is relative to
      /// the size of the resilient superclass metadata. Otherwise, it is
      /// absolute.
      uint32_t FieldOffsetVectorOffset;

    public:
      /// The field names. A doubly-null-terminated list of strings, whose
      /// length and order is consistent with that of the field offset vector.
      RelativeDirectPointer<const char, /*nullable*/ true> FieldNames;

      /// The field type vector accessor. Returns a pointer to an array of
      /// type metadata references whose order is consistent with that of the
      /// field offset vector.
      RelativeDirectPointer<const FieldType *
        (const TargetMetadata<Runtime> *)> GetFieldTypes;

      /// True if metadata records for this type have a field offset vector for
      /// its stored properties.
      bool hasFieldOffsetVector() const { return FieldOffsetVectorOffset != 0; }

      unsigned getFieldOffsetVectorOffset(const ClassMetadata *metadata) const {
        const auto *description = metadata->getDescription();

        if (description->hasResilientSuperclass())
          return metadata->SuperClass->getSizeInWords() + FieldOffsetVectorOffset;

        return FieldOffsetVectorOffset;
      }
    } Class;
    
    /// Information about struct types.
    struct {
      /// The number of stored properties in the class, not including its
      /// superclasses. If there is a field offset vector, this is its length.
      uint32_t NumFields;
      /// The offset of the field offset vector for this class's stored
      /// properties in its metadata, if any. 0 means there is no field offset
      /// vector.
      uint32_t FieldOffsetVectorOffset;
      
      /// The field names. A doubly-null-terminated list of strings, whose
      /// length and order is consistent with that of the field offset vector.
      RelativeDirectPointer<const char, /*nullable*/ true> FieldNames;
      
      /// The field type vector accessor. Returns a pointer to an array of
      /// type metadata references whose order is consistent with that of the
      /// field offset vector.
      RelativeDirectPointer<const FieldType *
        (const TargetMetadata<Runtime> *)> GetFieldTypes;

      /// True if metadata records for this type have a field offset vector for
      /// its stored properties.
      bool hasFieldOffsetVector() const { return FieldOffsetVectorOffset != 0; }
    } Struct;
    
    /// Information about enum types.
    struct {
      /// The number of non-empty cases in the enum are in the low 24 bits;
      /// the offset of the payload size in the metadata record in words,
      /// if any, is stored in the high 8 bits.
      uint32_t NumPayloadCasesAndPayloadSizeOffset;
      /// The number of empty cases in the enum.
      uint32_t NumEmptyCases;
      /// The names of the cases. A doubly-null-terminated list of strings,
      /// whose length is NumNonEmptyCases + NumEmptyCases. Cases are named in
      /// tag order, non-empty cases first, followed by empty cases.
      RelativeDirectPointer<const char, /*nullable*/ true> CaseNames;
      /// The field type vector accessor. Returns a pointer to an array of
      /// type metadata references whose order is consistent with that of the
      /// CaseNames. Only types for payload cases are provided.
      RelativeDirectPointer<
        const FieldType * (const TargetMetadata<Runtime> *)>
        GetCaseTypes;

      uint32_t getNumPayloadCases() const {
        return NumPayloadCasesAndPayloadSizeOffset & 0x00FFFFFFU;
      }
      uint32_t getNumEmptyCases() const {
        return NumEmptyCases;
      }
      uint32_t getNumCases() const {
        return getNumPayloadCases() + NumEmptyCases;
      }
      size_t getPayloadSizeOffset() const {
        return ((NumPayloadCasesAndPayloadSizeOffset & 0xFF000000U) >> 24);
      }
      
      bool hasPayloadSizeOffset() const {
        return getPayloadSizeOffset() != 0;
      }
    } Enum;
  };

  bool hasVTable() const {
    return (this->Flags.getKindSpecificFlags() & HasVTableFlag) != 0;
  }

  bool hasResilientSuperclass() const {
    return (this->Flags.getKindSpecificFlags() & HasResilientSuperclassFlag)
      != 0;
  }
  
  const VTableDescriptorHeader *getVTableDescriptor() const {
    if (!hasVTable())
      return nullptr;
    return this->template getTrailingObjects<VTableDescriptorHeader>();
  }
  
  llvm::ArrayRef<MethodDescriptor> getMethodDescriptors() const {
    if (!hasVTable())
      return {};
    return {this->template getTrailingObjects<MethodDescriptor>(),
            numTrailingObjects(OverloadToken<MethodDescriptor>{})};
  }

  void *getMethod(unsigned i) const {
    assert(hasVTable()
           && i < numTrailingObjects(OverloadToken<MethodDescriptor>{}));
    return getMethodDescriptors()[i].Impl.get();
  }

  static bool classof(const TargetContextDescriptor<Runtime> *cd) {
    return cd->getKind() >= ContextDescriptorKind::Type_First
        && cd->getKind() <= ContextDescriptorKind::Type_Last;
  }
  
  /// This is factored in a silly way because remote mirrors cannot directly
  /// dereference the SuperClass field of class metadata.
  uint32_t getGenericArgumentOffset(
                      const TargetClassMetadata<Runtime> *classMetadata,
                      const TargetClassMetadata<Runtime> *superMetadata) const {
    auto Offset = getFullGenericContextHeader().ArgumentOffset;
    if (hasResilientSuperclass())
      return superMetadata->getSizeInWords() + Offset;

    return Offset;
  }

  /// Return the offset of the start of generic arguments in the nominal
  /// type's metadata. This method should only be used with value type
  /// metadata and class metadata with a non-resilient superclass.
  uint32_t getGenericArgumentOffset() const {
    assert(!hasResilientSuperclass());
    return getFullGenericContextHeader().ArgumentOffset;
  }

  /// Return the offset of the start of generic arguments in the nominal
  /// type's metadata. The returned value is measured in sizeof(void*).
  uint32_t
  getGenericArgumentOffset(const TargetMetadata<Runtime> *metadata) const {
    if (hasResilientSuperclass()) {
      auto *classMetadata = llvm::cast<ClassMetadata>(metadata);
      auto *superMetadata = llvm::cast<ClassMetadata>(classMetadata->SuperClass);
      return getGenericArgumentOffset(classMetadata, superMetadata);
    }

    return getGenericArgumentOffset();
  }
  
  const TargetMetadata<Runtime> * const *getGenericArguments(
                               const TargetMetadata<Runtime> *metadata) const {
    auto offset = getGenericArgumentOffset(metadata);
    auto words =
      reinterpret_cast<const TargetMetadata<Runtime> * const *>(metadata);
    return words + offset;
  }
  
private:
  template<typename T>
  using OverloadToken =
    typename TrailingGenericContextObjects::template OverloadToken<T>;
  
  friend TrailingObjects;
  
  using TrailingGenericContextObjects::numTrailingObjects;
  
  size_t numTrailingObjects(OverloadToken<VTableDescriptorHeader>) const {
    return hasVTable() ? 1 : 0;
  }
  
  size_t numTrailingObjects(OverloadToken<MethodDescriptor>) const {
    if (!hasVTable())
      return 0;

    return getVTableDescriptor()->VTableSize;
  }
};

using TypeContextDescriptor = TargetTypeContextDescriptor<InProcess>;

template<typename Runtime>
inline const TargetGenericContext<Runtime> *
TargetContextDescriptor<Runtime>::getGenericContext() const {
  if (!isGeneric())
    return nullptr;

  switch (getKind()) {
  case ContextDescriptorKind::Module:
    // Never generic.
    return nullptr;
  case ContextDescriptorKind::Extension:
    return llvm::cast<TargetExtensionContextDescriptor<Runtime>>(this)
      ->getGenericContext();
  case ContextDescriptorKind::Anonymous:
    return llvm::cast<TargetAnonymousContextDescriptor<Runtime>>(this)
      ->getGenericContext();
  default:
    if (getKind() >= ContextDescriptorKind::Type_First
        && getKind() <= ContextDescriptorKind::Type_Last) {
      return llvm::cast<TargetTypeContextDescriptor<Runtime>>(this)
        ->getGenericContext();
    }
    
    // We don't know about this kind of descriptor.
    return nullptr;
  }
}
  
/// \brief Fetch a uniqued metadata object for a generic nominal type.
///
/// The basic algorithm for fetching a metadata object is:
///   func swift_getGenericMetadata(header, arguments) {
///     if (metadata = getExistingMetadata(&header.PrivateData,
///                                        arguments[0..header.NumArguments]))
///       return metadata
///     metadata = malloc(superclass.MetadataSize +
///                       numImmediateMembers * sizeof(void *))
///     memcpy(metadata, header.MetadataTemplate, header.TemplateSize)
///     for (i in 0..header.NumFillInstructions)
///       metadata[header.FillInstructions[i].ToIndex]
///         = arguments[header.FillInstructions[i].FromIndex]
///     setExistingMetadata(&header.PrivateData,
///                         arguments[0..header.NumArguments],
///                         metadata)
///     return metadata
///   }
SWIFT_RUNTIME_EXPORT
const Metadata *
swift_getGenericMetadata(GenericMetadata *pattern,
                         const void *arguments);

// Callback to allocate a generic class metadata object.
SWIFT_RUNTIME_EXPORT
ClassMetadata *
swift_allocateGenericClassMetadata(GenericMetadata *pattern,
                                   const void *arguments,
                                   ClassMetadata *superclass,
                                   size_t numImmediateMembers);

// Callback to allocate a generic struct/enum metadata object.
SWIFT_RUNTIME_EXPORT
ValueMetadata *
swift_allocateGenericValueMetadata(GenericMetadata *pattern,
                                   const void *arguments);

/// Instantiate a resilient or generic protocol witness table.
///
/// \param genericTable - The witness table template for the
///   conformance. It may either have fields that require runtime
///   initialization, or be missing requirements at the end for
///   which default witnesses are available.
///
/// \param type - The conforming type, used to form a uniquing key
///   for the conformance. If the witness table is not dependent on
///   the substituted type of the conformance, this can be set to
///   nullptr, in which case there will only be one instantiated
///   witness table per witness table template.
///
/// \param instantiationArgs - An opaque pointer that's forwarded to
///   the instantiation function, used for conditional conformances.
///   This API implicitly embeds an assumption that these arguments
///   never form part of the uniquing key for the conformance, which
///   is ultimately a statement about the user model of overlapping
///   conformances.
SWIFT_RUNTIME_EXPORT
const WitnessTable *
swift_getGenericWitnessTable(GenericWitnessTable *genericTable,
                             const Metadata *type,
                             void * const *instantiationArgs);

/// \brief Fetch a uniqued metadata for a function type.
SWIFT_RUNTIME_EXPORT
const FunctionTypeMetadata *
swift_getFunctionTypeMetadata(FunctionTypeFlags flags,
                              const Metadata *const *parameters,
                              const uint32_t *parameterFlags,
                              const Metadata *result);

SWIFT_RUNTIME_EXPORT
const FunctionTypeMetadata *
swift_getFunctionTypeMetadata0(FunctionTypeFlags flags,
                               const Metadata *result);

SWIFT_RUNTIME_EXPORT
const FunctionTypeMetadata *
swift_getFunctionTypeMetadata1(FunctionTypeFlags flags,
                               const Metadata *arg0,
                               const Metadata *result);

SWIFT_RUNTIME_EXPORT
const FunctionTypeMetadata *
swift_getFunctionTypeMetadata2(FunctionTypeFlags flags,
                               const Metadata *arg0,
                               const Metadata *arg1,
                               const Metadata *result);

SWIFT_RUNTIME_EXPORT
const FunctionTypeMetadata *swift_getFunctionTypeMetadata3(
                                                FunctionTypeFlags flags,
                                                const Metadata *arg0,
                                                const Metadata *arg1,
                                                const Metadata *arg2,
                                                const Metadata *result);

#if SWIFT_OBJC_INTEROP
SWIFT_RUNTIME_EXPORT
void
swift_instantiateObjCClass(const ClassMetadata *theClass);

SWIFT_RUNTIME_EXPORT
Class
swift_getInitializedObjCClass(Class c);

/// \brief Fetch a uniqued type metadata for an ObjC class.
SWIFT_RUNTIME_EXPORT
const Metadata *
swift_getObjCClassMetadata(const ClassMetadata *theClass);

/// \brief Get the ObjC class object from class type metadata.
SWIFT_RUNTIME_EXPORT
const ClassMetadata *
swift_getObjCClassFromMetadata(const Metadata *theClass);
#endif

/// \brief Fetch a unique type metadata object for a foreign type.
SWIFT_RUNTIME_EXPORT
const ForeignTypeMetadata *
swift_getForeignTypeMetadata(ForeignTypeMetadata *nonUnique);

/// \brief Fetch a uniqued metadata for a tuple type.
///
/// The labels argument is null if and only if there are no element
/// labels in the tuple.  Otherwise, it is a null-terminated
/// concatenation of space-terminated NFC-normalized UTF-8 strings,
/// assumed to point to constant global memory.
///
/// That is, for the tuple type (a : Int, Int, c : Int), this
/// argument should be:
///   "a  c \0"
///
/// This representation allows label strings to be efficiently
/// (1) uniqued within a linkage unit and (2) compared with strcmp.
/// In other words, it's optimized for code size and uniquing
/// efficiency, not for the convenience of actually consuming
/// these strings.
///
/// \param elements - potentially invalid if numElements is zero;
///   otherwise, an array of metadata pointers.
/// \param labels - the labels string
/// \param proposedWitnesses - an optional proposed set of value witnesses.
///   This is useful when working with a non-dependent tuple type
///   where the entrypoint is just being used to unique the metadata.
SWIFT_RUNTIME_EXPORT
const TupleTypeMetadata *
swift_getTupleTypeMetadata(TupleTypeFlags flags,
                           const Metadata * const *elements,
                           const char *labels,
                           const ValueWitnessTable *proposedWitnesses);

SWIFT_RUNTIME_EXPORT
const TupleTypeMetadata *
swift_getTupleTypeMetadata2(const Metadata *elt0, const Metadata *elt1,
                            const char *labels,
                            const ValueWitnessTable *proposedWitnesses);
SWIFT_RUNTIME_EXPORT
const TupleTypeMetadata *
swift_getTupleTypeMetadata3(const Metadata *elt0, const Metadata *elt1,
                            const Metadata *elt2, const char *labels,
                            const ValueWitnessTable *proposedWitnesses);

/// Initialize the value witness table and struct field offset vector for a
/// struct, using the "Universal" layout strategy.
SWIFT_RUNTIME_EXPORT
void swift_initStructMetadata(StructMetadata *self,
                              StructLayoutFlags flags,
                              size_t numFields,
                              const TypeLayout * const *fieldTypes,
                              size_t *fieldOffsets);

/// Relocate the metadata for a class and copy fields from the given template.
/// The final size of the metadata is calculated at runtime from the size of
/// the superclass metadata together with the given number of immediate
/// members.
SWIFT_RUNTIME_EXPORT
ClassMetadata *
swift_relocateClassMetadata(ClassMetadata *self,
                            size_t templateSize,
                            size_t numImmediateMembers);

/// Initialize the field offset vector for a dependent-layout class, using the
/// "Universal" layout strategy.
SWIFT_RUNTIME_EXPORT
void swift_initClassMetadata_UniversalStrategy(ClassMetadata *self,
                                               size_t numFields,
                                               const TypeLayout * const *fieldTypes,
                                               size_t *fieldOffsets);

/// \brief Fetch a uniqued metadata for a metatype type.
SWIFT_RUNTIME_EXPORT
const MetatypeMetadata *
swift_getMetatypeMetadata(const Metadata *instanceType);

/// \brief Fetch a uniqued metadata for an existential metatype type.
SWIFT_RUNTIME_EXPORT
const ExistentialMetatypeMetadata *
swift_getExistentialMetatypeMetadata(const Metadata *instanceType);

/// \brief Fetch a uniqued metadata for an existential type. The array
/// referenced by \c protocols will be sorted in-place.
SWIFT_RUNTIME_EXPORT
const ExistentialTypeMetadata *
swift_getExistentialTypeMetadata(ProtocolClassConstraint classConstraint,
                                 const Metadata *superclassConstraint,
                                 size_t numProtocols,
                                 const ProtocolDescriptor * const *protocols);

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with the
/// same number of witness tables.
SWIFT_RUNTIME_EXPORT
OpaqueValue *swift_assignExistentialWithCopy(OpaqueValue *dest,
                                             const OpaqueValue *src,
                                             const Metadata *type);

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with no
/// witness tables.
OpaqueValue *swift_assignExistentialWithCopy0(OpaqueValue *dest,
                                              const OpaqueValue *src,
                                              const Metadata *type);

/// \brief Perform a copy-assignment from one existential container to another.
/// Both containers must be of the same existential type representable with one
/// witness table.
OpaqueValue *swift_assignExistentialWithCopy1(OpaqueValue *dest,
                                              const OpaqueValue *src,
                                              const Metadata *type);

/// Calculate the numeric index of an extra inhabitant of a heap object
/// pointer in memory.
inline int swift_getHeapObjectExtraInhabitantIndex(HeapObject * const* src) {
  // This must be consistent with the getHeapObjectExtraInhabitantIndex
  // implementation in IRGen's ExtraInhabitants.cpp.

  using namespace heap_object_abi;

  uintptr_t value = reinterpret_cast<uintptr_t>(*src);
  if (value >= LeastValidPointerValue)
    return -1;

  // Check for tagged pointers on appropriate platforms.  Knowing that
  // value < LeastValidPointerValue tells us a lot.
#if SWIFT_OBJC_INTEROP
  if (value & ((uintptr_t(1) << ObjCReservedLowBits) - 1))
    return -1;
#endif

  return (int) (value >> ObjCReservedLowBits);
}
  
/// Store an extra inhabitant of a heap object pointer to memory,
/// in the style of a value witness.
inline void swift_storeHeapObjectExtraInhabitant(HeapObject **dest, int index) {
  // This must be consistent with the storeHeapObjectExtraInhabitant
  // implementation in IRGen's ExtraInhabitants.cpp.

  auto value = uintptr_t(index) << heap_object_abi::ObjCReservedLowBits;
  *dest = reinterpret_cast<HeapObject*>(value);
}

/// Return the number of extra inhabitants in a heap object pointer.
inline constexpr unsigned swift_getHeapObjectExtraInhabitantCount() {
  // This must be consistent with the getHeapObjectExtraInhabitantCount
  // implementation in IRGen's ExtraInhabitants.cpp.

  using namespace heap_object_abi;

  // The runtime needs no more than INT_MAX inhabitants.
  return (LeastValidPointerValue >> ObjCReservedLowBits) > INT_MAX
    ? (unsigned)INT_MAX
    : (unsigned)(LeastValidPointerValue >> ObjCReservedLowBits);
}  

/// Calculate the numeric index of an extra inhabitant of a function
/// pointer in memory.
inline int swift_getFunctionPointerExtraInhabitantIndex(void * const* src) {
  // This must be consistent with the getFunctionPointerExtraInhabitantIndex
  // implementation in IRGen's ExtraInhabitants.cpp.
  uintptr_t value = reinterpret_cast<uintptr_t>(*src);
  return (value < heap_object_abi::LeastValidPointerValue
            ? (int) value : -1);
}
  
/// Store an extra inhabitant of a function pointer to memory, in the
/// style of a value witness.
inline void swift_storeFunctionPointerExtraInhabitant(void **dest, int index) {
  // This must be consistent with the storeFunctionPointerExtraInhabitantIndex
  // implementation in IRGen's ExtraInhabitants.cpp.
  *dest = reinterpret_cast<void*>(static_cast<uintptr_t>(index));
}

/// Return the number of extra inhabitants in a function pointer.
inline constexpr unsigned swift_getFunctionPointerExtraInhabitantCount() {
  // This must be consistent with the getFunctionPointerExtraInhabitantCount
  // implementation in IRGen's ExtraInhabitants.cpp.

  using namespace heap_object_abi;

  // The runtime needs no more than INT_MAX inhabitants.
  return (LeastValidPointerValue) > INT_MAX
    ? (unsigned)INT_MAX
    : (unsigned)(LeastValidPointerValue);
}

/// Return the type name for a given type metadata.
std::string nameForMetadata(const Metadata *type,
                            bool qualified = true);

/// Register a block of protocol records for dynamic lookup.
SWIFT_RUNTIME_EXPORT
void swift_registerProtocols(const ProtocolRecord *begin,
                             const ProtocolRecord *end);

/// Register a block of protocol conformance records for dynamic lookup.
SWIFT_RUNTIME_EXPORT
void swift_registerProtocolConformances(const ProtocolConformanceRecord *begin,
                                        const ProtocolConformanceRecord *end);

/// Register a block of type context descriptors for dynamic lookup.
SWIFT_RUNTIME_EXPORT
void swift_registerTypeMetadataRecords(const TypeMetadataRecord *begin,
                                       const TypeMetadataRecord *end);

/// Return the superclass, if any.  The result is nullptr for root
/// classes and class protocol types.
SWIFT_CC(swift)
SWIFT_RUNTIME_STDLIB_INTERFACE
const Metadata *_swift_class_getSuperclass(const Metadata *theClass);

} // end namespace swift

#pragma clang diagnostic pop

#endif /* SWIFT_RUNTIME_METADATA_H */
