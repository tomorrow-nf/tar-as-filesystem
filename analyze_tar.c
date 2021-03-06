/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "common_functions.h"
#include "sqloptions.h"

int analyze_tar(char* f_name, struct stat filestats, int show_output) {

    FILE* tarfile;
	int GB_read = 0;         	  // number of gigabytes read so far
	long int bytes_read = 0;      // total bytes read - (gigabytes read * bytes per gigabyte)
	char* tar_filename = f_name;  // file to analyze
	char* real_filename;          // the filename without any directory info in front of it
	char* fullpath;               // the absolute path to the file
	long int longtmp; 			  // temporary variable for calculations
	long long int longlongtmp; 	  // temporary variable for calculations
	int dberror = 0;              // indicate an error in analysis

	// End of archive check
	char archive_end_check[1024];
	char archive_end[1024];
	memset(archive_end, 0, sizeof(archive_end));

	// Information for TAR archive and member headers
	char membername[5000];				// name of member file
	long long int file_length;			// size of file in bytes (long int)
	char linkname[5000];				// name of linked file
	struct headerblock header;
	unsigned long long int archive_id;

	//long name and link flags
	int the_name_is_long = 0;
	int the_link_is_long = 0;

	
	// get local path to file
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
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		printf("Connection Failure: %s\n", mysql_error(con));
		//exit, no point
		mysql_close(con);
		return 1;
	}
	

	tarfile = fopen(tar_filename, "r");
	if(!tarfile) {
		printf("Unable to open file: %s\n", tar_filename);
	}
	else {
			// begin transaction and check if this archive exists
			char insQuery[1000]; // insertion query buffer (we dont want current timestamp, we want the file's last modified timestamp)
			if(mysql_query(con, "START TRANSACTION")) {
				printf("Start Transaction error:\n%s\n", mysql_error(con));
				fclose(tarfile);
				mysql_close(con);
				return 1;
			}

			//check if file already exists and ask for permission to overwrite and remove references
			sprintf(insQuery, "SELECT * from ArchiveList WHERE ArchiveName = '%s'", real_filename);
			if(mysql_query(con, insQuery)) {
				printf("Select error:\n%s\n", mysql_error(con));
				fclose(tarfile);
				mysql_close(con);
				return 1;
			}
			
			MYSQL_RES* result = mysql_store_result(con);
			
			if(mysql_num_rows(result) == 0) {
				if(show_output) printf("File does not exist\n");
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
					fclose(tarfile);
					mysql_close(con);
					return 1;
				}
				else {
					sprintf(insQuery, "DELETE FROM UncompTar WHERE ArchiveName = '%s'", real_filename);
					if(mysql_query(con, insQuery)) {
						if(show_output) printf("Delete error:\n%s\n", mysql_error(con));
						dberror = 1;
					}
					sprintf(insQuery, "DELETE FROM ArchiveList WHERE ArchiveName = '%s'", real_filename);
					if(mysql_query(con, insQuery)) {
						if(show_output) printf("Delete error:\n%s\n", mysql_error(con));
						dberror = 1;
					}
				}
			}
			
			// file is not in database or it has been cleared from database
			char* mod_time = ctime(&(filestats.st_mtime));
			sprintf(insQuery, "INSERT INTO ArchiveList VALUES (0, '%s', '%s', '%s')", real_filename, fullpath, mod_time);

			if(mysql_query(con, insQuery)) {
				if(show_output) printf("Insert error:\n%s\n", mysql_error(con));
				dberror = 1;
			}
			archive_id = mysql_insert_id(con);
			if(archive_id == 0) {
				if(show_output) printf("Archive Id error, was 0\n");
				dberror = 1;
			}		

			while(1) {
				the_name_is_long = 0;
				the_link_is_long = 0;

				// Evaluate the tar header
				if(show_output) printf("member header offset: %d GB and %ld bytes\n", GB_read, bytes_read);


				//get tar header
				fread((void*)(&header), sizeof(struct headerblock), 1, tarfile);
				bytes_read = bytes_read + sizeof(struct headerblock);

				// CHECK FOR ././@LongLink
				if(strcmp(header.name, "././@LongLink") == 0) {
					if(show_output) printf("found a LongLink\n");

					//get length of name in bytes
					file_length = strtoll(header.size, NULL, 8);
					if(show_output) printf("LongLink's length (int): %lld\n", file_length);
					
					//read the real name
					if(header.typeflag[0] == 'K') {
						fread((void*)linkname, file_length, 1, tarfile);
						if(show_output) printf("the target of the link is: %s\n", linkname);
						the_link_is_long = 1;
					}
					else if(header.typeflag[0] == 'L') {
						fread((void*)membername, file_length, 1, tarfile);
						if(show_output) printf("the name of the member is: %s\n", membername);
						the_name_is_long = 1;
					}
					else {
						//TODO PROBLEM
					}
					bytes_read = bytes_read + file_length;

					//skip to end of block
					fseek(tarfile, (512 - (file_length % 512)), SEEK_CUR);
					bytes_read = bytes_read + (512 - (file_length % 512));

					//get the next header
					fread((void*)(&header), sizeof(struct headerblock), 1, tarfile);
					bytes_read = bytes_read + sizeof(struct headerblock);
				}
				if(strcmp(header.name, "././@LongLink") == 0) {
					if(show_output) printf("found a LongLink\n");

					//get length of name in bytes
					file_length = strtoll(header.size, NULL, 8);
					if(show_output) printf("LongLink's length (int): %lld\n", file_length);
					
					//read the real name
					if(header.typeflag[0] == 'K') {
						fread((void*)linkname, file_length, 1, tarfile);
						if(show_output) printf("the target of the link is: %s\n", linkname);
						the_link_is_long = 1;
					}
					else if(header.typeflag[0] == 'L') {
						fread((void*)membername, file_length, 1, tarfile);
						if(show_output) printf("the name of the member is: %s\n", membername);
						the_name_is_long = 1;
					}
					else {
						//TODO PROBLEM
					}
					bytes_read = bytes_read + file_length;

					//skip to end of block
					fseek(tarfile, (512 - (file_length % 512)), SEEK_CUR);
					bytes_read = bytes_read + (512 - (file_length % 512));

					//get the next header
					fread((void*)(&header), sizeof(struct headerblock), 1, tarfile);
					bytes_read = bytes_read + sizeof(struct headerblock);
				}

				if(show_output) printf("Reading real member's information\n");

				//get length of file in bytes
				file_length = strtoll(header.size, NULL, 8);
				if(show_output) printf("member's data length (int): %lld\n", file_length);

				//get filename (if name was not long)
				if(!the_name_is_long) {
					strncpy(membername, header.name, 100);
					membername[100] = '\0'; //force null terminated string
				}

				//get linked filename (if flag set, otherwise this field is useless)
				if(!the_link_is_long) {
					strncpy(linkname, header.linkname, 100);
				}
				if(show_output) printf("link name: %s\n", linkname);

				//convert to a name and directory path
				char membername_path[5000];
				char membername_file[5000];
				char* membername_ptr;

				//add beginning /
				sprintf(membername_path, "/%s", membername);

				//if last character is / kill it and set directory flag
				int slashes;
				char dirflag = 'N';
				if(membername_path[strlen(membername_path) - 1] == '/') {
					membername_path[strlen(membername_path) - 1] = '\0';
					slashes = 1;
					dirflag = 'Y';
				}
				else {
					slashes = 1;
				}

				//find breakpoint between filename and path
				membername_ptr = &(membername_path[strlen(membername_path) - 1]);
				while(1) {
					if(*membername_ptr == '/') slashes--;
					if(slashes == 0) break;
					membername_ptr--;
				}
				membername_ptr++; //now points to beginning of file name
				sprintf(membername_file, "%s", membername_ptr); //copy filename
				*membername_ptr = '\0'; // truncate path string

				if(show_output)  {
					printf("MEMBERNAME PATH: %s\n", membername_path);
					printf("REAL MEMBERNAME: %s\n", membername_file);
				}

				//reduce bytes read to below a gigabyte
				if(bytes_read >= BYTES_IN_GB) {
					bytes_read = bytes_read - BYTES_IN_GB;
					GB_read = GB_read + 1;
				}

				//print beginning point of data
				if(show_output) printf("data begins at %d GB and %ld bytes\n", GB_read, bytes_read);

				// Build the query and submit it
				sprintf(insQuery, "INSERT INTO UncompTar VALUES (0, %llu, '%s', '%s', '%s', %d, %ld, %llu, '%c', '%c', %ld, %ld, %ld, '%s')", archive_id, real_filename, membername_file, membername_path, GB_read, bytes_read, strtoull(header.size, NULL, 8), header.typeflag[0], dirflag, strtol(header.mode, NULL, 8), strtol(header.uid, NULL, 8), strtol(header.gid, NULL, 8), linkname);
				if(mysql_query(con, insQuery)) {
					if(show_output) {
						printf("Insert error:\n%s\n", mysql_error(con));
						printf("%s\n", insQuery);
					}
					dberror = 1;
				}

				//skip data
				//SEEK_CUR = current position macro, already defined
				if(file_length >= BYTES_IN_GB) {
					longlongtmp = file_length;
					while(longlongtmp >= BYTES_IN_GB) {
						fseek(tarfile, BYTES_IN_GB, SEEK_CUR);
						GB_read = GB_read + 1;
						longlongtmp = longlongtmp - BYTES_IN_GB;
					}
					if(longlongtmp > 0) {
						if((longlongtmp % 512) != 0) {
							longtmp = longlongtmp + (512 - (longlongtmp % 512));
						}
						else {
							longtmp = longlongtmp;
						}
						fseek(tarfile, longtmp, SEEK_CUR);
						bytes_read = bytes_read + longtmp;
					}
				}
				else if(file_length == 0) {
					// do not skip any data
				}
				else {
					if((file_length % 512) != 0) {
						longtmp = file_length + (512 - (file_length % 512));
					}
					else {
						longtmp = file_length;
					}
					fseek(tarfile, longtmp, SEEK_CUR);
					bytes_read = bytes_read + longtmp;
				}

				//end printed info with newline
				if(show_output) printf("\n");

				//check for end of archive
				fread((void*)archive_end_check, sizeof(archive_end_check), 1, tarfile);
				if(memcmp(archive_end_check, archive_end, sizeof(archive_end)) == 0) {
					break; //1024 bytes of zeros mark end of archive
				}
				else {
					fseek(tarfile, (-1 * sizeof(archive_end_check)), SEEK_CUR); //move back 1024 bytes
				}
			}
			//the file has been read, commit the transation and close the connection
			if(dberror == 1) {
				if(mysql_query(con, "ROLLBACK")) {
					printf("Rollback error:\n%s\n", mysql_error(con));
				}
				else {
					printf("Error Occured: Entries rolled back\n");
				}
			}
			else {
				if(mysql_query(con, "COMMIT")) {
					printf("Commit error:\n%s\n", mysql_error(con));
				}
				else {
					if(show_output) printf("Entries committed\n");
				}
			}
			fclose(tarfile);
		}

	//close database connection
     mysql_close(con);

     return 0;
 }
