#ifndef LIST_XZFILE_H
#define LIST_XZFILE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <lzma.h>
#include <sys/stat.h>

#define XZ_FILE_INFO_INIT { NULL, 0, 0, true }
#define FILTERS_STR_SIZE 512

#define bool int
#define true 1
#define false 0

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


/// \brief      Initialize the I/O module
extern void io_init(void);


/// \brief      Disable creation of sparse files when decompressing
extern void io_no_sparse(void);


/// \brief      Open the source file
extern file_pair *io_open_src(const char *src_name);


/// \brief      Open the destination file
extern bool io_open_dest(file_pair *pair);


/// \brief      Closes the file descriptors and frees possible allocated memory
///
/// The success argument determines if source or destination file gets
/// unlinked:
///  - false: The destination file is unlinked.
///  - true: The source file is unlinked unless writing to stdout or --keep
///    was used.
extern void io_close(file_pair *pair, bool success);


/// \brief      Reads from the source file to a buffer
///
/// \param      pair    File pair having the source file open for reading
/// \param      buf     Destination buffer to hold the read data
/// \param      size    Size of the buffer; assumed be smaller than SSIZE_MAX
///
/// \return     On success, number of bytes read is returned. On end of
///             file zero is returned and pair->src_eof set to true.
///             On error, SIZE_MAX is returned and error message printed.
extern size_t io_read(file_pair *pair, io_buf *buf, size_t size);


/// \brief      Read from source file from given offset to a buffer
///
/// This is remotely similar to standard pread(). This uses lseek() though,
/// so the read offset is changed on each call.
///
/// \param      pair    Seekable source file
/// \param      buf     Destination buffer
/// \param      size    Amount of data to read
/// \param      pos     Offset relative to the beginning of the file,
///                     from which the data should be read.
///
/// \return     On success, false is returned. On error, error message
///             is printed and true is returned.
extern bool io_pread(file_pair *pair, io_buf *buf, size_t size, off_t pos);


/// \brief      Writes a buffer to the destination file
///
/// \param      pair    File pair having the destination file open for writing
/// \param      buf     Buffer containing the data to be written
/// \param      size    Size of the buffer; assumed be smaller than SSIZE_MAX
///
/// \return     On success, zero is returned. On error, -1 is returned
///             and error message printed.
extern bool io_write(file_pair *pair, const io_buf *buf, size_t size);

///////////////////////////////////////////////////////////////////////////////
//
/// \file       coder.h
/// \brief      Compresses or uncompresses a file
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

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


/// Operation mode of the command line tool. This is set in args.c and read
/// in several files.
extern enum operation_mode opt_mode;

/// File format to use when encoding or what format(s) to accept when
/// decoding. This is a global because it's needed also in suffix.c.
/// This is set in args.c.
extern enum format_type opt_format;

/// If true, the compression settings are automatically adjusted down if
/// they exceed the memory usage limit.
extern bool opt_auto_adjust;


/// Set the integrity check type used when compressing
extern void coder_set_check(lzma_check check);

/// Set preset number
extern void coder_set_preset(uint32_t new_preset);

/// Enable extreme mode
extern void coder_set_extreme(void);

/// Add a filter to the custom filter chain
extern void coder_add_filter(lzma_vli id, void *options);

///
extern void coder_set_compression_settings(void);

/// Compress or decompress the given file
extern void coder_run(const char *filename);



///////////////////////////////////////////////////////////////////////////////
//
/// \file       message.h
/// \brief      Printing messages to stderr
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

/// Verbosity levels
enum message_verbosity {
	V_SILENT,   ///< No messages
	V_ERROR,    ///< Only error messages
	V_WARNING,  ///< Errors and warnings
	V_VERBOSE,  ///< Errors, warnings, and verbose statistics
	V_DEBUG,    ///< Very verbose
};


/// \brief      Signals used for progress message handling
extern const int message_progress_sigs[];


/// \brief      Initializes the message functions
///
/// If an error occurs, this function doesn't return.
///
extern void message_init(void);


