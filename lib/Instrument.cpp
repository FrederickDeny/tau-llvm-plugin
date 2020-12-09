//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//


#include <fstream>
#include <regex>

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstIterator.h"

// Need these to use Sampson's registration technique
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

#ifdef TAU_PROF_CXX
#include <cxxabi.h>
#endif

// Other passes do this, so I assume the macro is useful somewhere
#define DEBUG_TYPE "tau-profile"

#define TAU_BEGIN_INCLUDE_LIST_NAME      "BEGIN_INCLUDE_LIST"
#define TAU_END_INCLUDE_LIST_NAME        "END_INCLUDE_LIST"
#define TAU_BEGIN_EXCLUDE_LIST_NAME      "BEGIN_EXCLUDE_LIST"
#define TAU_END_EXCLUDE_LIST_NAME        "END_EXCLUDE_LIST"
#define TAU_BEGIN_FILE_INCLUDE_LIST_NAME "BEGIN_FILE_INCLUDE_LIST"
#define TAU_END_FILE_INCLUDE_LIST_NAME   "END_FILE_INCLUDE_LIST"
#define TAU_BEGIN_FILE_EXCLUDE_LIST_NAME "BEGIN_FILE_EXCLUDE_LIST"
#define TAU_END_FILE_EXCLUDE_LIST_NAME   "END_FILE_EXCLUDE_LIST"

#define TAU_REGEX_STAR              '#'
#define TAU_REGEX_FILE_STAR         '*'
#define TAU_REGEX_FILE_QUES         '?'

/*
  TODO: finish exclude
*/

using namespace llvm;

namespace {

// Command line options for this plugin.  These permit the user to specify what
// functions should be instrumented and what profiling functions to call.  The
// only real caveat is that the profiling function symbols must be present in
// some source/object/library or compilation will fail at link-time.
cl::opt<std::string>
TauInputFile("tau-input-file",
             cl::desc("Specify file containing the names of functions to instrument"),
             cl::value_desc("filename"));

cl::opt<std::string>
TauStartFunc("tau-start-func",
             cl::desc("Specify the profiling function to call before functions of interest"),
             cl::value_desc("Function name"),
             cl::init("Tau_start"));

cl::opt<std::string>
TauStopFunc("tau-stop-func",
            cl::desc("Specify the profiling function to call after functions of interest"),
            cl::value_desc("Function name"),
            cl::init("Tau_stop"));

cl::opt<std::string>
TauRegex("tau-regex",
         cl::desc("Specify a regex to identify functions interest (case-sensitive)"),
         cl::value_desc("Regular Expression"),
         cl::init(""));

cl::opt<std::string>
TauIRegex("tau-iregex",
         cl::desc("Specify a regex to identify functions interest (case-insensitive)"),
         cl::value_desc("Regular Expression"),
         cl::init(""));

cl::opt<bool>
TauDryRun("tau-dry-run",
         cl::desc("Don't actually instrument the code, just print what would be instrumented"));



// Demangling technique borrowed/modified from
// https://github.com/eklitzke/demangle/blob/master/src/demangle.cc
static StringRef normalize_name(StringRef mangled_name) {
#ifdef TAU_PROF_CXX
  int status = 0;

  const char *str = abi::__cxa_demangle(mangled_name.begin(), 0, 0, &status);
  StringRef realname{str};

  switch (status) {
  case 0:
    break;
  case -1:
    // errs() << "FAIL: failed to allocate memory while demangling "
    //        << mangled_name << '\n';
    break;
  case -2:
    // errs() << "FAIL: " << mangled_name
    //        << " is not a valid name under the C++ ABI mangling rules\n";
    break;
  default:
    // errs() << "FAIL: couldn't demangle " << mangled_name
    //        << " for some unknown reason: " << status << '\n';
    break;
  }

  return realname;
#else
  return mangled_name;
#endif
}

