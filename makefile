analyze_tar: analyze_tar.o common_functions.o
	gcc build/analyze_tar.o build/common_functions.o -o build/analyze_tar

common_functions.o: common_functions.c common_functions.h
	gcc -c common_functions.c -o build/common_functions.o

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c analyze_tar.c -o build/analyze_tar.o

clean:
	rm -f *.o analyze_tar build/*.o build/analyze_tar
