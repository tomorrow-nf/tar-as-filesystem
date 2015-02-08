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


/*
 	Query the TAR Browser database, use this info to extract the desired member from
 	the given uncompressed TAR archive
*/

int extract_tar_member(MYSQL *con, char* filename, char* fullpath, char* membername) {

	char* destination = "/tmp/";

	char* output = (char*) malloc(sizeof(char) * (strlen(membername) + strlen(destination) + 1)); //output location of file
	sprintf(output, "%s%s", destination, membername);
	
	// Member info to be queried from the database
	int gb_offset;
	long int b_offset;
	long long int mem_length;
	void* write_buf = (void*) malloc(BLOCKSIZE);

	// Temporary values
	long long int longlongtmp;
	long int longtmp;
	char queryBuf[1000]; // query buffer
	MYSQL_ROW row; //row to store query results


	// Query the offsets and member file length from the database
	sprintf(queryBuf, "SELECT * from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", filename, membername);
	if(mysql_query(con, queryBuf)) {
		printf("Select error:\n%s\n", mysql_error(con));
		free(output);
		return 1;
	}

	result = mysql_store_result(con);

	if(mysql_num_rows(result) == 0) {
		printf("The desired member does not exist\n");
		mysql_free_result(result);
		free(output);
		return 1;
	}

	// Results of query
	row = mysql_fetch_row(result);

	gb_offset = atoi(row[2]);
	b_offset = strtol(row[3], NULL, 10);
	mem_length = strtoll(row[4], NULL, 8);

	mysql_free_result(result);

	// File operations
	FILE* tarfile = fopen(fullpath, "r");
	if(!tarfile) {
		printf("Unable to open file: %s\n", fullpath);
		free(output);
		return 1;
	}

	FILE* memberfile = fopen(output, "w"); // Create a file to write to
	if(!memberfile) {
		printf("Unable to create file: %s\n", output);
		free(output);
		return 1;
	}

	// Seek to the file's offset
	if(gb_offset != 0) {
		int i;
		for(i=1; i<=gb_offset; i++) {
			fseek(tarfile, BYTES_IN_GB, SEEK_CUR);
		}
	}

	long long int bytes_read = 0;

	// Copy the file by blocks
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
	fclose(tarfile);
	fclose(member);
	return 0;
}



