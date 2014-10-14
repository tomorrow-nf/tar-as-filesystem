/*
    Will accept a tar.bz2 file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "bzip_seek/bitmapstructs.h"
#include "common_functions.h"

void* getblock(char* filename, int blocknum, struct blockmap* offsets) {
	
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

int analyze_bz2(char* f_name) {

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

	// Information for BZ2 archive and member headers
	char membername[MEMBERNAMESIZE];		// name of member file
	char file_length_string[FILELENGTHFIELDSIZE];	// size of file in bytes (octal string)
	long long int file_length;			// size of file in bytes (long int)
	char trashbuffer[200];				// for unused fields
	char link_flag;					// flag indicating this is a file link
	char linkname[MEMBERNAMESIZE];			// name of linked file
	char ustarflag[USTARFIELDSIZE];			// field indicating newer ustar format
	char memberprefix[PREFIXSIZE];			// ustar includes a field for long names

	long int blocksize; // bz2 blocksize (inteval of 100 from 100kB to 900kB)
	int bzerror; // bz2 error checking
	int blocknumber = 0; // block number being read
	int numblocks = 1; // number of blocks data exists in
	int datafromarchivecheck = 0; //if archivecheck had to backtrack
	long int tmp_dataread;

	struct blockmap* block_offsets = (struct blockmap*) malloc(sizeof(struct blockmap));
	block_offsets->blocklocations = (struct blocklocation*) malloc(sizeof(struct blocklocation) * 200);
	block_offsets->maxsize = 200;

	int M_result = map_bzip2(tar_filename, block_offsets);
        if(M_result != 0) {
		free(block_offsets->blocklocations);
		free(block_offsets);
		return 1;
	}

	
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
		free(block_offsets->blocklocations);
		free(block_offsets);
		return 1;
	}

	// begin transaction and check if this archive exists
	char insQuery[1000]; // insertion query buffer (we dont want current timestamp, we want the file's last modified timestamp)
	if(mysql_query(con, "START TRANSACTION")) {
		printf("Start Transaction error:\n%s\n", mysql_error(con));
		mysql_close(con);
		free(block_offsets->blocklocations);
		free(block_offsets);
		return 1;
	}

	//check if file already exists and ask for permission to overwrite and remove references
	sprintf(insQuery, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", real_filename);
	mysql_query(con, insQuery);
	MYSQL_RES* result = mysql_store_result(con);
	if(mysql_num_rows(result) == 0) {
		printf("File does not exist\n");
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
			free(block_offsets->blocklocations);
			free(block_offsets);
			return 1;
		}
		else {
			sprintf(insQuery, "DELETE FROM ArchiveList WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
			sprintf(insQuery, "DELETE FROM Bzip2_files WHERE ArchiveName = '%s'", real_filename);
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

	// Get a block as a char pointer for easy byte incrementing
	blocknumber++;
	char* memblock = (char*) getblock(tar_filename, blocknumber, block_offsets);
	if(memblock == NULL) {
		mysql_close(con);
		free(block_offsets->blocklocations);
		free(block_offsets);
		printf("getting first block failed\n");
		return 1;
	}
	long int remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
	long int blockposition = 0;
	int firstrun = 1;

	while (1){
		// TAR HEADER SECTION - get tar header
		char tmp_header_buffer[512];
			
		if(firstrun) {
			if(remainingdata >= 512){
				memcpy((void*) tmp_header_buffer, (memblock + blockposition), (sizeof(char) * 512));
				remainingdata = remainingdata - 512;
				blockposition = blockposition + 512;
			}
			else {
				tmp_dataread = remainingdata;
				memcpy((void*) tmp_header_buffer, (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				blocknumber++;
				memblock = (char*) getblock(tar_filename, blocknumber, block_offsets);
				if(memblock == NULL) {
					mysql_close(con);
					free(block_offsets->blocklocations);
					free(block_offsets);
					return 1;
				}
				remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
				blockposition = 0;

				memcpy((void*)(tmp_header_buffer + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
				remainingdata = remainingdata - (512 - tmp_dataread);
				blockposition = blockposition + (512 - tmp_dataread);
			}
		}
		else {
			//we are guarenteed to have the first 512 bytes of archive_end_check contain header data
			memcpy((void*) tmp_header_buffer, archive_end_check, (sizeof(char) * 512));
		}

		firstrun = 0;

		// TAR HEADER SECTION - extract data from tmp_header_buffer
		int tmp_header_buffer_position = 0;
		//get filename
		memcpy((void*)membername, (tmp_header_buffer + tmp_header_buffer_position), MEMBERNAMESIZE);
		printf("member name: %s\n", membername);
		tmp_header_buffer_position = tmp_header_buffer_position + MEMBERNAMESIZE;

		//discard mode, uid, and gid (8 bytes each)
		tmp_header_buffer_position = tmp_header_buffer_position + (sizeof(char) * 24);

		//get length of file in bytes
		memcpy((void*)file_length_string, (tmp_header_buffer + tmp_header_buffer_position), FILELENGTHFIELDSIZE);
		printf("file length (string): %s\n", file_length_string);
		file_length = strtoll(file_length_string, NULL, 8);
		printf("file length (int): %lld\n", file_length);
		tmp_header_buffer_position = tmp_header_buffer_position + FILELENGTHFIELDSIZE;

		//discard modify time and checksum (20 bytes)
		tmp_header_buffer_position = tmp_header_buffer_position + (sizeof(char) * 20);

		//get link flag (1 byte)
		memcpy((void*)(&link_flag), (tmp_header_buffer + tmp_header_buffer_position), sizeof(char));
		printf("link flag: %c\n", link_flag);
		tmp_header_buffer_position = tmp_header_buffer_position + sizeof(char);

		//get linked filename (if flag set, otherwise this field is useless)
		memcpy((void*)linkname, (tmp_header_buffer + tmp_header_buffer_position), MEMBERNAMESIZE);
		printf("link name: %s\n", linkname);
		tmp_header_buffer_position = tmp_header_buffer_position + MEMBERNAMESIZE;

		//get ustar flag and version, ignore version in check
		memcpy((void*)ustarflag, (tmp_header_buffer + tmp_header_buffer_position), USTARFIELDSIZE);
		printf("ustar flag: %s\n", ustarflag);
		tmp_header_buffer_position = tmp_header_buffer_position + USTARFIELDSIZE;

		// if flag is ustar get rest of fields, else we done
		if(strncmp(ustarflag, "ustar", 5) == 0) {
			//discard ustar data (80 bytes)
			tmp_header_buffer_position = tmp_header_buffer_position + (sizeof(char) * 80);

			//get ustar file prefix (may be nothing but /0)
			memcpy((void*)memberprefix, (tmp_header_buffer + tmp_header_buffer_position), PREFIXSIZE);
			printf("file prefix: %s\n", memberprefix);
		}


		// SKIP DATA SECTION - note the offset and skip the data
		//	-reminder: 	remainingdata: how much data is left in the block
		//			blockposition: essentially our position in the block

		int tmp_blocknumber = blocknumber; // temp to hold original blocknumber if data is in multiple blocks
		long int tmp_blockposition = blockposition; //temp to hold original block position if data is in multiple blocks
		numblocks = 1;

		// read longtmp = block adjusted file length
		if((file_length % 512) != 0) {
			longtmp = file_length + (512 - (file_length % 512));
		}
		else {
			longtmp = file_length;
		}

		printf("data begins in block %d, %ld bytes in\n", tmp_blocknumber, tmp_blockposition);
		if(file_length == 0) {
			// do nothing
		}
		else if(remainingdata >= longtmp) {
			remainingdata = remainingdata - longtmp;
			blockposition = blockposition + longtmp;
		}
		else {
			long long int file_data_remaining = longtmp;
			file_data_remaining = file_data_remaining - remainingdata;
			free(memblock);
			while(1) {
				blocknumber++;
				memblock = (char*) getblock(tar_filename, blocknumber, block_offsets);
				numblocks++;
				int sizeoftheblock = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;

				if(file_data_remaining == sizeoftheblock) {
					file_data_remaining = 0;
					remainingdata = 0;
					blockposition = 0;
					break;
				}
				else if(file_data_remaining < sizeoftheblock) {
					remainingdata = sizeoftheblock - file_data_remaining;
					blockposition = file_data_remaining;
					file_data_remaining = 0;
					break;
				}
				else {
					file_data_remaining = file_data_remaining - sizeoftheblock;
					free(memblock);
				}
			}
			
		}
		printf("file exists in %d blocks\n", numblocks);

		// Build the query and submit it
		sprintf(insQuery, "INSERT INTO Bzip2_files VALUES ('%s', '%s', %d, %llu, %ld, '%s', '%c')", real_filename, membername, tmp_blocknumber, ((block_offsets->blocklocations)[tmp_blocknumber]).position, tmp_blockposition, file_length_string, link_flag);
		if(mysql_query(con, insQuery)) {
			printf("Insert error:\n%s\n", mysql_error(con));
			printf("%s\n", insQuery);
			dberror = 1;
		}
			
		//end printed info with newline
		printf("\n");

		// TEST ARCHIVE END SECTION - test for the end of the archive
		//	-reminder: 	remainingdata: how much data is left in the block
		//			blockposition: essentially our position in the block
		tmp_dataread = 0;
		if(remainingdata >= 1024){
			memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * 1024));
			remainingdata = remainingdata - 1024;
			blockposition = blockposition + 1024;
		}
		else {
			tmp_dataread = remainingdata;
			memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * remainingdata));
			free(memblock);

			blocknumber++;
			memblock = (char*) getblock(tar_filename, blocknumber, block_offsets);
			if(memblock == NULL) {
				mysql_close(con);
				free(block_offsets->blocklocations);
				free(block_offsets);
				return 1;
			}
			remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
			blockposition = 0;

			memcpy((void*)(((char*)archive_end_check) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (1024 - tmp_dataread)));
			remainingdata = remainingdata - (1024 - tmp_dataread);
			blockposition = blockposition + (1024 - tmp_dataread);
		}

		if(memcmp(archive_end_check, archive_end, sizeof(archive_end)) == 0) {
			break; //1024 bytes of zeros mark end of archive
		}
		else {
			remainingdata = remainingdata + (512); //move back 512 bytes
			blockposition = blockposition - (512); // tar header is now in archive_end_check
		}
			
	}

	free(memblock);

	//the file has been read, commit the transation and close the connection
	if(dberror == 1) {
		if(mysql_query(con, "ROLLBACK")) {
			printf("Rollback error:\n%s\n", mysql_error(con));
		}
		else {
			printf("Entries rolled back\n");
		}
	}
	else {
		if(mysql_query(con, "COMMIT")) {
			printf("Commit error:\n%s\n", mysql_error(con));
		}
		else {
			printf("Entries committed\n");
		}
	}


	mysql_close(con);
	free(block_offsets->blocklocations);
	free(block_offsets);
	return 0;
}
