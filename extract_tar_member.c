/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common_functions.h"


int main(int argc, char* argv[]) {

	//TODO: MEMORY MANAGEMENT

	// Initial database connection
	MYSQL *con = mysql_init(NULL);
	mysql_init(con);
	printf("Connecting to database\n");
	if(!mysql_real_connect(con, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(con));
		//exit, no point
		mysql_close(con);
		return 1;
	}


	char* tar_filename = argv[1];   // file to extract member from
	// Set file extension
	// TODO: This is copied from analyze_tar, we can probably combine them later
	char* tar_file_handle = strrchr(tar_filename, '.');
	if (!tar_file_handle) {
		//TODO error if no extension given
		return 1;
	} 
	else {
		tar_file_handle = tar_file_handle + 1;
		// Save file extension. Validity will be checked later
	}
	
	char* tempdirectory = "temp/";
	char* membername = argv[2]; // member of archive to extract
	char* output = (char*) malloc(sizeof(char) * (strlen(membername) + strlen(tempdirectory) + 1)); //output location of file
	sprintf(output, "%s%s", tempdirectory, membername);
	
	// Member info to be queried from the database
	int gb_offset;
	long int b_offset;
	long long int mem_length;
	void* write_buf = (void*) malloc(BLOCKSIZE);
	char* fullpath; //full path location of archive

	// Initial declarations for the tar archive and output file
	FILE* tarfile;
	FILE* member;

	// Temporary values
	long long int longlongtmp;
	long int longtmp;
	char queryBuf[1000]; // query buffer

	// check if file exists
	printf("Checking if file exists\n");
	sprintf(queryBuf, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", tar_filename);
	mysql_query(con, queryBuf);
	MYSQL_RES* result = mysql_store_result(con);
	if(mysql_num_rows(result) == 0) {
		printf("The archive file does not exist\n");
		mysql_free_result(result);
		free(write_buf);
		free(output);
		mysql_close(con);
		return 1;
		
	}
	else {
		MYSQL_ROW row = mysql_fetch_row(result);
		fullpath = (char*) malloc(sizeof(char) * (strlen(row[1]) + 1));
		strcpy(fullpath, row[1]);
	}
	mysql_free_result(result);

	// Uncompressed tar archive
	if(strcmp(tar_file_handle, "tar") == 0) {
		tarfile = fopen(fullpath, "r");
		if(!tarfile) {
			printf("Unable to open file: %s\n", fullpath);
			free(write_buf);
			free(fullpath);
			free(output);
			mysql_close(con);
			return 1;
		}
		else {
			// Query the offsets and member file length from the database
			sprintf(queryBuf, "SELECT GBoffset from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
			mysql_query(con, queryBuf);
			result = mysql_store_result(con);
			MYSQL_ROW row = mysql_fetch_row(result);
			gb_offset = atoi(row[0]);
			mysql_free_result(result);

			sprintf(queryBuf, "SELECT BYTEoffset from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
			mysql_query(con, queryBuf);
			result = mysql_store_result(con);
			row = mysql_fetch_row(result);
			b_offset = strtol(row[0], NULL, 10);
			mysql_free_result(result);

			// Seek to the file's offset
			if(gb_offset != 0) {
				int i;
				for(i=1; i<=gb_offset; i++) {
					fseek(tarfile, BYTES_IN_GB, SEEK_CUR);
				}
			}
			fseek(tarfile, b_offset, SEEK_CUR);

			member = fopen(output, "w"); // Create a file to write to
			if(!member) {
				printf("Unable to create file: %s\n", output);
				free(write_buf);
				free(output);
				free(fullpath);
				mysql_close(con);
				return 1;
			}
			else {
				long long int bytes_read = 0;

				// Query the member file size
				printf(queryBuf, "SELECT MemberLength from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
				mysql_query(con, queryBuf);
				result = mysql_store_result(con);
				row = mysql_fetch_row(result);
				mem_length = strtoll(row[0], NULL, 8);
				mysql_free_result(result);

				// Copy the file by blocks
				printf("Copying the file\n");
				while (bytes_read < mem_length){
					if((mem_length - bytes_read) < BLOCKSIZE) {
						long int data_left = (mem_length - bytes_read);
						fread(write_buf, data_left, 1, tarfile);
						fwrite(write_buf, data_left, 1, member);
						break;
					}
					else {
						fread(write_buf, BLOCKSIZE, 1, tarfile);
						fwrite(write_buf, BLOCKSIZE, 1, member);
						bytes_read = bytes_read + BLOCKSIZE;
					}
				}
				free(write_buf);
				free(output);
				free(fullpath);
				mysql_close(con);
			}
		}
	}
}


