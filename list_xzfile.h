#ifndef LIST_XZFILE_H
#define LIST_XZFILE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <inttypes.h>
//#include <errno.h>
#include <lzma.h>
#include <sys/stat.h>

#define XZ_FILE_INFO_INIT { NULL, 0, 0, true }
#define FILTERS_STR_SIZE 512

#define bool int
#define true 1
#define false 0

#define my_min(x, y) ((x) < (y) ? (x) : (y))
#define my_max(x, y) ((x) > (y) ? (x) : (y))

#define O_BINARY 0


int user_abort = false;

// Compilation of headers needed for parse_index()

// According to Lasse Collin, this function will be deprecated
// within a year or two, and its functionality will be included
// in the API instead. THIS MUST BE UPDATED ONCE THIS HAPPENS
// IN ORDER TO ACCOMMODATE NEWER XZ FILES.


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

//List information about the given .xz file
extern void list_file(const char *filename);

//Show the totals after all files have been listed
extern void list_totals(void);


// Some systems have suboptimal BUFSIZ. Use a bit bigger value on them.
// We also need that IO_BUFFER_SIZE is a multiple of 8 (sizeof(uint64_t))
#if BUFSIZ <= 1024
#	define IO_BUFFER_SIZE 8192
#else
#	define IO_BUFFER_SIZE (BUFSIZ & ~7U)
#endif


/// is_sparse() accesses the buffer as uint64_t for maximum speed.
/// Use an union to make sure that the buffer is properly aligned.
typedef union {
	uint8_t u8[IO_BUFFER_SIZE];
	uint32_t u32[IO_BUFFER_SIZE / sizeof(uint32_t)];
	uint64_t u64[IO_BUFFER_SIZE / sizeof(uint64_t)];
} io_buf;


typedef struct {
	/// Name of the source filename (as given on the command line) or
	/// pointer to static "(stdin)" when reading from standard input.
	const char *src_name;

	/// Destination filename converted from src_name or pointer to static
	/// "(stdout)" when writing to standard output.
	char *dest_name;

	/// File descriptor of the source file
	int src_fd;

	/// File descriptor of the target file
	int dest_fd;

	/// True once end of the source file has been detected.
	bool src_eof;

	/// If true, we look for long chunks of zeros and try to create
	/// a sparse file.
	bool dest_try_sparse;

	/// This is used only if dest_try_sparse is true. This holds the
	/// number of zero bytes we haven't written out, because we plan
	/// to make that byte range a sparse chunk.
	off_t dest_pending_sparse;

	/// Stat of the source file.
	struct stat src_st;

	/// Stat of the destination file.
	struct stat dest_st;

} file_pair;


bool io_pread(file_pair *pair, io_buf *buf, size_t size, off_t pos);

size_t
io_read(file_pair *pair, io_buf *buf_union, size_t size);

enum operation_mode {
	MODE_COMPRESS,
	MODE_DECOMPRESS,
	MODE_TEST,
	MODE_LIST,
};


// NOTE: The order of these is significant in suffix.c.
enum format_type {
	FORMAT_AUTO,
	FORMAT_XZ,
	FORMAT_LZMA,
	// HEADER_GZIP,
	FORMAT_RAW,
};


/// Verbosity levels
enum message_verbosity {
	V_SILENT,   ///< No messages
	V_ERROR,    ///< Only error messages
	V_WARNING,  ///< Errors and warnings
	V_VERBOSE,  ///< Errors, warnings, and verbose statistics
	V_DEBUG,    ///< Very verbose
};


/// Memory usage limit for compression
static uint64_t memlimit_compress;

/// Memory usage limit for decompression
static uint64_t memlimit_decompress;

uint64_t hardware_memlimit_get(enum operation_mode mode);
file_pair* io_open_src(const char *src_name);
bool is_empty_filename(const char *filename);

const char stdin_filename[] = "(stdin)";
bool opt_stdout = false;
bool opt_force = false; 
#define STDIN_FILENO (fileno(stdin))





#endif
