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
#include "sqloptions.h"

/*
 	Query the TAR Browser database, use this info to extract the desired member from
 	the given BZip2 archive
*/

int extract_bzip2_member(MYSQL *con, char* filename, char* fullpath, char* membername) {

	char* destination = "/tmp/";

	char* output = (char*) malloc(sizeof(char) * (strlen(membername) + strlen(destination) + 1)); //output location of file
	sprintf(output, "%s%s", destination, membername);

	// Member info to be queried from the database
	//unsigned long long block_offset;
	long int block_offset;
	long int offset;
	long long int size;

	// Temporary values
	char queryBuf[1000]; // query buffer
	MYSQL_ROW row; //row to store query results

	// Query the offsets and member file length from the database
	sprintf(queryBuf, "SELECT * from CompXZ WHERE ArchiveName = '%s' AND MemberName = '%s'", filename, membername);
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
		
	row = mysql_fetch_row(result);
			
	// Save the queried data
	block_offset = atoi(row[6]);
	inside_offset = strtol(row[7], NULL, 10);
	size = strtoll(row[8], NULL, 8);

	mysql_free_result(result);
			

	// File operations
	int tarfile = open(fullpath, O_RDONLY);
	if(tarfile < 0) {
		printf("Unable to open file: %s\n", fullpath);
		free(output);
		mysql_close(con);
		return 1;
	}

	int memberfile = creat(output, S_IRWXU); // Create a file to write to
	if(memberfile < 0) {
		printf("Unable to create file: %s\n", output);
		free(output);
		return 1;
	}
		
	// Extract the member
	//pass the information & file handles to the function that decompresses, seeks, & writes
	int uncomperror = uncompressfile( tarfile, memberfile, block_offset, inside_offset, size );
	if(uncomperror) {
		printf("There was an error extracting the file\n");
		free(output);
		close(tarfile);
		close(memberfile);
		return 1;
	}

	//close files and finish
	free(output);
	close(tarfile);
	close(memberfile);
	return 0;
}
