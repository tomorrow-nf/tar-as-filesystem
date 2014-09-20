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

    // database values
    char* output = "temp/TEST1.txt"; //TODO get from database
    int gb_offset = 0;         	//TODO get from database
    long int b_offset = 10240; 	// TODO get from database
    long long int mem_length = 15; //TODO: get from database

    // Initial declarations for the tar archive and output file
    FILE* tarfile;
    FILE* member;

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
            }
            else {
                void* write_buf = (void*) malloc(BLOCKSIZE);
                long long int bytes_read = 0;

                // Copy the file by blocks
// (bytes_read = fread(write_buf, 1, sizeof(write_buf), tarfile)) < longtmp && bytes_read != 0)
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
            }
        }
    }
}


