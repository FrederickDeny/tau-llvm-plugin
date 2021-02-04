#include <stdio.h>
#include <stdlib.h>

#include "test1.h"

void function1(){
    printf( "Hello from function 1\n" );
}

int main(){
    function1();
    function2();
    return EXIT_SUCCESS;
}