/// Increase verbosity level by one step unless it was at maximum.
extern void message_verbosity_increase(void);

/// Decrease verbosity level by one step unless it was at minimum.
extern void message_verbosity_decrease(void);

/// Get the current verbosity level.
extern enum message_verbosity message_verbosity_get(void);


/// \brief      Print a message if verbosity level is at least "verbosity"
///
/// This doesn't touch the exit status.
extern void message(enum message_verbosity verbosity, const char *fmt, ...)
		lzma_attribute((__format__(__printf__, 2, 3)));


/// \brief      Prints a warning and possibly sets exit status
///
/// The message is printed only if verbosity level is at least V_WARNING.
/// The exit status is set to WARNING unless it was already at ERROR.
extern void message_warning(const char *fmt, ...)
		lzma_attribute((__format__(__printf__, 1, 2)));


/// \brief      Prints an error message and sets exit status
///
/// The message is printed only if verbosity level is at least V_ERROR.
/// The exit status is set to ERROR.
extern void message_error(const char *fmt, ...)
		lzma_attribute((__format__(__printf__, 1, 2)));


/// \brief      Prints an error message and exits with EXIT_ERROR
///
/// The message is printed only if verbosity level is at least V_ERROR.
extern void message_fatal(const char *fmt, ...)
		lzma_attribute((__format__(__printf__, 1, 2)))
		lzma_attribute((__noreturn__));


/// Print an error message that an internal error occurred and exit with
/// EXIT_ERROR.
extern void message_bug(void) lzma_attribute((__noreturn__));


/// Print a message that establishing signal handlers failed, and exit with
/// exit status ERROR.
extern void message_signal_handler(void) lzma_attribute((__noreturn__));


/// Convert lzma_ret to a string.
extern const char *message_strm(lzma_ret code);


/// Display how much memory was needed and how much the limit was.
extern void message_mem_needed(enum message_verbosity v, uint64_t memusage);


/// Buffer size for message_filters_to_str()
#define FILTERS_STR_SIZE 512


/// \brief      Get the filter chain as a string
///
/// \param      buf         Pointer to caller allocated buffer to hold
///                         the filter chain string
/// \param      filters     Pointer to the filter chain
/// \param      all_known   If true, all filter options are printed.
///                         If false, only the options that get stored
///                         into .xz headers are printed.
extern void message_filters_to_str(char buf[FILTERS_STR_SIZE],
		const lzma_filter *filters, bool all_known);


/// Print the filter chain.
extern void message_filters_show(
		enum message_verbosity v, const lzma_filter *filters);


/// Print a message that user should try --help.
extern void message_try_help(void);


/// Prints the version number to stdout and exits with exit status SUCCESS.
extern void message_version(void) lzma_attribute((__noreturn__));


/// Print the help message.
extern void message_help(bool long_help) lzma_attribute((__noreturn__));


/// \brief      Set the total number of files to be processed
///
/// Standard input is counted as a file here. This is used when printing
/// the filename via message_filename().
extern void message_set_files(unsigned int files);


/// \brief      Set the name of the current file and possibly print it too
///
/// The name is printed immediately if --list was used or if --verbose
/// was used and stderr is a terminal. Even when the filename isn't printed,
/// it is stored so that it can be printed later if needed for progress
/// messages.
extern void message_filename(const char *src_name);


/// \brief      Start progress info handling
///
/// message_filename() must be called before this function to set
/// the filename.
///
/// This must be paired with a call to message_progress_end() before the
/// given *strm becomes invalid.
///
/// \param      strm      Pointer to lzma_stream used for the coding.
/// \param      in_size   Size of the input file, or zero if unknown.
///
extern void message_progress_start(lzma_stream *strm, uint64_t in_size);


/// Update the progress info if in verbose mode and enough time has passed
/// since the previous update. This can be called only when
/// message_progress_start() has already been used.
extern void message_progress_update(void);


/// \brief      Finishes the progress message if we were in verbose mode
///
/// \param      finished    True if the whole stream was successfully coded
///                         and output written to the output stream.
///
extern void message_progress_end(bool finished);


#endif
