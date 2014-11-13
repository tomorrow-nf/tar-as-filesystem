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

	//TODO stuff here

	return 0;
}
