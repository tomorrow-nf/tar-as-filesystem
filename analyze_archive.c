/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
*/
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "common_functions.h"

int main(int argc, char* argv[]) {

	char* filename = argv[1]; 	// file to analyze
	char* file_handle;
	int problem_variable;

	// Set file extension
	file_handle = strrchr(filename, '.');
	if (!file_handle) {
		printf("No file extention found\n");
		return 1;
	} 
	else {
		file_handle = file_handle + 1;
		// Save file extension. Validity will be checked later
	}

	// Uncompressed tar archive
	if(strcmp(file_handle, "tar") == 0) {
		problem_variable = analyze_tarfile(filename);
	}
	//else if(strcmp(tar_file_handle, "bz") == 0)
}
