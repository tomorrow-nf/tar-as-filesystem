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

	if (bzerror == BZ_OK || bzerror == BZ_STREAM_END){
		return blockbuf;
	}
	else {
		if(blockbuf != NULL) {
			free(blockbuf);
		}
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
			default:
				printf("ERROR: bz2 error\n");
				return NULL;
		}
	}
}

int analyze_bz2(char* f_name) {

	FILE* tarfile;   // for checking header
	BZFILE* bz2file; // for evaluating file. These are both the same archive

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

	// Open the archive for evaluation
	tarfile = fopen(tar_filename, "r");
	if(!tarfile) {
		printf("Unable to open file: %s\n", tar_filename);
	}
	else {
		// Evaluate the bz2 header. All we care about is the blocksize (4th byte)
		fseek(tarfile, 3, SEEK_CUR);
		char blockbuffer[2]; // two byte buffer
		fread(&(blockbuffer[0]), sizeof(char), 1, tarfile);
		blockbuffer[1] = '\0';
		blocksize = atoi(blockbuffer) * 100000; // The value in the header is 1...9. We need that in bytes (number * 100000)
		printf("BZ2 Block size: %ld bytes\n", blocksize);
		fclose(tarfile); //close the file

		// begin transaction and check if this archive exists
		char insQuery[1000]; // insertion query buffer (we dont want current timestamp, we want the file's last modified timestamp)
		if(mysql_query(con, "START TRANSACTION")) {
			printf("Start Transaction error:\n%s\n", mysql_error(con));
			mysql_close(con);
			return 1;
		}

		//check if file already exists and ask for permission to overwrite and remove references
		sprintf(insQuery, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", real_filename);
		mysql_query(con, insQuery);
		MYSQL_RES* result = mysql_store_result(con);
		if(mysql_num_rows(result) == 0) {
			printf("File does not exist");
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
				//TODO CHANGE TABLE NAME
				//sprintf(insQuery, "DELETE FROM UncompTar WHERE ArchiveName = '%s'", real_filename);
				//if(mysql_query(con, insQuery)) {
				//	printf("Delete error:\n%s\n", mysql_error(con));
				//	dberror = 1;
				//}
			}
		}
		
		// file is not in database or it has been cleared from database
		sprintf(insQuery, "INSERT INTO ArchiveList VALUES ('%s', '%s', 'timestamp')", real_filename, fullpath);
		if(mysql_query(con, insQuery)) {
			printf("Insert error:\n%s\n", mysql_error(con));
			dberror = 1;
		}

		// Open the file as a bz2 file so that it can be decompressed
		tarfile = fopen(tar_filename, "r");
		bz2file = BZ2_bzReadOpen(&bzerror, tarfile, 0, 0, NULL, 0);
		if(bzerror != BZ_OK) {
			BZ2_bzReadClose(&bzerror, bz2file);
			fclose(tarfile);
			mysql_close(con);
			return 1;
		}

		// Get a block as a char pointer for easy byte incrementing
		char* memblock = (char*) getblock(bz2file, blocksize);
		blocknumber++;
		if(memblock == NULL) {
			BZ2_bzReadClose(&bzerror, bz2file);
			fclose(tarfile);
			mysql_close(con);
			return 1;
		}
		long int remainingdata = blocksize;
		long int blockposition = 0;

		while (1){
			// TAR HEADER SECTION - get tar header
			char tmp_header_buffer[512];
			if(remainingdata >= 512){
				memcpy((void*) tmp_header_buffer, (memblock + blockposition), (sizeof(char) * 512));
				remainingdata = remainingdata - 512;
				blockposition = blockposition + 512;
			}
			else {
				long int tmp_dataread = remainingdata;
				memcpy((void*) tmp_header_buffer, (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				memblock = (char*) getblock(bz2file, blocksize);
				blocknumber++;
				if(memblock == NULL) {
					BZ2_bzReadClose(&bzerror, bz2file);
					fclose(tarfile);
					mysql_close(con);
					return 1;
				}
				remainingdata = blocksize;
				blockposition = 0;

				memcpy((void*)(tmp_header_buffer + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
				remainingdata = remainingdata - (512 - tmp_dataread);
				blockposition = blockposition + (512 - tmp_dataread);
			}
			
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

			printf("data begins in block %d, %ld bytes in\n", tmp_blocknumber, tmp_blockposition);
			if(file_length == 0) {
				// do nothing
			}
			else if(remainingdata >= file_length) {
				remainingdata = remainingdata - file_length;
				blockposition = blockposition + file_length;
			}
			else {
				long long int file_data_remaining = file_length;
				file_data_remaining = file_data_remaining - remainingdata;
				free(memblock);
				while(file_data_remaining >= blocksize) {
					memblock = (char*) getblock(bz2file, blocksize);
					numblocks++;
					blocknumber++;
					file_data_remaining = file_data_remaining - blocksize;
					free(memblock);
				}

				if(file_data_remaining != 0) {
					memblock = (char*) getblock(bz2file, blocksize);
					remainingdata = blocksize - file_data_remaining;
					blockposition = file_data_remaining;
				}
				else {
					remainingdata = 0;
					blockposition = 0;
				}
			}
			printf("file exists in %d blocks\n", numblocks);

			//TODO add to sql database
			
			//end printed info with newline
			printf("\n");

			// TEST ARCHIVE END SECTION - test for the end of the archive
			//	-reminder: 	remainingdata: how much data is left in the block
			//			blockposition: essentially our position in the block
			int tmp_dataread = 0;
			if(remainingdata >= 1024){
				memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * 1024));
				remainingdata = remainingdata - 1024;
				blockposition = blockposition + 1024;
			}
			else {
				tmp_dataread = remainingdata;
				memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				memblock = (char*) getblock(bz2file, blocksize);
				blocknumber++;
				if(memblock == NULL) {
					BZ2_bzReadClose(&bzerror, bz2file);
					fclose(tarfile);
					mysql_close(con);
					return 1;
				}
				remainingdata = blocksize;
				blockposition = 0;

				memcpy((void*)(((char*)archive_end_check) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (1024 - tmp_dataread)));
				remainingdata = remainingdata - (1024 - tmp_dataread);
				blockposition = blockposition + (1024 - tmp_dataread);
			}

			if(memcmp(archive_end_check, archive_end, sizeof(archive_end)) == 0) {
				break; //1024 bytes of zeros mark end of archive
			}
			else {
				remainingdata = remainingdata + (1024 - tmp_dataread);
				blockposition = blockposition - (1024 - tmp_dataread); //move back 1024 bytes
			}
		}

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

		free(memblock);
		BZ2_bzReadClose(&bzerror, bz2file);
		fclose(tarfile);
	}

	mysql_close(con);
	return 0;
}
