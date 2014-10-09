#ifndef BITMAPSTRUCTS_H
#define BITMAPSTRUCTS_H

struct blockmap {
	int maxsize;
	struct blocklocation* blocklocations;
};

struct blocklocation {
	int GB;
	long long int bytes;
	long long int bits;
};

#endif
