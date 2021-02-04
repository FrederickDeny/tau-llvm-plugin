#!/bin/bash

TAU_PLUGIN=$PWD/../../build/install/lib/TAU_Profiling.so

BBLUE='\033[1;34m'
NC='\033[0m'

echo -e "${BBLUE}With -g: function2 should be excluded because it is defined in test1.h${NC}"

clang -g -fplugin=$TAU_PLUGIN -mllvm -tau-input-file=./test.txt  -ldl -L${TAU}/lib/$TAU_MAKEFILE -lTAU -Wl,-rpath,${TAU}/lib/$TAU_MAKEFILE  -O3 -o test1 test1.c
tau_exec -T serial,clang ./test1 &> /dev/null
pprof

echo -e "${BBLUE}Without -g: function2 should be included because the compiler considers it comes from test1.c (which includes test1.h)${NC}"

clang -fplugin=$TAU_PLUGIN -mllvm -tau-input-file=./test.txt  -ldl -L${TAU}/lib/$TAU_MAKEFILE -lTAU -Wl,-rpath,${TAU}/lib/$TAU_MAKEFILE  -O3 -o test1 test1.c
tau_exec -T serial,clang ./test1 &> /dev/null
pprof

rm profile.*
