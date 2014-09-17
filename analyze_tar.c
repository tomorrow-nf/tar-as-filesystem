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
#define BYTES_IN_GB 1073741824

int main(int argc, char* argv[]) {

    FILE* tarfile;
    int GB_read = 0;         // number of gigabytes read so far
    long int bytes_read = 0; // total bytes read - (gigabytes read * bytes per gigabyte)
    char* tar_file_handle;   // the end of the filename (.tar .bz .xz .docx)
    char* tar_filename = "testarchive.tar"; //temporary, will equal argv[1] later
    long int tmp; // temporary variable for calculations

    //Tar file important info
    char* entryname = (char*) malloc(ENTRYNAMESIZE);                 // name of entry file
    char* file_length_string = (char*) malloc(FILELENGTHFIELDSIZE);  // size of file in bytes (string)
    long long int file_length;                                       // size of file in bytes (long int)
    void* trashbuffer = (void*) malloc(sizeof(char) * 200);          // for unused fields
    char link_flag;                                                  // flag indicating this is a file link
    char* linkname = (char*) malloc(ENTRYNAMESIZE);                  // name of linked file
    char* ustarflag = (char*) malloc(USTARFIELDSIZE);                // field indicating newer ustar format
    char* entryprefix = (char*) malloc(PREFIXSIZE);                  // ustar includes a field for long names

    //TODO set tar_file_handle
    tar_file_handle = ".tar";

    if(strcmp(tar_file_handle, ".tar") == 0) {
        tarfile = fopen(tar_filename, "r");
        if(!tarfile) {
            printf("Unable to open file");
        }
        else {
            printf("entry header offset: %d GB and %ld\n", GB_read, bytes_read);

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

            //skip rest of 512 byte block
            tmp = 512 - (bytes_read % 512); //get bytes left till end of block
            if(tmp != 0) {
                fseek(tarfile, tmp, SEEK_CUR);
                bytes_read = bytes_read + tmp;
            }

            //print beginning point of data
            printf("data begins at %d GB and %ld bytes\n", GB_read, bytes_read);

            fread((void*)trashbuffer, (sizeof(char) * 17), 1, tarfile);
            printf("file contents: %s\n", (char*)trashbuffer);
            

            //skip data
            //SEEK_CUR = current position macro, already defined
            //TODO 
            //fseek(tarfile, Length_of_data, SEEK_CUR);
        }
    }
    else {
        //TODO error
    }
}
