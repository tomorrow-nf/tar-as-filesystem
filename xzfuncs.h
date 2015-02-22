#ifndef XZFUNCS_H
#define XZFUNCS_H

#include "bzip_seek/bitmapstructs.h" //this is just for the blockmap struct

void* grab_block(int blocknum, char* filename);

int fill_bitmap(char* filename, struct blockmap* offsets, int show_output);

#endif
