/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common_functions.h"
#include "sqloptions.h"


int main(int argc, char* argv[]) {

	// Initial database connection
	MYSQL *con = mysql_init(NULL);
	mysql_init(con);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	printf("Connecting to database\n");
	if(!mysql_real_connect(&mysql, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
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
	if(strcmp(tar_file_handle, "tar") != 0) {
		printf("The file handle was not tar\n");
		free(write_buf);
		free(output);
		mysql_close(con);
		return 1;
	}
	else {
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
			sprintf(queryBuf, "SELECT * from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
			if(mysql_query(con, queryBuf)) {
				printf("Select error:\n%s\n", mysql_error(con));
				free(write_buf);
				free(fullpath);
				free(output);
				fclose(tarfile);
				mysql_close(con);
				return 1;
			}
			result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				printf("The desired file does not exist\n");
				mysql_free_result(result);
				free(write_buf);
				free(fullpath);
				free(output);
				fclose(tarfile);
				mysql_close(con);
				return 1;

			}
			row = mysql_fetch_row(result);
			gb_offset = atoi(row[2]);
			printf("GB offset: %d\n", gb_offset); //DEBUG
			b_offset = strtol(row[3], NULL, 10);
			printf("b_offset: %ld\n", b_offset); //DEBUG
			mem_length = strtoll(row[4], NULL, 8);
			printf("mem_length string: %s\n", row[4]); //DEBUG
			printf("mem_length: %lld\n", mem_length); //DEBUG
			
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
				fclose(tarfile);
				return 1;
			}
			else {
				long long int bytes_read = 0;

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
				fclose(tarfile);
				fclose(member);
				mysql_close(con);
				return 0;
			}
		}
	}
}


