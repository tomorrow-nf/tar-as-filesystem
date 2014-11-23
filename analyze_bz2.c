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

void* getblock_bzip(char* filename, int blocknum, struct blockmap* offsets) {
	
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
	char archive_end_check[512];
	char archive_end[512];
	memset(archive_end, 0, sizeof(archive_end));

	// Information for BZ2 archive and member headers
	char membername[5000];				// name of member file
	long long int file_length;			// size of file in bytes (long int)
	char linkname[5000];				// name of linked file

	long int blocksize; // bz2 blocksize (inteval of 100 from 100kB to 900kB)
	int bzerror; // bz2 error checking
	int blocknumber = 0; // block number being read
	int numblocks = 1; // number of blocks data exists in
	int datafromarchivecheck = 0; //if archivecheck had to backtrack
	long int tmp_dataread;
	struct headerblock header;
	unsigned long long int archive_id; //id associated with the archive being analyzed

	//long name and link flags
	int the_name_is_long = 0;
	int the_link_is_long = 0;

	struct blockmap* block_offsets = (struct blockmap*) malloc(sizeof(struct blockmap));
	block_offsets->blocklocations = (struct blocklocation*) malloc(sizeof(struct blocklocation) * 200);
	block_offsets->maxsize = 200;

	int M_result = map_bzip2(tar_filename, block_offsets);
        if(M_result != 0) {
		printf("Error getting block sizes\n");
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
	if(mysql_query(con, insQuery)) {
		printf("Select error:\n%s\n", mysql_error(con));
		mysql_close(con);
		free(block_offsets->blocklocations);
		free(block_offsets);
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
			free(block_offsets->blocklocations);
			free(block_offsets);
			return 0;
		}
		else {
			sprintf(insQuery, "DELETE FROM Bzip2_files WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
			sprintf(insQuery, "DELETE FROM Bzip2_blocks WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
			sprintf(insQuery, "DELETE FROM ArchiveList WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Delete error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
		}
	}
		
	// file is not in database or it has been cleared from database
	sprintf(insQuery, "INSERT INTO ArchiveList VALUES (0, '%s', '%s', 'timestamp')", real_filename, fullpath);
	if(mysql_query(con, insQuery)) {
		printf("Insert error:\n%s\n", mysql_error(con));
		dberror = 1;
	}
	archive_id = mysql_insert_id(con);
	if(archive_id == 0) {
		printf("Archive Id error, was 0\n");
		dberror = 1;
	}

	// Get a block as a char pointer for easy byte incrementing
	blocknumber++;
	char* memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
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
printf("BEGINNING FILE ANALYSIS\n");
		the_name_is_long = 0;
		the_link_is_long = 0;
		
		if(firstrun) {
			firstrun = 0;
			if(remainingdata >= 512){
				memcpy((void*) (&header), (memblock + blockposition), sizeof(header));
				remainingdata = remainingdata - sizeof(header);
				blockposition = blockposition + sizeof(header);
			}
			else {
				tmp_dataread = remainingdata;
				memcpy((void*) (&header), (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				blocknumber++;
				memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
				if(memblock == NULL) {
					mysql_close(con);
					free(block_offsets->blocklocations);
					free(block_offsets);
					return 1;
				}
				remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
				blockposition = 0;
	
				memcpy((void*)(((char*)&header) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
				remainingdata = remainingdata - (512 - tmp_dataread);
				blockposition = blockposition + (512 - tmp_dataread);
			}
		}

		// TAR HEADER SECTION - extract data from header

		// CHECK FOR ././@LongLink
		int tmp_longlink_position = 0;
		int position_incrementer = 0;
		while(strcmp(header.name, "././@LongLink") == 0) {
			printf("found a LongLink\n");

			//get length of name in bytes, adjust to be at end of block
			file_length = strtoll(header.size, NULL, 8);
			if((file_length % 512) != 0) {
				position_incrementer = file_length + (512 - (file_length % 512));
			}
			else {
				position_incrementer = file_length;
			}
			printf("LongLink's length (int): %lld\n", file_length);

			//copy longlink into proper area
			if(remainingdata >= position_incrementer){
				if(header.typeflag[0] == 'L') {
					memcpy((void*) membername, (memblock + blockposition), file_length);
printf("READING INTO MEMBERNAME\n");
					the_name_is_long = 1;
				}
				else if(header.typeflag[0] == 'K') {
					memcpy((void*) linkname, (memblock + blockposition), file_length);
printf("READING INTO LINKNAME\n");
					the_link_is_long = 1;
				}
				remainingdata = remainingdata - position_incrementer;
				blockposition = blockposition + position_incrementer;
			}
			else {
				tmp_dataread = remainingdata;
				if(header.typeflag[0] == 'L') {
					memcpy((void*) membername, (memblock + blockposition), (sizeof(char) * remainingdata));
printf("READING INTO MEMBERNAME\n");
					the_name_is_long = 1;
				}
				else if(header.typeflag[0] == 'K') {
					memcpy((void*) linkname, (memblock + blockposition), (sizeof(char) * remainingdata));
printf("READING INTO LINKNAME\n");
					the_link_is_long = 1;
				}
				free(memblock);

				blocknumber++;
				memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
				if(memblock == NULL) {
					mysql_close(con);
					free(block_offsets->blocklocations);
					free(block_offsets);
					return 1;
				}
				remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
				blockposition = 0;

				if(header.typeflag[0] == 'L') {
					memcpy((void*)(((char*)membername) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (file_length - tmp_dataread)));
printf("READING INTO MEMBERNAME\n");
					the_name_is_long = 1;
				}
				else if(header.typeflag[0] == 'K') {
					memcpy((void*)(((char*)linkname) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (file_length - tmp_dataread)));
printf("READING INTO LINKNAME\n");
					the_link_is_long = 1;
				}
				
				remainingdata = remainingdata - (position_incrementer - tmp_dataread);
				blockposition = blockposition + (position_incrementer - tmp_dataread);
			}

			// load in next header
			if(remainingdata >= 512){
				memcpy((void*) (&header), (memblock + blockposition), sizeof(header));
				remainingdata = remainingdata - sizeof(header);
				blockposition = blockposition + sizeof(header);
			}
			else {
				tmp_dataread = remainingdata;
				memcpy((void*) (&header), (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				blocknumber++;
				memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
				if(memblock == NULL) {
					mysql_close(con);
					free(block_offsets->blocklocations);
					free(block_offsets);
					return 1;
				}
				remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
				blockposition = 0;
	
				memcpy((void*)(((char*)&header) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
				remainingdata = remainingdata - (512 - tmp_dataread);
				blockposition = blockposition + (512 - tmp_dataread);
			}
		}

		//get filename
		if(!the_name_is_long) {
			strncpy(membername, header.name, 100);
		}
		printf("member name: %s\n", membername);

		//get length of file in bytes
		file_length = strtoll(header.size, NULL, 8);
		printf("file length (int): %lld\n", file_length);

		//get link flag (1 byte)
		printf("link flag: %c\n", header.typeflag[0]);

		//get linked filename (if flag set, otherwise this field is useless)
		if(!the_link_is_long) {
			strncpy(linkname, header.linkname, 100);
		}
		printf("link name: %s\n", linkname);


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
				memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
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

		//convert to a name and directory path
		char* membername_nopath;
		char membername_path[5000];

		membername_nopath = strrchr(membername, '/');
		if (!membername_nopath) {
			membername_nopath = membername;
			membername_path[0] = '/';
			membername_path[1] = '\0';
		} 
		else {
			membername_nopath++;
			int membername_nopaths_length = strlen(membername_nopath);
			int membername_length = strlen(membername);
			membername_path[0] = '/';
			int i = 1;
			for(i=1;i<=(membername_length - membername_nopaths_length);i++) {
				membername_path[i] = membername[i-1];
			}
			membername_path[i] = '\0';
		}
		if(strcmp("", membername_nopath) == 0) {
			membername_nopath = " ";
		}
		printf("MEMBERNAME PATH: %s\n", membername_path);
		printf("REAL MEMBERNAME: %s\n", membername_nopath);

		// Build the query and submit it
		sprintf(insQuery, "INSERT INTO Bzip2_files VALUES (0, %llu, '%s', '%s', '%s', %d, %llu, %ld, '%s', '%c')", archive_id, real_filename, membername_nopath, membername_path, tmp_blocknumber, ((block_offsets->blocklocations)[tmp_blocknumber]).position, tmp_blockposition, header.size, header.typeflag[0]);
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
		if(remainingdata >= 512){
			memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * 512));
			remainingdata = remainingdata - 512;
			blockposition = blockposition + 512;
		}
		else {
			tmp_dataread = remainingdata;
			memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * remainingdata));
			free(memblock);

			blocknumber++;
			memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
			if(memblock == NULL) {
				mysql_close(con);
				free(block_offsets->blocklocations);
				free(block_offsets);
				return 1;
			}
			remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
			blockposition = 0;

			memcpy((void*)(((char*)archive_end_check) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
			remainingdata = remainingdata - (512 - tmp_dataread);
			blockposition = blockposition + (512 - tmp_dataread);
		}

		if(memcmp(archive_end_check, archive_end, sizeof(archive_end)) == 0) {
			//check subsequent block for zeros and error if not
			tmp_dataread = 0;
			if(remainingdata >= 512){
				memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * 512));
				remainingdata = remainingdata - 512;
				blockposition = blockposition + 512;
			}
			else {
				tmp_dataread = remainingdata;
				memcpy((void*) archive_end_check, (memblock + blockposition), (sizeof(char) * remainingdata));
				free(memblock);

				blocknumber++;
				memblock = (char*) getblock_bzip(tar_filename, blocknumber, block_offsets);
				if(memblock == NULL) {
					mysql_close(con);
					free(block_offsets->blocklocations);
					free(block_offsets);
					return 1;
				}
				remainingdata = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
				blockposition = 0;

				memcpy((void*)(((char*)archive_end_check) + tmp_dataread), (memblock + blockposition), (sizeof(char) * (512 - tmp_dataread)));
				remainingdata = remainingdata - (512 - tmp_dataread);
				blockposition = blockposition + (512 - tmp_dataread);
			}
			
			if(memcmp(archive_end_check, archive_end, sizeof(archive_end)) == 0) {
				break;
			}
			else {
				printf("improper end to tar file found stopping analysis\n");
				break;
			}
		}
		else {
			//put that 512 bytes as the next header
			memcpy((void*) (&header), (void*)archive_end_check, sizeof(header));
		}
	}

	free(memblock);

	//store the block map (blocknumber = the last block)
	int b_cur = 1;
	for(b_cur=1;b_cur<=blocknumber;b_cur++) {
		printf("storing block %d\n", b_cur);
		sprintf(insQuery, "INSERT INTO Bzip2_blocks VALUES (%llu, '%s', %d, %llu, %llu)", archive_id, real_filename, b_cur, ((block_offsets->blocklocations)[b_cur]).position, ((block_offsets->blocklocations)[b_cur]).uncompressedSize);
		if(mysql_query(con, insQuery)) {
			printf("Insert error:\n%s\n", mysql_error(con));
			printf("%s\n", insQuery);
			dberror = 1;
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


	mysql_close(con);
	free(block_offsets->blocklocations);
	free(block_offsets);
	return 0;
}
