/*
    Will accept a tar file name and print out the files in the archive as well as their
    byte offsets and sizes (in bytes).
    Can accept an optional -q or -Q argument to keep output to a minimum.
*/
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "common_functions.h"

// File handle error checking
// err_type: 1 = no extension, 2 = invalid extension
int ext_error(int err_type, char* error_string){
	if (err_type == 1){
		printf("ERROR: No file extension on: %s\n"
			"Archive must have an appropriate file extension\n"
			"----------------------------------\n"
			"- Uncompressed TAR   archive: .tar\n"
			"- Compressed   Bzip2 archive: .tar.bz2\n"
			"- Compressed   XZ    archive: .tar.xz, .txz\n"
			"----------------------------------\n", error_string);
		return 1;
	}
	else if (err_type == 2){
		printf("ERROR: Invalid file extension: %s\n"
			"Archive must have an appropriate file extension\n"
			"----------------------------------\n"
			"- Uncompressed TAR   archive: .tar\n"
			"- Compressed   Bzip2 archive: .tar.bz2\n"
			"- Compressed   XZ    archive: .tar.xz, .txz\n"
			"----------------------------------\n", error_string);
		return 1;
	}
	return 0;
}

int main(int argc, char* argv[]) {

	char* filename; 	// file to analyze
	char* file_handle;	// .xz, .bz2, ect.
	int problem_variable = 0;	// indicates error
	struct stat filestats;	// important info about the file being analyzed
	int show_output = 1;

	// Check syntax
	if(argc == 3) {
		if(strcmp(argv[2], "-q") == 0 || strcmp(argv[2], "-Q") == 0) {
			show_output = 0;
		}
	}
	else if (argc != 2){
		printf("ERROR: Syntax\n"
			"Expected: analyze_archive <archive file name> <optional -q or -Q quiet flag>\n");
		return 1;
	}
	filename = argv[1];
		
	//check read permission and get stats
	if(access(filename, R_OK) != 0) {
		printf("Error, file could not be accessed with read permission\n");
		return 1;
	}
	else {
		if(lstat(filename, &filestats) != 0) {
			printf("Error, getting statistics on file failed\n");
			return 1;
		}
	}

	// Set file extension
	file_handle = strrchr(filename, '.');
	if (!file_handle) {
		return ext_error(1, filename);
	} 
	else {
		file_handle = file_handle + 1;
		// Save file extension. Validity will be checked later
	}

	// Uncompressed tar archive
	if(strcmp(file_handle, "tar") == 0) {
		if(show_output) printf("ANALYZING %s ARCHIVE\n", file_handle);
		problem_variable = analyze_tar(filename, filestats, show_output);
	}
	// bzip2
	else if(strcmp(file_handle, "bz2") == 0) {
		// Ensure that this is a tar.bz2, not just .bz2
		file_handle = strrchr(filename, '.') - 3;
		if(strcmp(file_handle, "tar.bz2") == 0) {
			if(show_output) printf("ANALYZING %s ARCHIVE\n", file_handle);
			problem_variable = analyze_bz2(filename, filestats, show_output);
		}
		else return ext_error(2, filename);
	}
	// xz
	else if(strcmp(file_handle, "xz") == 0 || strcmp(file_handle, "txz") == 0) {
		file_handle = strrchr(filename, '.') - 3;
		if(strcmp(file_handle, "tar.xz") == 0 || strcmp(file_handle, "txz") == 0) {
			if(show_output) printf("ANALYZING %s ARCHIVE\n", file_handle);
			problem_variable = analyze_xz(filename, filestats, show_output);
		}
		else return ext_error(2, filename);
	}
	else {
		return ext_error(2, filename);
	}

	return problem_variable;
}
