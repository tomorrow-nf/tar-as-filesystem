/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common_functions.h"

void read_tar(char* tar_filename, char* membername, size_t size, off_t offset, char* buf) {

	// Initial database connection
	MYSQL *con = mysql_init(NULL);
	mysql_init(con);
	printf("Connecting to database\n");
	if(!mysql_real_connect(con, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(con));
		//exit, no point
		mysql_close(con);
		return;
	}


	// Set file extension
	// TODO: This is copied from analyze_tar, we can probably combine them later
	char* tar_file_handle = strrchr(tar_filename, '.');
	if (!tar_file_handle) {
		printf("no file handle given\n");
		return;
	} 
	else {
		tar_file_handle = tar_file_handle + 1;
		// Save file extension. Validity will be checked later
	}
	
	// Member info to be queried from the database
	int gb_offset;
	long int b_offset;
	long long int mem_length;
	char* fullpath; //full path location of archive

	// Initial declarations for the tar archive and output file
	int tarfile;

	// Temporary values
	long long int longlongtmp;
	long int longtmp;
	char queryBuf[1000]; // query buffer
	MYSQL_ROW row; //row to store query results

	// check if file exists
	printf("Checking if file exists\n");
	sprintf(queryBuf, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", tar_filename);
	if(mysql_query(con, queryBuf)) {
		printf("Select error error:\n%s\n", mysql_error(con));
	}
	MYSQL_RES* result = mysql_store_result(con);
	if(mysql_num_rows(result) == 0) {
		printf("The archive file does not exist\n");
		mysql_free_result(result);
		mysql_close(con);
		return;
		
	}
	else {
		MYSQL_ROW row = mysql_fetch_row(result);
		fullpath = (char*) malloc(sizeof(char) * (strlen(row[1]) + 1));
		strcpy(fullpath, row[1]);
	}
	mysql_free_result(result);

	// Uncompressed tar archive
	if(strcmp(tar_file_handle, "tar") != 0) {
		printf("The file handle was not tar\n");
		mysql_close(con);
		return;
	}
	else {
		int tarfile = open(fullpath, O_RDONLY);
    		if(tarfile < 0) {
			printf("Unable to open file: %s\n", fullpath);
			free(fullpath);
			mysql_close(con);
			return;
		}
		else {
			// Query the offsets and member file length from the database
			sprintf(queryBuf, "SELECT * from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
			if(mysql_query(con, queryBuf)) {
				printf("Select error:\n%s\n", mysql_error(con));
				free(fullpath);
				close(tarfile);
				mysql_close(con);
				return;
			}
			result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				printf("The desired file does not exist\n");
				mysql_free_result(result);
				free(fullpath);
				close(tarfile);
				mysql_close(con);
				return;

			}
			row = mysql_fetch_row(result);
			gb_offset = atoi(row[3]);
			printf("GB offset: %d\n", gb_offset); //DEBUG
			b_offset = strtol(row[4], NULL, 10);
			printf("b_offset: %ld\n", b_offset); //DEBUG
			mem_length = strtoll(row[5], NULL, 8);
			printf("mem_length string: %s\n", row[4]); //DEBUG
			printf("mem_length: %lld\n", mem_length); //DEBUG
			
			mysql_free_result(result);

			// Seek to the file's offset
			if(gb_offset != 0) {
				int i;
				for(i=1; i<=gb_offset; i++) {
					lseek(tarfile, BYTES_IN_GB, SEEK_CUR);
				}
			}
			lseek(tarfile, b_offset, SEEK_CUR);

			// Seek to the desired offset
			lseek(tarfile, offset, SEEK_CUR);

			// Read the file
			printf("Copying the file\n");
			read(tarfile, (void*)buf, size);
			
			free(fullpath);
			close(tarfile);
			mysql_close(con);
			return;
		}
	}
}

int main(int argc, char* argv[]) {
	if(argc != 5) {
		printf("./read_tar.c filename, membername, size, offset\n");
		return 1;
	}

	char* tar_filename = argv[1];   // file to read member from
	char* membername = argv[2]; // the member to read from
	size_t size = strtol(argv[3], NULL, 10);
	off_t offset = strtol(argv[4], NULL, 10);
	char* buf = (char*) malloc(sizeof(char) * size);
	read_tar(tar_filename, membername, size, offset, buf);
	
	//print what we read
	int i;
	printf("data read:\n");
	for(i=0;i<size;i++) {
		printf("%c", buf[i]);
	}
	printf("\n\n");
}

