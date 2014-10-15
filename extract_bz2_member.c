/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "common_functions.h"
#include "bzip_seek/bitmapstructs.h"


int main(int argc, char* argv[]) {

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
	char* tar_file_handle = strrchr(tar_filename, '.');
	if (!tar_file_handle) {
		printf("no file handle given\n");
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
	unsigned long long block_offset;
	long int inside_offset;
	long long int mem_length;
	
	char* fullpath; //full path location of archive

	// Initial declarations for the tar archive and output file
	int tarfile;
	int member;

	// Temporary values
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
	// Bzip2 compressed tar archive
	if(strcmp(tar_file_handle, "bz2") != 0) {
		printf("The file handle is not bz2\n");
		free(output);
		mysql_close(con);
		return 1;
	}
	else {
		tarfile = open(fullpath, O_RDONLY);
		if(tarfile < 0) {
			printf("Unable to open file: %s\n", fullpath);
			free(fullpath);
			free(output);
			mysql_close(con);
			return 1;
		}
		else {
			// Query the offsets and member file length from the database
			sprintf(queryBuf, "SELECT * from Bzip2_files WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
			if(mysql_query(con, queryBuf)) {
				printf("Select error:\n%s\n", mysql_error(con));
				free(fullpath);
				free(output);
				close(tarfile);
				mysql_close(con);
				return 1;
			}
			result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				printf("The desired file does not exist\n");
				mysql_free_result(result);
				free(fullpath);
				free(output);
				close(tarfile);
				mysql_close(con);
				return 1;

			}
			row = mysql_fetch_row(result);
			
			block_offset = strtoull(row[3], NULL, 10);
			printf("block_offset: %llu\n", block_offset); //DEBUG
			inside_offset = strtol(row[4], NULL, 10);
			printf("inside_offset: %ld\n", inside_offset); //DEBUG
			mem_length = strtoll(row[5], NULL, 8);
			printf("mem_length string: %s\n", row[4]); //DEBUG
			printf("mem_length: %lld\n", mem_length); //DEBUG
			
			mysql_free_result(result);

			
			member = creat(output, S_IRWXU); // Create a file to write to
			if(member < 0) {
				printf("Unable to create file: %s\n", output);
				free(output);
				free(fullpath);
				mysql_close(con);
				close(tarfile);
				return 1;
			}
			
			printf("Both files open, uncompressing\n");
			//pass the information & file handles to the function that decompresses, seeks, & writes
			int uncomperror = uncompressfile( tarfile, member, block_offset, inside_offset, mem_length );
			if(uncomperror) {
				printf("There was an error in extracting the file\n");
				free(output);
				free(fullpath);
				mysql_close(con);
				close(tarfile);
				close(member);
				return 1;
			}

			//close files and finish
			free(output);
			free(fullpath);
			mysql_close(con);
			close(tarfile);
			close(member);
			return 0;
		}
	}
}
