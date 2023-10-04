#!/bin/bash

#rm mylib.o
#rm Testcases/test*.o

gcc -c mylib.c
gcc -c Testcases/test1.c -o Testcases/test1.o
gcc -o Testcases/test1 Testcases/test1.o mylib.o


gcc -c Testcases/test2.c -o Testcases/test2.o
gcc -o Testcases/test2 Testcases/test2.o mylib.o

gcc -c Testcases/test3.c -o Testcases/test3.o
gcc -o Testcases/test3 Testcases/test3.o mylib.o

gcc -c Testcases/test4.c -o Testcases/test4.o
gcc -o Testcases/test4 Testcases/test4.o mylib.o

gcc -c Testcases/test5.c -o Testcases/test5.o
gcc -o Testcases/test5 Testcases/test5.o mylib.o

gcc -c Testcases/test6.c -o Testcases/test6.o
gcc -o Testcases/test6 Testcases/test6.o mylib.o

gcc -c Testcases/test7.c -o Testcases/test7.o
gcc -o Testcases/test7 Testcases/test7.o mylib.o

gcc -c Testcases/test8.c -o Testcases/test8.o
gcc -o Testcases/test8 Testcases/test8.o mylib.o

gcc -c Testcases/test9.c -o Testcases/test9.o
gcc -o Testcases/test9 Testcases/test9.o mylib.o

gcc -c Testcases/test10.c -o Testcases/test10.o
gcc -o Testcases/test10 Testcases/test10.o mylib.o

gcc -c Testcases/test11.c -o Testcases/test11.o
gcc -o Testcases/test11 Testcases/test11.o mylib.o

gcc -c Testcases/test12.c -o Testcases/test12.o
gcc -o Testcases/test12 Testcases/test12.o mylib.o

gcc -c Testcases/test13.c -o Testcases/test13.o
gcc -o Testcases/test13 Testcases/test13.o mylib.o

gcc -c Testcases/test14.c -o Testcases/test14.o
gcc -o Testcases/test14 Testcases/test14.o mylib.o

gcc -c Testcases/test15.c -o Testcases/test15.o
gcc -o Testcases/test15 Testcases/test15.o mylib.o
