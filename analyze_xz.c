/*
    Will accept a tar.xz file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <lzma.h>
#include "bzip_seek/bitmapstructs.h" //this is just for the blockmap struct
#include "common_functions.h"

// Struct for handling XZ streams. Blocks, indexes, etc. are
// handled by liblzma structs
// Stream format from official documentation:
/*
+-+-+-+-+-+-+-+-+-+-+-+-+=======+=======+     +=======+=======+-+-+-+-+-+-+-+-+-+-+-+-+
|     Stream Header     | Block | Block | ... | Block | Index |     Stream Footer     |
+-+-+-+-+-+-+-+-+-+-+-+-+=======+=======+     +=======+=======+-+-+-+-+-+-+-+-+-+-+-+-+
*/
struct xz_stream {
	uint8_t header_magic[6];
	uint8_t footer_magic[2];

	int stored_crc32;
	int calculated_crc32;

	//lzma_stream_flags flags; // Included in both header and footer

	int stored_backward_size;
	int real_backward_size;

	lzma_index_iter xzindex;

	lzma_block* block;
	struct blockmap xzblockmap;
};


// Multibyte int handling, from
// http://tukaani.org/xz/xz-file-format.txt
size_t encode(uint8_t buf[static 9], uint64_t num){
	if (num > UINT64_MAX / 2)
		return 0;

	size_t i = 0;

	while (num >= 0x80) {
		buf[i++] = (uint8_t)(num) | 0x80;
		num >>= 7;
	}

	buf[i++] = (uint8_t)(num);

	return i;
}

size_t decode(const uint8_t buf[], size_t size_max, uint64_t *num){
	if (size_max == 0)
		return 0;

	if (size_max > 9)
		size_max = 9;

	*num = buf[0] & 0x7F;
	size_t i = 0;

	while (buf[i++] & 0x80) {
		if (i >= size_max || buf[i] == 0x00)
			return 0;

		*num |= (uint64_t)(buf[i] & 0x7F) << (i * 7);
	}

	return i;
}

// TODO: THIS HAS NOT BEEN MODIFIED YET. This is straight from BZ2
void* getblock_xz(char* filename, int blocknum, struct blockmap* offsets) {
	
	void* blockbuf = (char*) malloc(((offsets->blocklocations)[blocknum]).uncompressedSize); // Build a buffer to hold a single block

	// Read a single block into the buffer
	int err = uncompressblock( filename, ((offsets->blocklocations)[blocknum]).position,
						blockbuf );

	if (!err){
		return blockbuf;
	}
	else {
		return NULL;
	}
}


