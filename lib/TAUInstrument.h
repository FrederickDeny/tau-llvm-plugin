#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#include "llvm/ADT/StringRef.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// Command line options for this plugin.  These permit the user to specify what
// functions should be instrumented and what profiling functions to call.  The
// only real caveat is that the profiling function symbols must be present in
// some source/object/library or compilation will fail at link-time.

static cl::opt<std::string> TauInputFile(
    "tau-input-file",
    cl::desc("Specify file containing the names of functions to instrument"),
    cl::value_desc("filename"));

static cl::opt<std::string> TauStartFunc(
    "tau-start-func",
    cl::desc(
        "Specify the profiling function to call before functions of interest"),
    cl::value_desc("Function name"), cl::init("Tau_start"));

static cl::opt<std::string> TauStopFunc(
    "tau-stop-func",
    cl::desc(
        "Specify the profiling function to call after functions of interest"),
    cl::value_desc("Function name"), cl::init("Tau_stop"));

static cl::opt<std::string> TauRegex(
    "tau-regex",
    cl::desc("Specify a regex to identify functions interest (case-sensitive)"),
    cl::value_desc("Regular Expression"), cl::init(""));

static cl::opt<std::string> TauIRegex(
    "tau-iregex",
    cl::desc(
        "Specify a regex to identify functions interest (case-insensitive)"),
    cl::value_desc("Regular Expression"), cl::init(""));

static cl::opt<bool>
    TauDryRun("tau-dry-run",
              cl::desc("Don't actually instrument the code, just print "
                       "what would be instrumented"));

struct TAUInstrument : public PassInfoMixin<TAUInstrument> {

  StringSet<> funcsOfInterest;
  StringSet<> funcsExcl;
  // StringSet<> funcsOfInterestRegex;
  // StringSet<> funcsExclRegex;
  std::vector<std::regex> funcsOfInterestRegex;
  std::vector<std::regex> funcsExclRegex;

  StringSet<> filesIncl;
  StringSet<> filesExcl;
  // StringSet<> filesInclRegex;
  //  StringSet<> filesExclRegex;
  std::vector<std::regex> filesInclRegex;
  std::vector<std::regex> filesExclRegex;

  // basic ==> POSIX regular expression
  std::regex rex{TauRegex, std::regex_constants::ECMAScript};
  std::regex irex{TauIRegex, std::regex_constants::ECMAScript |
                                 std::regex_constants::icase};

  void loadFunctionsFromFile(std::ifstream &file);
  bool maybeSaveForProfiling(Function &call);
  bool regexFits(const StringRef &name, std::vector<std::regex> &regexList,
                 bool cli = false);
  bool addInstrumentation(Function &func);
  void readUntilToken(std::ifstream &file, StringSet<> &vec,
                      std::vector<std::regex> &vecReg, const char *token);

  TAUInstrument() {
    if (!TauInputFile.empty()) {
      std::ifstream ifile{TauInputFile};
      loadFunctionsFromFile(ifile);
      errs() << "functions were loaded from file \n";
    }
  }

  using CallAndName = std::pair<CallInst *, StringRef>;
  PreservedAnalyses run(Function &func, FunctionAnalysisManager &AM);

  bool runOnFunction(Function &func);
};

/*!
 *    * The instrumentation pass.
 *       */
struct LegacyTAUInstrument : public FunctionPass {

  static char ID; // Pass identification, replacement for typeid

  LegacyTAUInstrument() : FunctionPass(ID) {}
  bool runOnFunction(Function &func) override;

  TAUInstrument Impl;
};

} // namespace
