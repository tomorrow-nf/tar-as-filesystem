/*
    Extracts a given member of a tar archive without
    extracting any other files before it
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql.h>
#include "common_functions.h"


int main(int argc, char* argv[]) {

    //TODO: MEMORY MANAGEMENT

    // Initial database connection
    MYSQL *con = mysql_init(NULL);

    char* tar_filename = argv[1];   // file to extract member from
    // Set file extension
    // TODO: This is copied from analyze_tar, we can probably combine them later
    char* tar_file_handle = strrchr(tar_filename, '.');
    if (!tar_file_handle) {
        //TODO error if no extension given
        return 1;
    } 
    else {
        tar_file_handle = tar_file_handle + 1;
        // Save file extension. Validity will be checked later
    }

    char* membername = argv[2]; // member of archive to extract
    char* output = strcat("temp/", membername); // output directory

    // Member info to be queried from the database
    int gb_offset;
    long int b_offset;
    long long int mem_length;


    // Initial declarations for the tar archive and output file
    FILE* tarfile;
    FILE* member;

    // Temporary values
    long long int longlongtmp;
    long int longtmp;
    char queryBuf[500]; // query buffer


    // Uncompressed tar archive
    if(strcmp(tar_file_handle, "tar") == 0) {
        tarfile = fopen(tar_filename, "r");
        if(!tarfile) {
            printf("Unable to open file: %s\n", tar_filename);
        }
        else {
            // Query the offsets and member file length from the database
            sprintf(queryBuf, "SELECT GBoffset from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
            mysql_query(con, queryBuf);
            MYSQL_RES* result = mysql_use_result(con);
            MYSQL_ROW row = mysql_fetch_row(result);
            gb_offset = row[0];

            sprintf(queryBuf, "SELECT BYTEoffset from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
            mysql_query(con, queryBuf);
            result = mysql_use_result(con);
            row = mysql_fetch_row(result);
            b_offset = row[0];

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

                // Query the member file size
                printf(queryBuf, "SELECT MemberLength from UncompTar WHERE ArchiveName = '%s' AND MemberName = '%s'", tar_filename, membername);
                mysql_query(con, queryBuf);
                result = mysql_use_result(con);
                row = mysql_fetch_row(result);

                mem_length = strtoll(row[0], NULL, 8);
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


