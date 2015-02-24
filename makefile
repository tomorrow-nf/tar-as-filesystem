all: directories tarbrowser analyze_archive prepareDatabase

directories: 
	mkdir -p build

tarbrowser: list_xzfile.o common_functions.o bzip2map
	gcc -Wall tarbrowser.c build/list_xzfile.o build/common_functions.o bzip_seek/bzip-table.o bzip_seek/micro-bunzip.o bzip_seek/seek-bunzip.o -llzma `getconf LFS_CFLAGS` `pkg-config fuse --cflags --libs` `mysql_config --cflags --libs` -o tarbrowser

analyze_archive: list_xzfile.o bzip2map analyze_archive.o analyze_tar.o analyze_bz2.o analyze_xz.o common_functions.o
	gcc -o analyze_archive build/analyze_archive.o build/analyze_tar.o build/analyze_bz2.o build/analyze_xz.o build/list_xzfile.o build/common_functions.o bzip_seek/bzip-table.o bzip_seek/micro-bunzip.o bzip_seek/seek-bunzip.o -llzma `getconf LFS_CFLAGS` `mysql_config --libs`

#produces necessary utilities from other code sources
# bzip-table.o, micro-bunzip.o, seek-bunzip.o, xz-list.o
bzip2map:
	make bzip-table.o micro-bunzip.o seek-bunzip.o -C bzip_seek

prepareDatabase: prepareDatabase.o
	gcc -o prepareDatabase build/prepareDatabase.o `mysql_config --libs`

########### important object files #########################

common_functions.o: common_functions.c common_functions.h
	gcc -c `getconf LFS_CFLAGS` common_functions.c -o build/common_functions.o

analyze_archive.o: analyze_archive.c common_functions.h
	gcc -c `getconf LFS_CFLAGS` analyze_archive.c -o build/analyze_archive.o

analyze_xz.o: analyze_xz.c common_functions.h
	gcc -c `getconf LFS_CFLAGS` `mysql_config --cflags` -liblzma analyze_xz.c -o build/analyze_xz.o

analyze_bz2.o: analyze_bz2.c common_functions.h
	gcc -c `getconf LFS_CFLAGS` `mysql_config --cflags` analyze_bz2.c -o build/analyze_bz2.o

analyze_tar.o: analyze_tar.c common_functions.h
	gcc -c `getconf LFS_CFLAGS` `mysql_config --cflags` analyze_tar.c -o build/analyze_tar.o

prepareDatabase.o: prepareDatabase.c
	gcc -c `getconf LFS_CFLAGS` `mysql_config --cflags` prepareDatabase.c -o build/prepareDatabase.o 

list_xzfile.o: list_xzfile.c list_xzfile.h
	gcc -c `getconf LFS_CFLAGS` list_xzfile.c -llzma -o build/list_xzfile.o

clean:
	rm -f *.o tarbrowser analyze_archive prepareDatabase build/* temp/*
	make clean -C bzip_seek
