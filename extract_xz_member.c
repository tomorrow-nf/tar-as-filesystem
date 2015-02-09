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
#include "xzfuncs.h"
#include "sqloptions.h"

/*
	Get the "blocknum" block in the xz file "filename", move foward "offset" bytes,
	copy bytes into "outbuf" until "size" bytes have been copied

	Requires that outbuf is already allocated to "size"
*/
char* getmember_xz(char* filename, int blocknum, long long int offset, long long int size, char* outbuf, struct blockmap* offsets) {

	long long int bytes_to_read = size;
	char* block = grab_block(blocknum, filename);
	char* location = block;
	location = location + offset;
	int current_blocknum = blocknum;

	if(block == NULL) {
		return 0; //TODO ERRNO
	}
	
	long long int dataremaining_in_block = ((offsets->blocklocations)[current_blocknum]).uncompressedSize - offset;
	
	int done = 1;
	while(done) {
		if(dataremaining_in_block > bytes_to_read)
			memcpy(outbuf, location, bytes_to_read);
			free(block);
			break;
		else {
			memcpy(outbuf, location, dataremaining_in_block);
			bytes_to_read = bytes_to_read - dataremaining_in_block;
			location = location + dataremaining_in_block;
			free(block);
			current_blocknum++;
			block = grab_block(current_blocknum, filename);
			if(block == NULL) return 0; //TODO ERRNO
			dataremaining_in_block = ((offsets->blocklocations)[current_blocknum]).uncompressedSize;
		}
	}

	/*
	// Grab a block and append it to the buffer. Repeat until the entire member is copied into the buffer
	for (int i = 0, sizeof(outbuf) < size, i++){
		if (i = 0){
			outbuf = outbuf + offset; // Increment by the offset on first pass
		}
		outbuf = strcat(outbuf, grab_block(blocknum, filename));
		blocknum++;
		getmember_xz(filename, blocknum, outbuf, size);
	}
	return outbuf;*/
}

/*
 	Query the TAR Browser database, use this info to extract the desired member from
 	the given XZ archive
*/

int extract_xz_member(MYSQL *con, char* filename, char* fullpath, char* membername) {

	char* destination = "/tmp/";

	char* output = (char*) malloc(sizeof(char) * (strlen(membername) + strlen(destination) + 1)); //output location of file
	sprintf(output, "%s%s", destination, membername);

	// Member info to be queried from the database
	//unsigned long long block_offset;
	int blocknum
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
	blocknum = atoi(row[5]);
	offset = strtol(row[7], NULL, 10);
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
	char* output_data = (char*) malloc(size);
	output_data = getmember_xz(filename, memberfile, blocknum, offset, size, output_data);

	// Write the output data into a file
	if (fwrite(output_data, sizeof(output_data), 1, memberfile) == 0){
		printf("There was an error extracting the file\n");
		free(output);
		close(tarfile);
		close(memberfile);
		return 1;
	}

	//close files and finish
	free(output);
	free(output_data);
	close(tarfile);
	close(memberfile);
	return 0;
}
