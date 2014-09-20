all: analyze_tar extract_tar_member

analyze_tar: analyze_tar.o common_functions.o
	gcc build/analyze_tar.o build/common_functions.o -o build/analyze_tar

extract_tar_member: extract_tar_member.o common_functions.o
	gcc build/extract_tar_member.o build/common_functions.o -o build/extract_tar_member

common_functions.o: common_functions.c common_functions.h
	gcc -c common_functions.c -o build/common_functions.o

extract_tar_member.o: extract_tar_member.c common_functions.h
	gcc -c extract_tar_member.c -o build/extract_tar_member.o

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c analyze_tar.c -o build/analyze_tar.o

clean:
	rm -f *.o  build/* temp/*
