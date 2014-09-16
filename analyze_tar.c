/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {

    FILE* tarfile;
    long int bytes_read = 0; //running count of bytes read, will be used as offset
    char* file_handle;
    char* filename = "testarchive.tar"; //temporary will equal argv[1] later

    //Tar file important info
    //TODO define buffers here

    //TODO set file_handle

    if(strcmp(file_handle, ".tar") == 0) {
        tarfile = fopen(filename, "r");
        if(!tarfile) {
            printf("Unable to open file");
        }
        else {
            //TODO discard file header of archive file
            fread(&buffer, sizeof(buffer), 1, tarfile);
            bytes_read = bytes_read + sizeof(buffer);

            printf("file offset: %ld\n", bytes_read);
            //TODO read tar header data into buffers and print
            fread(&buffer, sizeof(buffer), 1, tarfile);
            printf("buffer should be in ascii and printable");
            bytes_read = bytes_read + sizeof(buffer);

            //TODO skip data
            //SEEK_CUR = current position macro, already defined
            fseek(tarfile, Length_of_data, SEEK_CUR);
        }
    }
    else {
        //TODO error
    }
}
