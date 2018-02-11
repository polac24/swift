//===--- Job.h - Commands to Execute ----------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DRIVER_JOB_H
#define SWIFT_DRIVER_JOB_H

#include "swift/Basic/LLVM.h"
#include "swift/Driver/Action.h"
#include "swift/Driver/OutputFileMap.h"
#include "swift/Driver/Types.h"
#include "swift/Driver/Util.h"
#include "llvm/Option/Option.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

namespace swift {
namespace driver {

class Job;
class JobAction;

/// \file Job.h
///
///Some terminology for the following sections (and especially Driver.cpp):
///
/// BaseInput: a filename provided by the user, upstream of the entire Job
///            graph, usually denoted by an InputAction. Every Job has access,
///            during construction, to a set of BaseInputs that are upstream of
///            its inputs and input jobs in the job graph, and from which it can
///            derive PrimaryInput names for itself.
///
/// BaseOutput: a filename that is a non-temporary, output at the bottom of a
///             Job graph, and often (though not always) directly specified by
///             the user in the form of a -o or -emit-foo-path name, or an entry
///             in a user-provided OutputFileMap. May also be an auxiliary,
///             derived from a BaseInput and a type.
///
/// PrimaryInput: one of the distinguished inputs-to-act-on (as opposed to
///               merely informative additional inputs) to a Job. May be a
///               BaseInput but may also be a temporary that doesn't live beyond
///               the execution of the Job graph.
///
/// PrimaryOutput: an output file matched 1:1 with a specific
///                PrimaryInput. Auxiliary outputs may also be produced. A
///                PrimaryOutput may be a BaseOutput, but may also be a
///                temporary that doesn't live beyond the execution of the Job
///                graph (that is: it exists in order to be the PrimaryInput
///                for a subsequent Job).
///
/// The user-provided OutputFileMap lists BaseInputs and BaseOutputs, but doesn't
/// describe the temporaries inside the Job graph.
///
/// The Compilation's DerivedOutputFileMap (shared by all CommandOutputs) lists
/// PrimaryInputs and maps them to PrimaryOutputs, including all the
/// temporaries. This means that in a multi-stage Job graph, the BaseInput =>
/// BaseOutput entries provided by the user are split in two (or more) steps,
/// one BaseInput => SomeTemporary and one SomeTemporary => BaseOutput.
///
/// To try to keep this as simple as possible (it's already awful) we associate
/// every PrimaryInput 1:1 with a specific BaseInput from which it was derived;
/// this way a CommandOutput will have a vector of _pairs_ of
/// {Base,Primary}Inputs rather than a pair of separate vectors. This arrangement
/// appears to cover all the graph topologies we encounter in practice.


struct CommandInputPair {
  /// A filename provided from the user, either on the command line or in an
  /// input file map. Feeds into a Job graph, from InputActions, and is
  /// _associated_ with a PrimaryInput for a given Job, but may be upstream of
  /// the Job (and its PrimaryInput) and thus not necessarily passed as a
  /// filename to the job. Used as a key into the user-provided OutputFileMap
  /// (of BaseInputs and BaseOutputs), and used to derive downstream names --
  /// both temporaries and auxiliaries -- but _not_ used as a key into the
  /// DerivedOutputFileMap.
  StringRef Base;

  /// A filename that _will be passed_ to the command as a designated primary
  /// input. Typically either equal to BaseInput or a temporary with a name
  /// derived from the BaseInput it is related to. Also used as a key into
  /// the DerivedOutputFileMap.
  StringRef Primary;
};

class CommandOutput {

  /// A CommandOutput designates one type of output as primary, though there
  /// may be multiple outputs of that type.
  types::ID PrimaryOutputType;

  /// A CommandOutput also restricts its attention regarding additional-outputs
  /// to a subset of the PrimaryOutputs associated with its PrimaryInputs;
  /// sometimes multiple commands operate on the same PrimaryInput, in different
  /// phases (eg. autolink-extract and link both operate on the same .o file),
  /// so Jobs cannot _just_ rely on the presence of a primary output in the
  /// DerivedOutputFileMap.
  llvm::SmallSet<types::ID, 4> AdditionalOutputTypes;

