all: directories tarbrowser analyze_archive prepareDatabase

directories: 
	mkdir -p build

tarbrowser: list_xzfile.o common_functions.o bzip2map
	gcc -Wall tarbrowser.c build/list_xzfile.o build/common_functions.o bzip_seek/bzip-table.o bzip_seek/micro-bunzip.o bzip_seek/seek-bunzip.o -llzma `pkg-config fuse --cflags --libs` `mysql_config --cflags --libs` -o tarbrowser

analyze_archive: list_xzfile.o bzip2map analyze_archive.o analyze_tar.o analyze_bz2.o analyze_xz.o common_functions.o
	gcc -o analyze_archive build/analyze_archive.o build/analyze_tar.o build/analyze_bz2.o build/analyze_xz.o build/list_xzfile.o build/common_functions.o bzip_seek/bzip-table.o bzip_seek/micro-bunzip.o bzip_seek/seek-bunzip.o -llzma `mysql_config --libs`

#produces necessary utilities from other code sources
# bzip-table.o, micro-bunzip.o, seek-bunzip.o, xz-list.o
bzip2map:
	make bzip-table.o micro-bunzip.o seek-bunzip.o -C bzip_seek

prepareDatabase: prepareDatabase.o
	gcc -o prepareDatabase build/prepareDatabase.o `mysql_config --libs`

########### important object files #########################

common_functions.o: common_functions.c common_functions.h
	gcc -c common_functions.c -o build/common_functions.o

analyze_archive.o: analyze_archive.c common_functions.h
	gcc -c analyze_archive.c -o build/analyze_archive.o

analyze_xz.o: analyze_xz.c common_functions.h
	gcc -c `mysql_config --cflags` -liblzma analyze_xz.c -o build/analyze_xz.o

analyze_bz2.o: analyze_bz2.c common_functions.h
	gcc -c `mysql_config --cflags` analyze_bz2.c -o build/analyze_bz2.o

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c `mysql_config --cflags` analyze_tar.c -o build/analyze_tar.o

prepareDatabase.o: prepareDatabase.c
	gcc -c `mysql_config --cflags` prepareDatabase.c -o build/prepareDatabase.o 

list_xzfile.o: list_xzfile.c list_xzfile.h
	gcc -c list_xzfile.c -llzma -o build/list_xzfile.o

clean:
	rm -f *.o tarbrowser analyze_archive prepareDatabase build/* temp/*
	make clean -C bzip_seek

######## below are temporary things that will be removed ################################

extract_tar_member: extract_tar_member.o common_functions.o
	gcc -o build/extract_tar_member build/extract_tar_member.o build/common_functions.o `mysql_config --libs`

extract_bz2_member: bzip2map extract_bz2_member.o common_functions.o
	gcc -o build/extract_bz2_member build/extract_bz2_member.o build/common_functions.o bzip_seek/micro-bunzip.o bzip_seek/seek-bunzip.o `mysql_config --libs`

extract_tar_member.o: extract_tar_member.c common_functions.h
	gcc -c `mysql_config --cflags` extract_tar_member.c -o build/extract_tar_member.o

extract_bz2_member.o: extract_bz2_member.c common_functions.h
	gcc -c `mysql_config --cflags` extract_bz2_member.c -o build/extract_bz2_member.o

read_tar.o: read_tar.c common_functions.h
	gcc -c `mysql_config --cflags` read_tar.c -o build/read_tar.o

read_tar: read_tar.o common_functions.o
	gcc -o build/read_tar build/read_tar.o build/common_functions.o `mysql_config --libs`