  /*!
   *  Find/declare a function taking a single `i8*` argument with a void return
   *  type suitable for making a call to in IR. This is used to get references
   *  to the TAU profiling function symbols.
   *
   * \param funcname The name of the function
   * \param ctx The LLVMContext
   * \param mdl The Module in which the function will be used
   */
#if( LLVM_VERSION_MAJOR <= 8 )
static Constant *getVoidFunc(StringRef funcname, LLVMContext &context, Module *module) {
#else
static FunctionCallee getVoidFunc(StringRef funcname, LLVMContext &context, Module *module) {
#endif // LLVM_VERSION_MAJOR <= 8

    // Void return type
    Type *retTy = Type::getVoidTy(context);

    // single i8* argument type (char *)
    Type *argTy = Type::getInt8PtrTy(context);
    SmallVector<Type *, 1> paramTys{argTy};

    // Third param to `get` is `isVarArg`.  It's not documented, but might have
    // to do with variadic functions?
    FunctionType *funcTy = FunctionType::get(retTy, paramTys, false);
    return module->getOrInsertFunction(funcname, funcTy);
}


  /*!
   * The instrumentation pass.
   */
struct Instrument : public FunctionPass {

    using CallAndName = std::pair<CallInst *, StringRef>;

    static char ID; // Pass identification, replacement for typeid
    StringSet<> funcsOfInterest;
    StringSet<> funcsExcl;
    StringSet<> funcsOfInterestRegex;
    StringSet<> funcsExclRegex;
  
    StringSet<> filesIncl;
    StringSet<> filesExcl;
    StringSet<> filesInclRegex;
    StringSet<> filesExclRegex;

    // basic ==> POSIX regular expression
    std::regex rex{TauRegex,
                   std::regex_constants::ECMAScript};
    std::regex irex{TauIRegex,
                    std::regex_constants::ECMAScript | std::regex_constants::icase};

    Instrument() : FunctionPass(ID) {
      if(!TauInputFile.empty()) {
        std::ifstream ifile{TauInputFile};
        loadFunctionsFromFile(ifile);
      }
    }

  /*! 
   * Given an open file, a token and two vectors, read what is coming next and 
   * put it in the vector or its regex counterpart until the token has been
   * reached.
   */
  void readUntilToken( std::ifstream& file, StringSet<>& vec, StringSet<>& vecReg, const char* token ){
    std::string funcName;
    std::string s_token( token ); // used by an errs()
    bool rc = true;

    while( std::getline( file, funcName ) ){
      if( funcName.find_first_not_of(' ') != std::string::npos ) {
	/* Exclude whitespace-only lines */
       
	if( 0 == funcName.compare( token ) ){
	  return;
	}

	if( s_token.end() == std::find( s_token.begin(), s_token.end(), 'X' ) ){
	  errs() << "Include";
	} else {
	  errs() << "Exclude";
	}
	if( s_token.end() == std::find( s_token.begin(), s_token.end(), 'F' ) ){
	  errs() << " function: " << funcName;
	} else {
	  errs() << " file " << funcName;
	}

	if( funcName.end() != std::find( funcName.begin(), funcName.end(), TAU_REGEX_STAR )
	    || funcName.end() != std::find( funcName.begin(), funcName.end(), TAU_REGEX_FILE_STAR )
	    || funcName.end() != std::find( funcName.begin(), funcName.end(), TAU_REGEX_FILE_QUES ) ) {
	  errs() << " (regex)";
	  vecReg.insert( funcName );
	} else {
	  vec.insert( funcName );
	}
	errs() << "\n";
      }
    }
    
    if( rc ){
      errs() << "Error while reading the instrumentation list in the input file. Did you close it with " << token << "?\n";
    }
    
  }


    /*!
     *  Given an open file, read each line as the name of a function that should
     *  be instrumented.  This modifies the class member funcsOfInterest to hold
     *  strings from the file.
     */
    void loadFunctionsFromFile(std::ifstream & file) {
      std::string funcName;
      bool rc = true;

      /* This will be necessary as long as we don't have pattern matching in C++ */
      enum TokenValues { begin_func_include, begin_func_exclude,
			 begin_file_include, begin_file_exclude };
      
      static std::map<std::string, TokenValues> s_mapTokenValues;
      
      s_mapTokenValues[ TAU_BEGIN_INCLUDE_LIST_NAME ] = begin_func_include;
      s_mapTokenValues[ TAU_BEGIN_EXCLUDE_LIST_NAME ] = begin_func_exclude;
      s_mapTokenValues[ TAU_BEGIN_FILE_INCLUDE_LIST_NAME ] = begin_file_include;
      s_mapTokenValues[ TAU_BEGIN_FILE_EXCLUDE_LIST_NAME ] = begin_file_exclude;
      
      while(std::getline(file, funcName)) {

	if( funcName.find_first_not_of(' ') != std::string::npos ) {
	  /* Exclude whitespace-only lines */
	  
	  switch( s_mapTokenValues[ funcName ]){
	  case begin_func_include:
	    errs() << "Included functions: \n";
	    readUntilToken( file, funcsOfInterest, funcsOfInterestRegex, TAU_END_INCLUDE_LIST_NAME );
	    break;
	    
	  case begin_func_exclude:
	    errs() << "Excluded functions: \n";
	    readUntilToken( file, funcsExcl, funcsExclRegex, TAU_END_EXCLUDE_LIST_NAME );
	    break;
	 
	  case begin_file_include:
	    errs() << "Included files: \n";
	    readUntilToken( file, filesIncl, filesInclRegex, TAU_END_FILE_INCLUDE_LIST_NAME );
	    break;

	  case begin_file_exclude:
	    errs() << "Excluded files: \n";
	    readUntilToken( file, filesExcl, filesExclRegex, TAU_END_FILE_EXCLUDE_LIST_NAME );
	    break;
	 
	  default:
	    errs() << "Wrong syntax: the lists must be between ";
	    errs() << TAU_BEGIN_INCLUDE_LIST_NAME << " and " << TAU_END_INCLUDE_LIST_NAME;
	    errs() << " for the list of functions to instrument and ";
	    errs() << TAU_BEGIN_EXCLUDE_LIST_NAME << " and " << TAU_END_EXCLUDE_LIST_NAME;
	    errs() << " for the list of functions to exclude.\n";
	    break;
	  }
	}
      }
    }

    /*!
     *  The FunctionPass interface method, called on each function produced from
     *  the original source.
     */
    bool runOnFunction(Function &func) override {
      bool modified = false;

      bool instru = maybeSaveForProfiling( func );

      if( TauDryRun ) {
        // TODO: Fix this.
        // getName() doesn't seem to give a properly mangled name
	/*  auto pretty_name = normalize_name(func.getName());
        if(pretty_name.empty()) pretty_name = func.getName();
	errs() << pretty_name << " would be instrumented\n";*/
        return false; // Dry run does not modify anything
      }
      if( instru ){
	modified |= addInstrumentation( func );
      }
      return modified;
    }
  
    /*!
     *  Inspect the given CallInst and, if it should be profiled, add it
     *  and its recognized name the given vector.
     *
     * \param call The CallInst to inspect
     * \param calls Vector to add to, if the CallInst should be profiled
     */
  bool maybeSaveForProfiling( Function& call ){
	StringRef callName = call.getName();
	std::string filename = call.getParent()->getSourceFileName();

	/* This big test was explanded for readability */
	bool instrumentHere = false;
	
	/* Are we including or excluding some files? */
	if( (filesIncl.size() + filesInclRegex.size() + filesExcl.size() + filesExclRegex.size() == 0 ) ){
	  instrumentHere = true;
	} else {
	  /* Yes: are we in a file where we are instrumenting? */
	  if( ( filesIncl.count( filename ) > 0 
		|| regexFitsFile( filename, filesInclRegex ) )
	      || !( filesExcl.count( filename )
		    || regexFitsFile( filename, filesExclRegex ) ) ){
	    instrumentHere = true;
	  }
	}
	if( instrumentHere
	    &&
	    ( funcsOfInterest.count( callName ) > 0
	      || regexFits ( callName, funcsOfInterestRegex )
	      //	      || funcsOfInterest.count(calleeAndParent) > 0
	      )
	    && !( funcsExcl.count( callName )
		  || regexFits( callName, funcsExclRegex )
		  ) ) {
	  errs() << "Instrument " << callName << "\n";
	  return true;
	}
	return false;
  }
  
    /*! 
     * This function determines if the current function name (parameter name)
     * matches a regular expression. Regular expressions can be passed either 
     * on the command-line (historical behavior) or in the input file. The latter
     * use a specific wildcard.
     */
    bool regexFits( const StringRef & name, StringSet<>& regexList ) {
        /* Regex coming from the command-line */
        bool match = false, imatch = false;
        if(!TauRegex.empty()) match = std::regex_search(name.str(), rex);
        if(!TauIRegex.empty()) imatch = std::regex_search(name.str(), irex);

        if( match || imatch ) return true;
        
        /* Regex coming from the input file, using '#' as the wildcard */
        for( auto& r : regexList ){
            auto rc = std::search( name.begin(), name.end(),
                                   r.getKey().begin(), r.getKey().end(),
                                   []( char txt, char pattern ) {
                                       return pattern == TAU_REGEX_STAR || pattern == txt;
                                   } );
            //   errs() << "rc: " << rc << "\n";
            if ( name.end() != rc ) return true;
        }
        
        return false;
    }

  /*!
   * Regex utility. This function takes one of our filename as an input
   * and converts it into a regex that can be used by std::regex.
  */
  std::regex getRegex( std::string str, std::string c ){
    std::regex q( std::string( "[" + c + "]" ) );
    std::string q_reg( std::string(  "(." + c + ")" ) );
    std::string regex_1;
    
    std::regex_replace( std::back_inserter( regex_1 ), str.begin(), str.end(), q, q_reg );
    return std::regex( regex_1 );
  }

  /*!
   * TODO
   * document
   * can be factorized with regexfit
   */
  bool regexFitsFile( const StringRef & name, StringSet<>& regexList ) {
    bool match = false;
    for( auto& r : regexList ){
      std::regex r1 = getRegex( r.getKey().str(), std::string( 1, TAU_REGEX_FILE_STAR ));
      std::regex r2 = getRegex( r.getKey().str(), std::string( 1, TAU_REGEX_FILE_QUES ));
      
      match  = std::regex_match( name.str(), r1 );
      match |= std::regex_match( name.str(), r2 );

      if( match ) return true;
    }
   return false;
  }

    /*!
     *  Add instrumentation to the CallInsts in the given vector, using the
     *  given function for context.
     *
     * \param calls vector of calls to add profiling to
     * \param func Function in which the calls were found
     * \return False if no new instructions were added (only when calls is empty),
     *  True otherwise
     */
    bool addInstrumentation( Function &func) {

      // Declare and get handles to the runtime profiling functions
      auto &context = func.getContext();
      auto *module = func.getParent();
#if( LLVM_VERSION_MAJOR <= 8 )
      Constant
        *onCallFunc = getVoidFunc(TauStartFunc, context, module),
        *onRetFunc = getVoidFunc(TauStopFunc, context, module);
#else
      FunctionCallee
        onCallFunc = getVoidFunc(TauStartFunc, context, module),
        onRetFunc = getVoidFunc(TauStopFunc, context, module);
#endif // LLVM_VERSION_MAJOR <= 8

      errs() << "Adding instrumentation in " << func.getName() << '\n';

      // Insert instrumentation before the first instruction
      auto pi = inst_begin( &func );
      Instruction* i = &*pi;
      IRBuilder<> before( i );

      bool mutated = false; // TODO

      // This is the recommended way of creating a string constant (to be used
      // as an argument to runtime functions)

      Value *strArg = before.CreateGlobalStringPtr( func.getName() );
      SmallVector<Value *, 1> args{strArg};
      before.CreateCall( onCallFunc, args );
      mutated = true;

      // We need to find all the exit points for this function

      for( inst_iterator I = inst_begin( func ), E = inst_end( func ); I != E; ++I){
	Instruction* e = &*I;
	IRBuilder<> final( e );
	if( isa<ReturnInst>( e ) ) {
	  final.CreateCall( onRetFunc, args );
	}	  
      }
      return mutated;
    }
};
}

char Instrument::ID = 0;

static RegisterPass<Instrument> X("tau-prof", "TAU Profiling", false, false);

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerInstrumentPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new Instrument());
}
static RegisterStandardPasses
RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible, registerInstrumentPass);
