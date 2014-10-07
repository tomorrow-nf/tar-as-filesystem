all: analyze_archive extract_tar_member prepareDatabase

analyze_archive: bzip2map analyze_archive.o analyze_tar.o analyze_bz2.o common_functions.o
	gcc -o build/analyze_archive build/analyze_archive.o build/analyze_tar.o build/analyze_bz2.o build/common_functions.o `mysql_config --libs` -L. -lbz2

bzip2map:
	make map -C bzip2_map
	mv bzip2_map/libbz2.a libbz2.a

extract_tar_member: extract_tar_member.o common_functions.o
	gcc -o build/extract_tar_member build/extract_tar_member.o build/common_functions.o `mysql_config --libs`

prepareDatabase: prepareDatabase.o
	gcc -o build/prepareDatabase build/prepareDatabase.o `mysql_config --libs`

common_functions.o: common_functions.c common_functions.h
	gcc -c common_functions.c -o build/common_functions.o

extract_tar_member.o: extract_tar_member.c common_functions.h
	gcc -c `mysql_config --cflags` extract_tar_member.c -o build/extract_tar_member.o

analyze_archive.o: analyze_archive.c common_functions.h
	gcc -c analyze_archive.c -o build/analyze_archive.o

analyze_bz2.o: analyze_bz2.c common_functions.h
	gcc -c `mysql_config --cflags` analyze_bz2.c -lbz2 -o build/analyze_bz2.o

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c `mysql_config --cflags` analyze_tar.c -o build/analyze_tar.o

prepareDatabase.o: prepareDatabase.c
	gcc -c `mysql_config --cflags` prepareDatabase.c -o build/prepareDatabase.o

clean:
	rm -f *.o  build/* temp/* libbz2.a
	make clean -C bzip2_map
