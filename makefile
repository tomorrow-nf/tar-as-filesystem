analyze_tar: analyze_tar.o common_functions.o
	gcc analyze_tar.o common_functions.o -o analyze_tar

common_functions.o: common_functions.c common_functions.h
	gcc -c common_functions.c

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c analyze_tar.c

clean:
	rm -f *.o analyze_tar