  /// The set of input filenames for this \c CommandOutput; combined with \c
  /// DerivedOutputMap, specifies a set of output filenames (of which one -- the
  /// one of type \c PrimaryOutputType) is the primary output filename.
  SmallVector<CommandInputPair, 1> Inputs;

  /// All CommandOutputs in a Compilation share the same \c
  /// DerivedOutputMap. This is computed both from any user-provided input file
  /// map, and any inference steps.
  OutputFileMap &DerivedOutputMap;

  // If there is an entry in the DerivedOutputMap for a given (\p
  // PrimaryInputFile, \p Type) pair, return a nonempty StringRef, otherwise
  // return an empty StringRef.
  StringRef
  getOutputForInputAndType(StringRef PrimaryInputFile, types::ID Type) const;

  /// Add an entry to the \c DerivedOutputMap if it doesn't exist. If an entry
  /// already exists for \p PrimaryInputFile of type \p type, then either
  /// overwrite the entry (if \p overwrite is \c true) or assert that it has
  /// the same value as \p OutputFile.
  void ensureEntry(StringRef PrimaryInputFile,
                   types::ID Type,
                   StringRef OutputFile,
                   bool Overwrite);

public:
  CommandOutput(types::ID PrimaryOutputType, OutputFileMap &Derived);

  /// Return the primary output type for this CommandOutput.
  types::ID getPrimaryOutputType() const;

  /// Associate a new \p PrimaryOutputFile (of type \c getPrimaryOutputType())
  /// with the provided \p Input pair of Base and Primary inputs.
  void addPrimaryOutput(CommandInputPair Input, StringRef PrimaryOutputFile);

  /// Assuming (and asserting) that there is only one input pair, return the
  /// primary output file associated with it. Note that the returned StringRef
  /// may be invalidated by subsequent mutations to the \c CommandOutput.
  StringRef getPrimaryOutputFilename() const;

  /// Return a all of the outputs of type \c getPrimaryOutputType() associated
  /// with a primary input. Note that the returned \c StringRef vector may be
  /// invalidated by subsequent mutations to the \c CommandOutput.
  SmallVector<StringRef, 16> getPrimaryOutputFilenames() const;

  /// Assuming (and asserting) that there are one or more input pairs, associate
  /// an additional output named \p OutputFilename of type \p type with the
  /// first primary input. If the provided \p type is the primary output type,
  /// overwrite the existing entry assocaited with the first primary input.
  void setAdditionalOutputForType(types::ID type, StringRef OutputFilename);

  /// Assuming (and asserting) that there are one or more input pairs, return
  /// the _additional_ (not primary) output of type \p type associated with the
  /// first primary input.
  StringRef getAdditionalOutputForType(types::ID type) const;

  /// Assuming (and asserting) that there is only one input pair, return any
  /// output -- primary or additional -- of type \p type associated with that
  /// the sole primary input.
  StringRef getAnyOutputForType(types::ID type) const;

  /// Return the BaseInput numbered by \p Index.
  StringRef getBaseInput(size_t Index) const;

  void print(raw_ostream &Stream) const;
  void dump() const LLVM_ATTRIBUTE_USED;
};

class Job {
public:
  enum class Condition {
    Always,
    RunWithoutCascading,
    CheckDependencies,
    NewlyAdded
  };

  using EnvironmentVector = std::vector<std::pair<const char *, const char *>>;

private:
  /// The action which caused the creation of this Job, and the conditions
  /// under which it must be run.
  llvm::PointerIntPair<const JobAction *, 2, Condition> SourceAndCondition;

  /// The list of other Jobs which are inputs to this Job.
  SmallVector<const Job *, 4> Inputs;

  /// The output of this command.
  std::unique_ptr<CommandOutput> Output;

  /// The executable to run.
  const char *Executable;

