#ifndef BITMAPSTRUCTS_H
#define BITMAPSTRUCTS_H

struct blockmap {
	int maxsize;
	struct blocklocation* blocklocations;
};

struct blocklocation {
	int GB;
	int bytes;
	int bits;
	int uncompressedSize;
};

#endif
