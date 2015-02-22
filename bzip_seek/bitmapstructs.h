#ifndef BITMAPSTRUCTS_H
#define BITMAPSTRUCTS_H

struct blockmap {
	int maxsize;
	struct blocklocation* blocklocations;
};

struct blocklocation {
	unsigned long long position;
	unsigned long long uncompressedSize;
};

int map_bzip2(char* filename, struct blockmap* offsets, int show_output);

int uncompressblock( char* filename, unsigned long long position, void* buf );

int uncompressfile( int src_fd, int dst_fd, unsigned long long position, long int offset, long long int memlength );

#endif
