/*
    Will accept a tar.bz2 file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <bzlib.h>
#include "common_functions.h"

void* getblock(BZFILE* b, long int blocksize) {
	
	void* blockbuf = (char*) malloc(blocksize); // Build a buffer to hold a single block
	int bzerror; // bz2 error checking

    // Read a single block into the buffer
    BZ2_bzRead(&bzerror, b, blockbuf, blocksize);

    if (bzerror == BZ_OK){
    	return blockbuf;
    }
    else {
	    //check appropriate errors 
	    switch (bzerror) {
	    	case BZ_PARAM_ERROR:
	    		printf("ERROR: bz2 parameter error\n");
	    		return NULL;
	    	case BZ_SEQUENCE_ERROR:
	    		printf("ERROR: bz2 file was not opened read-only\n");
	    		return NULL;
	    	case BZ_IO_ERROR:
	    		printf("ERROR: Error reading from compressed file\n");
	    		return NULL;
	    	case BZ_UNEXPECTED_EOF:
	    		printf("ERROR: Unexpected end of file\n");
	    		return NULL;
	    	case BZ_DATA_ERROR:
	    		printf("ERROR: Data integrity error in compressed archive\n");
	    		return NULL;
	    	case BZ_DATA_ERROR_MAGIC:
	    		printf("ERROR: Data integrity error in compressed archive\n");
	    		return NULL;
	    	case BZ_MEM_ERROR:
	    		printf("ERROR: Insufficient memory available\n");
	    		return NULL;
	    	case BZ_STREAM_END:
	    		// TODO
	    		return NULL;
	    	default:
	    		printf("ERROR: bz2 error\n");
	    		return NULL;
	    }
    }
}

int analyze_bz2(char* f_name) {

	FILE* tarfile;   // for checking header
	BZFILE* bz2file; // for evaluating file. These are both the same archive

	int GB_read = 0;         		// number of gigabytes read so far
	long int bytes_read = 0; 		// total bytes read - (gigabytes read * bytes per gigabyte)
	char* tar_filename = f_name; 	// file to analyze
	char* real_filename;            // the filename without any directory info in front of it
	char* fullpath;             	// the absolute path to the file
	long int longtmp; 				// temporary variable for calculations
	long long int longlongtmp; 		// temporary variable for calculations
	int dberror = 0;                // indicate an error in analysis

	// End of archive check
	char archive_end_check[1024];
	char archive_end[1024];
	memset(archive_end, 0, sizeof(archive_end));

	char* tempsdf = (char*) malloc(90);

	// Information for BZ2 archive and member headers
	char* membername = (char*) malloc(MEMBERNAMESIZE);               // name of member file
	char* file_length_string = (char*) malloc(FILELENGTHFIELDSIZE);  // size of file in bytes (octal string)
	long long int file_length;                                       // size of file in bytes (long int)
	void* trashbuffer = (void*) malloc(sizeof(char) * 200);          // for unused fields
	char link_flag;                                                  // flag indicating this is a file link
	char* linkname = (char*) malloc(MEMBERNAMESIZE);                 // name of linked file
	char* ustarflag = (char*) malloc(USTARFIELDSIZE);                // field indicating newer ustar format
	char* memberprefix = (char*) malloc(PREFIXSIZE);                 // ustar includes a field for long names

	long int blocksize; // bz2 blocksize (inteval of 100 from 100kB to 900kB)
	int bzerror; // bz2 error checking

	
	// get real filename
	real_filename = strrchr(tar_filename, '/');
	if (!real_filename) {
		real_filename = tar_filename;
	} 
	else {
		real_filename++;
	}
	fullpath = realpath(tar_filename, NULL);

	// Open the archive for evaluation
	tarfile = fopen(tar_filename, "r");
	if(!tarfile) {
		printf("Unable to open file: %s\n", tar_filename);
	}
	else {
		// Evaluate the bz2 header. All we care about is the blocksize (4th byte)
		fseek(tarfile, 3, SEEK_CUR);
		char* blockbuffer = (char*) malloc(8); // single byte buffer
		fread(blockbuffer, 1, 1, tarfile);
		blocksize = atoi(blockbuffer) * 102400; // The value in the header is 1...9. We need that in bytes (* 100 * 1024)
		printf("BZ2 Block size: %ld bytes\n", blocksize);

		// Open the file as a bz2 file so that it can be decompressed
		bz2file = BZ2_bzopen(tar_filename, "r");

		while (1){
			// Get a block
			getblock(bz2file, blocksize);
		}

		return 0;
	}
}