  /// The list of program arguments (not including the implicit first argument,
  /// which will be the Executable).
  ///
  /// These argument strings must be kept alive as long as the Job is alive.
  llvm::opt::ArgStringList Arguments;

  /// Additional variables to set in the process environment when running.
  ///
  /// These strings must be kept alive as long as the Job is alive.
  EnvironmentVector ExtraEnvironment;

  /// Whether the job wants a list of input or output files created.
  std::vector<FilelistInfo> FilelistFileInfos;

  /// The modification time of the main input file, if any.
  llvm::sys::TimePoint<> InputModTime = llvm::sys::TimePoint<>::max();

public:
  Job(const JobAction &Source,
      SmallVectorImpl<const Job *> &&Inputs,
      std::unique_ptr<CommandOutput> Output,
      const char *Executable,
      llvm::opt::ArgStringList Arguments,
      EnvironmentVector ExtraEnvironment = {},
      std::vector<FilelistInfo> Infos = {})
      : SourceAndCondition(&Source, Condition::Always),
        Inputs(std::move(Inputs)), Output(std::move(Output)),
        Executable(Executable), Arguments(std::move(Arguments)),
        ExtraEnvironment(std::move(ExtraEnvironment)),
        FilelistFileInfos(std::move(Infos)) {}

  const JobAction &getSource() const {
    return *SourceAndCondition.getPointer();
  }

  const char *getExecutable() const { return Executable; }
  const llvm::opt::ArgStringList &getArguments() const { return Arguments; }
  ArrayRef<FilelistInfo> getFilelistInfos() const { return FilelistFileInfos; }

  ArrayRef<const Job *> getInputs() const { return Inputs; }
  const CommandOutput &getOutput() const { return *Output; }

  Condition getCondition() const {
    return SourceAndCondition.getInt();
  }
  void setCondition(Condition Cond) {
    SourceAndCondition.setInt(Cond);
  }

  void setInputModTime(llvm::sys::TimePoint<> time) {
    InputModTime = time;
  }

  llvm::sys::TimePoint<> getInputModTime() const {
    return InputModTime;
  }

  ArrayRef<std::pair<const char *, const char *>> getExtraEnvironment() const {
    return ExtraEnvironment;
  }

  /// Print the command line for this Job to the given \p stream,
  /// terminating output with the given \p terminator.
  void printCommandLine(raw_ostream &Stream, StringRef Terminator = "\n") const;

  /// Print a short summary of this Job to the given \p Stream.
  void printSummary(raw_ostream &Stream) const;

  /// Print the command line for this Job to the given \p stream,
  /// and include any extra environment variables that will be set.
  ///
  /// \sa printCommandLine
  void printCommandLineAndEnvironment(raw_ostream &Stream,
                                      StringRef Terminator = "\n") const;

  void dump() const LLVM_ATTRIBUTE_USED;

  static void printArguments(raw_ostream &Stream,
                             const llvm::opt::ArgStringList &Args);
};

/// A BatchJob comprises a _set_ of jobs, each of which is sufficiently similar
/// to the others that the whole set can be combined into a single subprocess
/// (and thus run potentially more-efficiently than running each Job in the set
/// individually).
///
/// Not all Jobs can be combined into a BatchJob: at present, only those Jobs
/// that come from CompileJobActions, and which otherwise have the exact same
/// input file list and arguments as one another, aside from their primary-file.
/// See ToolChain::jobsAreBatchCombinable for details.

class BatchJob : public Job {
  SmallVector<const Job *, 4> CombinedJobs;
public:
  BatchJob(const JobAction &Source, SmallVectorImpl<const Job *> &&Inputs,
           std::unique_ptr<CommandOutput> Output, const char *Executable,
           llvm::opt::ArgStringList Arguments,
           EnvironmentVector ExtraEnvironment,
           std::vector<FilelistInfo> Infos,
           ArrayRef<const Job *> Combined);

  ArrayRef<const Job*> getCombinedJobs() const {
    return CombinedJobs;
  }
};

} // end namespace driver
} // end namespace swift

#endif
