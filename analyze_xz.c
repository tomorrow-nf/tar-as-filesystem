/*
    Will accept a tar.xz file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "common_functions.h"

void* getblock_xz(char* filename, int blocknum) {
	
	return NULL;
}

int analyze_xz(char* f_name) {

	long int bytes_read = 0; 		// total bytes read - (gigabytes read * bytes per gigabyte)
	char* tar_filename = f_name; 	// file to analyze
	char* real_filename;            // the filename without any directory info in front of it
	char* fullpath;             	// the absolute path to the file
	long int longtmp; 				// temporary variable for calculations
	long long int longlongtmp; 		// temporary variable for calculations
	int dberror = 0;                // indicate an error in analysis

	// End of archive check
	char archive_end_check[512];
	char archive_end[512];
	memset(archive_end, 0, sizeof(archive_end));

	// Information for BZ2 archive and member headers
	char membername[MEMBERNAMESIZE];		// name of member file
	long long int file_length;			// size of file in bytes (long int)
	char linkname[MEMBERNAMESIZE];			// name of linked file

	int blocknumber = 0; // block number being read
	int numblocks = 1; // number of blocks data exists in
	int datafromarchivecheck = 0; //if archivecheck had to backtrack
	long int tmp_dataread;
	struct headerblock header;

	return 0;
}
