/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common_functions.h"


int main(int argc, char* argv[]) {
	/*TODO: Many of these values are currently hard-coded based on
		analyze_tar output of testarchive.tar. Needs to be
		updated in the future to accomodate SQL database
	*/

	//char* tar_file_handle;  		 	// file type (tar, bz2, gz, xz)
    char* tar_filename = argv[1]; 	// file to extract member from
	//char* output = strcat("temp/", argv[2]);  // member of archive to extract
	char* tar_file_handle = "tar";
    char* output = "temp/TEST2.TXT"; // Output directory

    long long int mem_length = 17; //TODO: get from database

    // Initial declarations for the tar archive and output file
    FILE* tarfile;
    FILE* member;
    // Offsets
    int gb_offset = 0;         	// number of gigabytes read so far
    //long int offset = 0; 		// total bytes read - (gigabytes read * bytes per gigabyte)
    long int b_offset = 1536; 	// total bytes read - (gigabytes read * bytes per gigabyte)
    long long int offset = gb_offset * BYTES_IN_GB + b_offset; // Calculate total offset in bytes

    // Temporary values
    long long int longlongtmp;
    long int longtmp;
   

    // Uncompressed tar archive
    if(strcmp(tar_file_handle, "tar") == 0) {
        tarfile = fopen(tar_filename, "r");
        if(!tarfile) {
            printf("Unable to open file: %s\n", tar_filename);
        }
        else {
        	fseek(tarfile, offset, SEEK_CUR); // Seek to the file's offset
            member = fopen(output, "w"); // Create a file to write to
            if(!member) {
	            printf("Unable to create file: %s\n", output);
	        }
	        else {
	            char* write_buf = (char*) malloc(BLOCKSIZE);
	            long int bytes_read = offset;

	            // Copy the file by blocks
	            if(mem_length > BYTES_IN_GB) {
                    long long int longlongtmp = mem_length;
                    while(longlongtmp > BYTES_IN_GB) {
                        fseek(tarfile, BYTES_IN_GB, SEEK_CUR);
                        gb_offset = gb_offset + 1;
                        longlongtmp = longlongtmp - BYTES_IN_GB;
                    }
                    long int longtmp = longlongtmp + (512 - (longlongtmp % 512));
                    //fseek(tarfile, longtmp, SEEK_CUR);
                    while ((bytes_read = fread(write_buf, 1, sizeof(write_buf), tarfile)) < longtmp && bytes_read != 0){
				    	fwrite(write_buf, 1, bytes_read, member);
				    }
				    printf("Copied file from %s to %s\n", tar_filename, output);
                }
                else if(mem_length == 0) {
                    // Nothing to copy
                    printf("File is empty, aborting\n");
                }
                else {
                    longtmp = mem_length + (512 - (mem_length % 512));
                    while ((bytes_read = fread(write_buf, 1, sizeof(write_buf), tarfile)) < longtmp && bytes_read != 0){
				    	fwrite(write_buf, 1, bytes_read, member);
				    }
				    printf("Copied file from %s to %s\n", tar_filename, output);
                }
			}
		}
	}
}


