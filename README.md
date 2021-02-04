# Adding TAU profiling calls via LLVM pass

## Building

This pass (in `lib/Instrument.cpp`) should be built against the install of LLVM you want to use it in. The `master` branch was tested against 6.0, 7.0, 8.0, 9.0, 10.0, 11.0 and the current master branch (12.0).

### Independently

Starting at the project root,

``` bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Don't forget to set the `$LLVM_DIR`, `$CC` and `$CXX` environment variables.
The configuration of TAU that will be used is set by the environment
variable TAU_MAKEFILE. The compilers (`$CC` and `$CXX`) must be the same as
the ones used to build the LLVM installation you are building against.

## Usage

The plugin accepts some optional command line arguments, that permit the
user to specify:

  - `-tau-start-func`  
    The function to call *before* each instrumented function call-site.
    By default this is `Tau_start`
  - `-tau-stop-func`  
    The function to call *after* each instrumented function call-site.
    By default this is `Tau_stop`
  - `-tau-input-file`  
    A file containing the names of functions to instrument. This has no
    default, but failing to specify such a file will result in no
    instrumentation.
  - `-tau-regex`  
    A case-sensitive ECMAScript Regular Expression to test against
    function names. All functions matching the expression will be
    instrumented
  - `-tau-iregex`  
    A case-insensitive ECMAScript Regular Expression to test against
    function names. All functions matching the expression will be
    instrumented

They can be set using `clang`, `clang++`, or `opt` with LLVM bitcode
files. Only usage with Clang frontends is detailed here.

To use the plugin with the default start and stop functions, you must
know the path to the TAU shared library. To use alternative functions,
you'll need the path to the appropriate libraries for them.

At the moment, there are three source files and a sample file containing
function names in the `sandbox` directory that can be used for simple
tests.

  - `rtlib.c` defines two functions that could be used as alternatives
    to `Tau_start` and `Tau_stop`.
  - `example.c` is a Hello World C program to test the pass on
  - `example.cc` is a Hello World C++ program with some OO features to
    see what kinds of calls are visible after lowering to LLVM IR.

An example using several source files is also given in the `sandbox/mm`
directory, and a more complex program is given in the `sandbox/hh`
directory.

The following instructions assume `TAU_Profiling.so` and
`Tau_Profiling_CXX.so` have been built against the system installed
LLVM. If this is not the case, replace invocations of `clang` and
`clang++` with the appropriate versions.

If used, the runtime library must be compiled first (producing a shared
library is also OK):

``` bash
clang -c rtlib.c
# produces rtlib.o
```

To compile and link the example C program with the plugin and TAU
profiling:

``` bash
clang -fplugin=/path/to/TAU_Profiling.so              \
  -mllvm -tau-input-file=./functions_C.txt            \
  -ldl -L/path/to/TAU/and/archi/$TAU_MAKEFILE -l TAU  \
  -Wl,-rpath,/path/to/TAU/and/archi/$TAU_MAKEFILE     \
  -o example example.c
```

Linking against \`libdl\` is required for TAU. Specifying the path for
dynamic linking also appears to be necessary.

The process is similar for the example C++ program:

``` bash
clang -fplugin=/path/to/TAU_Profiling_CXX.so        \
  -mllvm -tau-input-file=./functions_CXX.txt        \
  -ldl -L/path/to/TAU/and/archi/$TAU_MAKEFILE -lTAU \
  -Wl,-rpath,/path/to/TAU/and/archi/$TAU_MAKEFILE   \
  -o example example.cc
```

In the `sandbox/mm` directory, an example of a C++ program using several
source files is given and can be compiled using:

``` bash
clang -O3 -g -fplugin=/path/to/TAU_Profiling_CXX.so   \ 
  -mllvm -tau-input-file=./functions_CXX_mm.txt -ldl  \
  -L/path/to/TAU/and/archi/lib/$TAU_MAKEFILE -lTAU    \
  -Wl,-rpath,/path/to/TAU/and/archi/lib/$TAU_MAKEFILE \
  matmult.cpp matmult_initialize.cpp -o mm_cpp
