#ifndef BITMAPSTRUCTS_H
#define BITMAPSTRUCTS_H

struct blockmap {
	int maxsize;
	struct blocklocation* blocklocations;
};

struct blocklocation {
	unsigned long long position;
	int uncompressedSize;
};

int map_bzip2(char* filename, struct blockmap* offsets);

int uncompressblock( char* filename, unsigned long long position, void* buf );

#endif
