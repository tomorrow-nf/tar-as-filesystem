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

	// Check syntax
	if (argc != 2){
		printf("ERROR: Syntax\n"
			"Expected: analyze_archive <archive file name>\n");
		return 1;
	}

	// Set file extension
	file_handle = strrchr(filename, '.');
	if (!file_handle) {
		printf("ERROR: No file extension on: %s\n"
			"Archive must have an appropriate file extension\n"
			"----------------------------------\n"
			"- Uncompressed TAR   archive: .tar\n"
			"- Compressed   Bzip2 archive: .bz2\n"
			"- Compressed   Gzip  archive: .gz\n"
			"- Compressed   XZ    archive: .xz\n"
			"----------------------------------\n", filename);
		return 1;
	} 
	else {
		file_handle = file_handle + 1;
		// Save file extension. Validity will be checked later
	}

	// Uncompressed tar archive
	if(strcmp(file_handle, "tar") == 0) {
		problem_variable = analyze_tar(filename);
	}
	else if(strcmp(file_handle, "bz2") == 0) {
		problem_variable = analyze_bz2(filename);
	}
	/*
	else if(strcmp(file_handle, "gz") == 0) {
		problem_variable = analyze_gzip(filename);
	}
	else if(strcmp(file_handle, "xz") == 0) {
		problem_variable = analyze_xz(filename);
	}
	*/
	else {
		printf("ERROR: Invalid file extension: %s\n"
			"Archive must have an appropriate file extension\n"
			"----------------------------------\n"
			"- Uncompressed TAR   archive: .tar\n"
			"- Compressed   Bzip2 archive: .bz2\n"
			"- Compressed   Gzip  archive: .gz\n"
			"- Compressed   XZ    archive: .xz\n"
			"----------------------------------\n", file_handle);
		return 1;
	}

	return problem_variable;
}