int analyze_xz(char* f_name) {

	long int bytes_read = 0; 		// total bytes read - (gigabytes read * bytes per gigabyte)
	char* tar_filename = f_name; 	// file to analyze
	char* real_filename;            // the filename without any directory info in front of it
	char* fullpath;             	// the absolute path to the file
	long int longtmp; 				// temporary variable for calculations
	long long int longlongtmp; 		// temporary variable for calculations
	int dberror = 0;                // indicate an error in analysis
	struct headerblock header;

	// End of archive check
	char archive_end_check[512];
	char archive_end[512];
	memset(archive_end, 0, sizeof(archive_end));

	// Information for XZ archive and member headers
	char membername[MEMBERNAMESIZE];		// name of member file
	long long int file_length;			// size of file in bytes (long int)
	char linkname[MEMBERNAMESIZE];			// name of linked file

	int blocknumber = 0; // block number being read
	int numblocks = 1; 	 // number of blocks data exists in
	int totalblocks = 0; // total number of blocks in the stream

	// get real filename
	real_filename = strrchr(tar_filename, '/');
	if (!real_filename) {
		real_filename = tar_filename;
	} 
	else {
		real_filename++;
	}
	fullpath = realpath(tar_filename, NULL);


	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	mysql_init(con);
	if(!mysql_real_connect(con, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(con));
		//exit, no point
		mysql_close(con);
		return 1;
	}

	// begin transaction and check if this archive exists
	char insQuery[1000]; // insertion query buffer (we dont want current timestamp, we want the file's last modified timestamp)
	if(mysql_query(con, "START TRANSACTION")) {
		printf("Start Transaction error:\n%s\n", mysql_error(con));
		mysql_close(con);
		return 1;
	}

	//check if file already exists and ask for permission to overwrite and remove references
	sprintf(insQuery, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", real_filename);
	if(mysql_query(con, insQuery)) {
		printf("Select error:\n%s\n", mysql_error(con));
		mysql_close(con);
		return 1;
	}
	MYSQL_RES* result = mysql_store_result(con);
	if(mysql_num_rows(result) == 0) {
		printf("File is not in database\n");
		//file foes not exist, do nothing
		mysql_free_result(result);
	}
	else {
		MYSQL_ROW row = mysql_fetch_row(result);
		mysql_free_result(result);
		char yes_no[20];
		sprintf(yes_no, "bad"); //prime with bad answer
		while(strcmp(yes_no, "y") && strcmp(yes_no, "Y") && strcmp(yes_no, "n") && strcmp(yes_no, "N")) {
			printf("File analysis already exists, overwrite[Y/N]: ");
			scanf("%s", yes_no);
		}
		// if N exit, if Y overwrite
		if(!strcmp(yes_no, "N") || !strcmp(yes_no, "n")) {
			if(mysql_query(con, "ROLLBACK")) {
				printf("Rollback error:\n%s\n", mysql_error(con));
			}
			mysql_close(con);
			return 1;
		}
		else {
			sprintf(insQuery, "DELETE FROM ArchiveList WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
			sprintf(insQuery, "DELETE FROM CompXZ WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
		}
	}
		
	// file is not in database or it has been cleared from database
	sprintf(insQuery, "INSERT INTO ArchiveList VALUES ('%s', '%s', 'timestamp')", real_filename, fullpath);
	if(mysql_query(con, insQuery)) {
		printf("Insert error:\n%s\n", mysql_error(con));
		dberror = 1;
	}

	// open the file
	FILE* xzfile = fopen(tar_filename, "r");
	if(!xzfile) {
		printf("Unable to open file: %s\n", tar_filename);
	}

	int streamflag = 1; // Set to 0 when there are no more streams to analyze
	int streampos = SEEK_END; // Initialize to the last stream
	int streamsize = 0; // size of next stream. Initialize to 0. Pulled from index.
	char posbuffer[5]; // Used to check for padding
	int i, j;
	// Expected magic bytes
	uint8_t exp_head_mag[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
	uint8_t exp_foot_mag[2] = { 'Y', 'Z' };

	struct xz_stream* stream; // the current stream

	while(streamflag == 1){
		// Skip to the end of the stream
		fseek(xzfile, streamsize, streampos-5);
		// Check for any null byte padding and adjust the current
		// position accordingly and seek to it.
		fread((void*) posbuffer, 1, 5, xzfile);
		for (i = 4, j = 0; i >= 0; i--, j++){
			if (posbuffer[i] != '\0'){
				streampos = streampos - j;
			}
		}
		fseek(xzfile, streamsize, streampos);
		// Parse the footer of the current stream
		//		Store the CRC32
		// TODO: CALCULATE OUR OWN AND COMPARE
		fread((void*)stream->stored_crc32, 4, 1, xzfile);
		//		Store the backward size (size of index)
		// TODO: Calculate size of the index and compare
		fread((void*)stream->stored_backward_size, 4, 1, xzfile);
		//		TODO: Store the stream flags
		//fread((void*)stream->flags, 2, 1, xzfile);
		// Skipping this for now. We shouldn't need them
		fseek(xzfile, 2, SEEK_CUR);
		//		Check whether magic bytes are valid
		fread((void*)stream->footer_magic, 1, 6, xzfile);
		for (i = 0; i < 1; i++){
			if (stream->footer_magic[i] != exp_foot_mag[i]){
				// TODO: errno
				printf("ERROR: Invalid XZ file, aborting\n");
				return 1;
			}			
		}

		// Parse the index
		// TODO: HOW DO

		// Skip data, then parse the header of the current stream
		// TODO: SKIP DATA

		//		Check whether magic bytes are valid
		fread((void*)stream->header_magic, 1, 6, xzfile);
		for (i = 0; i < 5; i++){
			if (stream->header_magic[i] != exp_head_mag[i]){
				// TODO: errno
				printf("ERROR: Invalid XZ file, aborting\n");
				return 1;
			}			
		}
		//		TODO: Store the stream flags
		//fread((void*)stream->flags, 2, 1, xzfile);
		// Skipping this for now. We shouldn't need them
		fseek(xzfile, 2, SEEK_CUR);

		//		Store the CRC32
		// TODO: CALCULATE OUR OWN AND COMPARE
		fread((void*)stream->stored_crc32, 4, 1, xzfile);



		streampos = SEEK_CUR;
	}

	// TODO: Memory stuff
	return 0;
}
