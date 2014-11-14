#ifndef LIST_XZFILE_H
#define LIST_XZFILE_H

#include <lzma.h>
#include <stdint.h>


#define XZ_FILE_INFO_INIT { NULL, 0, 0, true }
#define FILTERS_STR_SIZE 512

/// Information about a .xz file
typedef struct {
	/// Combined Index of all Streams in the file
	lzma_index *idx;

	/// Total amount of Stream Padding
	uint64_t stream_padding;

	/// Highest memory usage so far
	uint64_t memusage_max;

	/// True if all Blocks so far have Compressed Size and
	/// Uncompressed Size fields
	bool all_have_sizes;

} xz_file_info;


/// Information about a .xz Block
typedef struct {
	/// Size of the Block Header
	uint32_t header_size;

	/// A few of the Block Flags as a string
	char flags[3];

	/// Size of the Compressed Data field in the Block
	lzma_vli compressed_size;

	/// Decoder memory usage for this Block
	uint64_t memusage;

	/// The filter chain of this Block in human-readable form
	char filter_chain[FILTERS_STR_SIZE];

} block_header_info;

#endif