```
Running the resulting executable in either case should produce a
`profile.*` file.

## Input file syntax

The input file contains function names, one on each line. For C programs,
use the function name; for C++ programs, use the whole prototype.

### Functions to instrument

Examples are given in `sandbox` directory.

``` bash
$ cat sandbox/functions_C.txt 
BEGIN_INCLUDE_LIST
hw
END_INCLUDE_LIST
$ cat sandbox/functions_CXX.txt 
BEGIN_INCLUDE_LIST
main()
A::foo()
END_INCLUDE_LIST
```

The input file for C++ programs must provide the arguments' datatypes. 

``` bash
$ cat sandbox/mm/functions_CXX_mm.txt 
BEGIN_INCLUDE_LIST
initialize(double**, int, int)
compute_interchange(double**, double**, double**, int, int, int)
END_INCLUDE_LIST
```

Templates work with the type or non-type parameter specified:

``` bash
void householder<double>(int, int, double**, double**, double**)
void householder<float>(int, int, float**, float**, float**)
```

### Regular expressions

The module provides two ways of passing function names as regular expressions.

#### As command line arguments

The options `-tau-regex` and `-tau-iregex` can be used to pass case-sensitive
and case-insensitive ECMAScript Regular Expressions on the command line.
For example:

``` bash
clang -fplugin=/path/to/TAU_Profiling.so -mllvm -tau-regex="apply*"  \
  -ldl -L/path/to/TAU/and/archi/$TAU_MAKEFILE -lTAU                  \
  -Wl,-rpath,/path/to/TAU/and/archi/$TAU_MAKEFILE                    \
  -O3 -g ./householder.c -o householder -lm
```

#### In the input file

In the input file, regular expressions use the Kleene star (\#) as
the wildcard, since the star (*) already means something in function names.
For instance, using the example code given in `sandbox/hh`, we can instrument
all the functions starting with `apply` (ie, `applyQ` and `applyR`) using:

``` 
apply#
```

### Exclude functions from the instrumentation

Functions can be explicitely excluded from the instrumentation. The function names
are given between the tag `BEGIN_EXCLUDE_LIST` and the tag `END_EXCLUDE_LIST`.
For instance, using the example code given in `sandbox/hh`, we can exclude the function 
`check` using:

``` 
BEGIN_EXCLUDE_LIST
check
END_EXCLUDE_LIST
```

Regular expressions work on excluded functions too. For instance, we can exclude
all the function whose names start with `check` using:

``` 
BEGIN_EXCLUDE_LIST
check#
END_EXCLUDE_LIST
```

Wildcards also work for template types:

``` 
void applyQ<#>(int, #**, #*, #, int)
```

### Include or exclude files from the instrumentation

Similarly, some files can be included or excluded from the instrumentation. Regular
expressions can also be used, using '*' to match a sequence of characters and '?' to match
a given character. The syntax is:

``` 
BEGIN_FILE_INCLUDE_LIST
file1.c
bar*.h
END_FILE_INCLUDE_LIST
BEGIN_FILE_EXCLUDE_LIST
file4.c
foo?.h
END_FILE_EXCLUDE_LIST
```

An example for this is given in `sandbox/hh2`. The input file requests the instrumentation 
of `householder`, `matmul` and all the functions starting with `apply` (`apply#`). There is 
an `applyR` function in `R.c` and an `applyQ` function in `Q.c`. However, we are excluding 
`Q.c` from the instrumentation. We should see `householder`, `matmul` and `applyR` in the
output and not `applyQ` using the following compilation command:

``` bash
clang -fplugin=/path/to/TAU_Profiling.so              \
  -mllvm -tau-input-file=functions_hh.txt             \
  -ldl -L/path/to/TAU/and/archi/$TAU_MAKEFILE -l TAU  \
  -Wl,-rpath,/path/to/TAU/and/archi/$TAU_MAKEFILE     \
  -o householder3 householder3.c matmul.c Q.c R.c -lm
```

### Functions in header files

Functions defined in header files can be included or excluded by file name using the -g option
in the compilation command. Otherwise, they will appear to be defined in the .c or .cpp file(s)
that include(s) them.

## <span class="todo TODO">TODO</span> 

  - Write something to spit out the names of known called functions,
    demangled if possible/necessary. This will help the user know
    exactly what name of the function to use to make sure it's
    instrumented.
  - Look into regexes, maybe? Having to write the fully-qualified name
    of all the functions requiring instrumentation sounds tedious and
    error-prone.
  - Give better output about what is being instrumented.

## TOTHINK

### Where to insert calls

Profiling function calls are currently inserted around call sites. But
they could be inserted at function entry and exit (or it could be a
plugin parameter).

1.  Entry/Exit Pros
    
      - If I were doing it manually, that's what I'd do.
      - Presumably less noise in the IR, if ever inspected.
      - Can produce an instrumented library that just needs to be linked
        properly. This would be particularly useful for profiling across
        several apps using the same library.

2.  Entry/Exit Cons
    
      - Can't profile library calls (I think?) if all I have is the
        `.so` or `.a`, which may be a more realistic use-case.
      - Without better knowledge of IR function structure, it's not
        clear whether preserving semantics (esp. at function exit) is
        difficult.

## References

  - [Writing an LLVM Pass](http://llvm.org/docs/WritingAnLLVMPass.html)
  - [Adrian Sampson's LLVM pass guide
    (2015)](https://www.cs.cornell.edu/~asampson/blog/llvm.html)
