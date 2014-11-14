/*
	Standalone version of the list_file function in src/xz/list.c
	- import any structures into list_xzfile.h
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <lzma.h>
#include "list_xzfile.h"

int main(int argc, char* argv[]) {
	char* filename = argv[1]; 	// file to analyze
	char* file_handle;
	FILE* XZfile;

	// Set file extension
	file_handle = strrchr(filename, '.');
	if (!file_handle) {
		return 1;
	} 
	else {
		file_handle = file_handle + 1;
		// Save file extension. Validity will be checked later
	}

	if(strcmp(file_handle, "xz") != 0) {
		return 1;
	}

	//we have an xz file
	XZfile = fopen(XZfile, "r");
	if(!XZfile) {
		printf("Unable to open file: %s\n", filename);
		return 1;
	}

	xz_file_info xfi = XZ_FILE_INFO_INIT;
	parse_indexes(&xfi, XZfile);

	//TODO more after parse_indexes is written
}

bool parse_indexes(xz_file_info *xfi, FILE* XZfile) {
	//TODO write parse_indexes without the interdependencies of xz
}
