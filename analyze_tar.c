/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "common_functions.h"

#define ENTRYNAMESIZE (sizeof(char) * 100)
#define FILELENGTHFIELDSIZE (sizeof(char) * 12)
#define USTARFIELDSIZE (sizeof(char) * 8)
#define PREFIXSIZE (sizeof(char) * 155)

int main(int argc, char* argv[]) {

    FILE* tarfile;
    int GB_read = 0;         		// number of gigabytes read so far
    long int bytes_read = 0; 		// total bytes read - (gigabytes read * bytes per gigabyte)
    char* tar_file_handle;  		// file type (tar, bz2, gz, xz)
    char* tar_filename = argv[1]; 	// file to analyze
    long int longtmp; 				// temporary variable for calculations
    long long int longlongtmp; 		// temporary variable for calculations

    //create end of archive check
    char archive_end_check[1024];
    char archive_end[1024];
    memset(archive_end, 0, sizeof(archive_end));

    //Tar file important info
    char* entryname = (char*) malloc(ENTRYNAMESIZE);                 // name of entry file
    char* file_length_string = (char*) malloc(FILELENGTHFIELDSIZE);  // size of file in bytes (string)
    long long int file_length;                                       // size of file in bytes (long int)
    void* trashbuffer = (void*) malloc(sizeof(char) * 200);          // for unused fields
    char link_flag;                                                  // flag indicating this is a file link
    char* linkname = (char*) malloc(ENTRYNAMESIZE);                  // name of linked file
    char* ustarflag = (char*) malloc(USTARFIELDSIZE);                // field indicating newer ustar format
    char* entryprefix = (char*) malloc(PREFIXSIZE);                  // ustar includes a field for long names

    // Set file extension
	tar_file_handle = strrchr(tar_filename, '.');
	if (!tar_file_handle) {
	    //TODO error if no extension given
	    return 1;
	} 
	else {
		tar_file_handle = tar_file_handle + 1;
		// Save file extension. Validity will be checked later
	}

	// Uncompressed tar archive
    if(strcmp(tar_file_handle, "tar") == 0) {
        tarfile = fopen(tar_filename, "r");
        if(!tarfile) {
            printf("Unable to open file: %s\n", tar_filename);
        }
        else {
            while(1) {
            	// Evaluate the tar header
                printf("entry header offset: %d GB and %ld bytes\n", GB_read, bytes_read);

                //get filename
                fread((void*)entryname, ENTRYNAMESIZE, 1, tarfile);
                printf("entry name: %s\n", entryname);
                bytes_read = bytes_read + ENTRYNAMESIZE;

                //discard mode, uid, and gid (8 bytes each)
                fread((void*)trashbuffer, (sizeof(char) * 24), 1, tarfile);
                bytes_read = bytes_read + (sizeof(char) * 24);

                //get length of file in bytes
                fread((void*)file_length_string, FILELENGTHFIELDSIZE, 1, tarfile);
                printf("file length string: %s\n", file_length_string);
                file_length = strtolonglong(file_length_string);
                printf("file length int: %lld\n", file_length);
                bytes_read = bytes_read + FILELENGTHFIELDSIZE;

                //discard modify time and checksum (20 bytes)
                fread((void*)trashbuffer, (sizeof(char) * 20), 1, tarfile);
                bytes_read = bytes_read + (sizeof(char) * 20);

                //get link flag (1 byte)
                fread((void*)(&link_flag), sizeof(char), 1, tarfile);
                printf("link flag: %c\n", link_flag);
                bytes_read = bytes_read + sizeof(char);

                //get linked filename (if flag set, otherwise this field is useless)
                fread((void*)linkname, ENTRYNAMESIZE, 1, tarfile);
                printf("link name: %s\n", linkname);
                bytes_read = bytes_read + ENTRYNAMESIZE;

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
                    fread((void*)entryprefix, PREFIXSIZE, 1, tarfile);
                    printf("file prefix: %s\n", entryprefix);
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
            
                //skip data
                //SEEK_CUR = current position macro, already defined
                if(file_length > BYTES_IN_GB) {
                    longlongtmp = file_length;
                    while(longlongtmp > BYTES_IN_GB) {
                        fseek(tarfile, BYTES_IN_GB, SEEK_CUR);
                        GB_read = GB_read + 1;
                        longlongtmp = longlongtmp - BYTES_IN_GB;
                    }
                    longtmp = longlongtmp + (512 - (longlongtmp % 512));
                    fseek(tarfile, longtmp, SEEK_CUR);
                    bytes_read = bytes_read + longtmp;
                }
                else if(file_length == 0) {
                    // do not skip any data
                }
                else {
                    longtmp = file_length + (512 - (file_length % 512));
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
        }
    }
    else {
        //TODO error
    }
}
