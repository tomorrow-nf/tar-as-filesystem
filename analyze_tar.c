/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <mysql.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "common_functions.h"

int analyze_tar(char* f_name) {

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
	char file_length_string[FILELENGTHFIELDSIZE];	// size of file in bytes (octal string)
	long long int file_length;			// size of file in bytes (long int)
	char trashbuffer[200];				// for unused fields
	char link_flag;					// flag indicating this is a file link
	char linkname[MEMBERNAMESIZE];			// name of linked file
	char ustarflag[USTARFIELDSIZE];			// field indicating newer ustar format
	char memberprefix[PREFIXSIZE];			// ustar includes a field for long names

	
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
	if(!mysql_real_connect(con, "localhost", "root", "root", "Tarfiledb", 0, NULL, 0)) {
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
			mysql_query(con, insQuery);
			MYSQL_RES* result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				printf("File does not exist\n");
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
					sprintf(insQuery, "DELETE FROM ArchiveList WHERE ArchiveName = '%s'", real_filename);
					if(mysql_query(con, insQuery)) {
						printf("Delete error:\n%s\n", mysql_error(con));
						dberror = 1;
					}
					sprintf(insQuery, "DELETE FROM UncompTar WHERE ArchiveName = '%s'", real_filename);
					if(mysql_query(con, insQuery)) {
						printf("Delete error:\n%s\n", mysql_error(con));
						dberror = 1;
					}
				}
			}
			
			// file is not in database or it has been cleared from database
			sprintf(insQuery, "INSERT INTO ArchiveList VALUES ('%s', '%s', 'timestamp')", real_filename, fullpath);
			if(mysql_query(con, insQuery)) {
				printf("Insert error:\n%s\n", mysql_error(con));
				dberror = 1;
			}		

			while(1) {
				// Evaluate the tar header
				printf("member header offset: %d GB and %ld bytes\n", GB_read, bytes_read);

				//get filename
				fread((void*)membername, MEMBERNAMESIZE, 1, tarfile);
				printf("member name: %s\n", membername);
				bytes_read = bytes_read + MEMBERNAMESIZE;

				//discard mode, uid, and gid (8 bytes each)
				fread((void*)trashbuffer, (sizeof(char) * 24), 1, tarfile);
				bytes_read = bytes_read + (sizeof(char) * 24);

				//get length of file in bytes
				fread((void*)file_length_string, FILELENGTHFIELDSIZE, 1, tarfile);
				printf("file length (string): %s\n", file_length_string);
				file_length = strtoll(file_length_string, NULL, 8);
				printf("file length (int): %lld\n", file_length);
				bytes_read = bytes_read + FILELENGTHFIELDSIZE;

				// CHECK FOR ././@LongLink
				if(strcmp(membername, "././@LongLink") == 0) {
					//skip to end of block
					fseek(tarfile, 376, SEEK_CUR);
					bytes_read = bytes_read + 376;
					
					//read the real filename
					fread((void*)membername, file_length, 1, tarfile);
					printf("real member name: %s\n", membername);
					bytes_read = bytes_read + file_length;

					//skip to end of block
					fseek(tarfile, (512 - (file_length % 512)), SEEK_CUR);
					bytes_read = bytes_read + (512 - (file_length % 512));

					//skip to the real file length
					fseek(tarfile, (MEMBERNAMESIZE + 24), SEEK_CUR);
					bytes_read = bytes_read + (MEMBERNAMESIZE + 24);

					//get length of file in bytes
					fread((void*)file_length_string, FILELENGTHFIELDSIZE, 1, tarfile);
					printf("real file length (string): %s\n", file_length_string);
					file_length = strtoll(file_length_string, NULL, 8);
					printf("real file length (int): %lld\n", file_length);
					bytes_read = bytes_read + FILELENGTHFIELDSIZE;
				}

				//discard modify time and checksum (20 bytes)
				fread((void*)trashbuffer, (sizeof(char) * 20), 1, tarfile);
				bytes_read = bytes_read + (sizeof(char) * 20);

				//get link flag (1 byte)
				fread((void*)(&link_flag), sizeof(char), 1, tarfile);
				printf("link flag: %c\n", link_flag);
				bytes_read = bytes_read + sizeof(char);

				//get linked filename (if flag set, otherwise this field is useless)
				fread((void*)linkname, MEMBERNAMESIZE, 1, tarfile);
				printf("link name: %s\n", linkname);
				bytes_read = bytes_read + MEMBERNAMESIZE;

				//get ustar flag and version, ignore version in check
				fread((void*)ustarflag, USTARFIELDSIZE, 1, tarfile);
				printf("ustar flag: %s\n", ustarflag);
				bytes_read = bytes_read + USTARFIELDSIZE;

				// if flag is ustar get rest of fields, else we went into data, go back
				if(strncmp(ustarflag, "ustar", 5) == 0) {
					//discard ustar data (80 bytes)
					fread((void*)trashbuffer, (sizeof(char) * 80), 1, tarfile);
					bytes_read = bytes_read + (sizeof(char) * 80);

					//get ustar file prefix (may be nothing but /0)
					fread((void*)memberprefix, PREFIXSIZE, 1, tarfile);
					printf("file prefix: %s\n", memberprefix);
					bytes_read = bytes_read + PREFIXSIZE;
				}
				else {
					fseek(tarfile, (-8), SEEK_CUR); //go back 8 bytes
					bytes_read = bytes_read - 8;
				}

				// Analyze the archive contents
				//skip rest of 512 byte block
				longtmp = 512 - (bytes_read % 512); //get bytes left till end of block
				if(longtmp != 0) {
					fseek(tarfile, longtmp, SEEK_CUR);
					bytes_read = bytes_read + longtmp;
				}

				//reduce bytes read to below a gigabyte
				if(bytes_read >= BYTES_IN_GB) {
					bytes_read = bytes_read - BYTES_IN_GB;
					GB_read = GB_read + 1;
				}

				//print beginning point of data
				printf("data begins at %d GB and %ld bytes\n", GB_read, bytes_read);

				// Build the query and submit it
				sprintf(insQuery, "INSERT INTO UncompTar VALUES (0, '%s', '%s', %d, %ld, '%s', '%c')", real_filename, membername, GB_read, bytes_read, file_length_string, link_flag);
				if(mysql_query(con, insQuery)) {
					printf("Insert error:\n%s\n", mysql_error(con));
					printf("%s\n", insQuery);
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
				printf("\n");

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
			fclose(tarfile);
		}

	//close database connection
     mysql_close(con);

     return 0;
 }
