/*
	Standalone version of the list_file function in src/xz/list.c
	- import any structures into list_xzfile.h
*/

#include <errno.h>
#include <assert.h>
#include "bzip_seek/bitmapstructs.h"
#include "list_xzfile.h"
#include "xzfuncs.h"

/*
int main(int argc, char* argv[]) {
	/*struct blockmap* block_offsets = (struct blockmap*) malloc(sizeof(struct blockmap));
	block_offsets->blocklocations = (struct blocklocation*) malloc(sizeof(struct blocklocation) * 200);
	block_offsets->maxsize = 200;
	fill_bitmap(argv[1], block_offsets);*/
/*
	void* reg_file = malloc(20480);
	int reg_int = open("test/UNCOMPRESSEDtestarchive.tar", O_RDONLY);
	read(reg_int, reg_file, 20480);
	close(reg_int);

	void* xzfile = grab_block(1, "test/UNCOMPRESSEDtestarchive.tar.xz");

	if (xzfile == NULL){
		printf("ERROR: Null xzfile, aborting\n");
		return 1;
	}

	else if (memcmp(reg_file, xzfile, 20480) != 0){
		printf("ERROR: the two files did not match\n");
		int xzfd = open("test/uncmped.tar", O_CREAT | O_RDWR);
		write(xzfd, xzfile, 20480);
		close(xzfd);
	}
	else printf("SUCCESS\n");

	return 0;
}*/

// uncompresses block number "blocknum" into buffer "buf"
void* grab_block(int blocknum, char* filename) {
	file_pair *pair = io_open_src(filename);
	if (pair == NULL) {
		printf("ERROR: File not found\n");
		return 0;
	}

	xz_file_info xfi = XZ_FILE_INFO_INIT;
	parse_indexes(&xfi, pair);

	//create iterator and move to desired block
	lzma_index_iter iter;
	lzma_index_iter_init(&iter, xfi.idx);
	lzma_index_iter_rewind(&iter);

	while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
		if(iter.block.number_in_stream != iter.block.number_in_file) {
			close(pair->src_fd);
			return NULL; //TODO: Error
		}
		if(iter.block.number_in_stream == blocknum) {
			break; //iterator points to the block we want
		}
	}

	uint8_t* in_buf = (uint8_t*) malloc(iter.block.total_size);
	uint8_t* out_buf = (uint8_t*) malloc(iter.block.uncompressed_size);
	uint8_t buf[LZMA_BLOCK_HEADER_SIZE_MAX];

	//printf("DEBUG: in_buf (iter.block.total_size) size = %llu\n", iter.block.total_size);
	//printf("DEBUG: out_buf size = %llu\n", iter.block.uncompressed_size);

	lseek(pair->src_fd, iter.block.compressed_file_offset, SEEK_SET);
	read(pair->src_fd, in_buf, iter.block.total_size);

	lzma_block* this_block = (lzma_block*) malloc(sizeof(lzma_block));
	char block_begin;
	memcpy(&block_begin, in_buf, sizeof(char));

	if(iter.stream.flags == NULL) {
		printf("ERROR: there are NO stream flags\n");
		free(in_buf);
		free(this_block->filters); //malloc'ed in parse_block_header but needed in this function
		free(this_block);
		close(pair->src_fd);
		return NULL;
	}

	// Decode this block
	if (parse_block_header(this_block, pair, &iter)){
		printf("Error encountered decoding block header, aborting\n");
		free(in_buf);
		free(this_block->filters); //malloc'ed in parse_block_header but needed in this function
		free(this_block);
		close(pair->src_fd);
		return NULL;
	}

	int csizeret = lzma_block_compressed_size(this_block, iter.block.unpadded_size);
	if (csizeret != LZMA_OK){
		// TODO: Detailed error checking
		printf("Error code %d encountered while decoding block compressed size, aborting\n", csizeret);
		free(in_buf);
		free(this_block->filters); //malloc'ed in parse_block_header but needed in this function
		free(this_block);
		close(pair->src_fd);
		return NULL;
	}

	size_t in_pos = this_block->header_size; 
	size_t out_pos = 0;
	size_t in_size = iter.block.total_size;
	size_t out_size = iter.block.uncompressed_size;

	/* FOR OUR OWN REFERENCE
	* See base.h for details
	* LZMA_OK = 0,
	* LZMA_STREAM_END = 1,
	* LZMA_NO_CHECK = 2,
	* LZMA_UNSUPPORTED_CHECK = 3,
	* LZMA_GET_CHECK = 4,
	* LZMA_MEM_ERROR = 5,
	* LZMA_MEMLIMIT_ERROR = 6,
	* LZMA_FORMAT_ERROR = 7,
	* LZMA_OPTIONS_ERROR = 8,
	* LZMA_DATA_ERROR = 9,
	* LZMA_BUF_ERROR = 10,
	* LZMA_PROG_ERROR = 11,
	*/

	int lzmaret = lzma_block_buffer_decode(this_block, NULL, in_buf, &in_pos, in_size, out_buf, &out_pos, out_size);

	if (lzmaret != LZMA_OK){
		// TODO: Detailed error checking
		printf("Error code %d encountered while decoding block body, aborting\n", lzmaret);
		free(in_buf);
		free(this_block->filters); //malloc'ed in parse_block_header but needed in this function
		free(this_block);
		close(pair->src_fd);
		return NULL;
	}

	//close file
	close(pair->src_fd);

	//free malloc'ed stuff
	free(in_buf);
	free(this_block->filters); //malloc'ed in parse_block_header but needed in this function
	free(this_block);

	// return the uncompressed data
	return (void*) out_buf;
}

int fill_bitmap(char* filename, struct blockmap* offsets, int show_output) {

	file_pair *pair = io_open_src(filename);
	if (pair == NULL) {
		return 0;
	}

	xz_file_info xfi = XZ_FILE_INFO_INIT;
	parse_indexes(&xfi, pair);

	unsigned long long int streams = lzma_index_stream_count(xfi.idx);
	unsigned long long int number_of_blocks = lzma_index_block_count(xfi.idx);
	unsigned long long int uncomprsize = lzma_index_uncompressed_size(xfi.idx);
	if(show_output) {
		printf("streams: %llu\n", streams);
		printf("blocks : %llu\n", number_of_blocks);
		printf("unCsize: %llu\n\n", uncomprsize);
	}

	// if streams is not 1 error
	if(streams != 1) {
		return 1;
	}

	//create iterator
	lzma_index_iter iter;
	lzma_index_iter_init(&iter, xfi.idx);
	lzma_index_iter_rewind(&iter);

	while (!lzma_index_iter_next(&iter, LZMA_INDEX_ITER_BLOCK)) {
		if(iter.block.number_in_stream != iter.block.number_in_file) {
			return 1; //WE GOT PROBLEMS
		}
		unsigned long long int blockno = iter.block.number_in_stream;
		unsigned long long int uncompsz = iter.block.uncompressed_size;
		if(show_output) {
			printf("block  : %llu\n", blockno);
			printf("unCsize: %llu\n", uncompsz);
		}

		if((offsets->maxsize - 10) <= blockno) {
			offsets->maxsize = (offsets->maxsize * 2);
			offsets->blocklocations = (struct blocklocation*) realloc(offsets->blocklocations, (offsets->maxsize * sizeof(struct blocklocation)));
		}

		((offsets->blocklocations)[blockno]).position = 0;
		((offsets->blocklocations)[blockno]).uncompressedSize = uncompsz;
	}

	close(pair->src_fd);
	return 0;
}


// The functions below are imported from the XZ source code (list.c) by Lasse Collin.
// For now, Lasse suggested we import these functions to use them, but in the future,
// THIS WILL NO LONGER WORK. Instead, the functionality will be built into the API
// and if this project is maintained, the XZ handling will likely need to be done over
// from the beginning.

bool parse_indexes(xz_file_info *xfi, file_pair *pair) {
	//TODO write parse_indexes without the interdependencies of xz
	if (pair->src_st.st_size <= 0) {
		//message_error(_("%s: File is empty"), pair->src_name);
		return true;
	}

	if (pair->src_st.st_size < 2 * LZMA_STREAM_HEADER_SIZE) {
		//message_error(_("%s: Too small to be a valid .xz file"),
			//pair->src_name);
		return true;
	}

	io_buf buf;
	lzma_stream_flags header_flags;
	lzma_stream_flags footer_flags;
	lzma_ret ret;

	// lzma_stream for the Index decoder
	lzma_stream strm = LZMA_STREAM_INIT;

	// All Indexes decoded so far
	lzma_index *combined_index = NULL;

	// The Index currently being decoded
	lzma_index *this_index = NULL;

	// Current position in the file. We parse the file backwards so
	// initialize it to point to the end of the file.
	off_t pos = pair->src_st.st_size;

	// Each loop iteration decodes one Index.
	do {
		// Check that there is enough data left to contain at least
		// the Stream Header and Stream Footer. This check cannot
		// fail in the first pass of this loop.
		if (pos < 2 * LZMA_STREAM_HEADER_SIZE) {
			//message_error("%s: %s", pair->src_name,
				//message_strm(LZMA_DATA_ERROR));
			goto error;
		}

		pos -= LZMA_STREAM_HEADER_SIZE;
		lzma_vli stream_padding = 0;

		// Locate the Stream Footer. There may be Stream Padding which
		// we must skip when reading backwards.
		while (true) {
			if (pos < LZMA_STREAM_HEADER_SIZE) {
				//message_error("%s: %s", pair->src_name,
					//message_strm(
						//LZMA_DATA_ERROR));
				goto error;
			}

			if (io_pread(pair, &buf,
				LZMA_STREAM_HEADER_SIZE, pos))
				goto error;

			// Stream Padding is always a multiple of four bytes.
			int i = 2;
			if (buf.u32[i] != 0)
				break;

			// To avoid calling io_pread() for every four bytes
			// of Stream Padding, take advantage that we read
			// 12 bytes (LZMA_STREAM_HEADER_SIZE) already and
			// check them too before calling io_pread() again.
			do {
				stream_padding += 4;
				pos -= 4;
				--i;
			} while (i >= 0 && buf.u32[i] == 0);
		}

		// Decode the Stream Footer.
		ret = lzma_stream_footer_decode(&footer_flags, buf.u8);
		if (ret != LZMA_OK) {
			//message_error("%s: %s", pair->src_name,
				//message_strm(ret));
			goto error;
		}

		// Check that the Stream Footer doesn't specify something
		// that we don't support. This can only happen if the xz
		// version is older than liblzma and liblzma supports
		// something new.
		//
		// It is enough to check Stream Footer. Stream Header must
		// match when it is compared against Stream Footer with
		// lzma_stream_flags_compare().
		if (footer_flags.version != 0) {
			//message_error("%s: %s", pair->src_name,
				//message_strm(LZMA_OPTIONS_ERROR));
			goto error;
		}

		// Check that the size of the Index field looks sane.
		lzma_vli index_size = footer_flags.backward_size;
		if ((lzma_vli)(pos) < index_size + LZMA_STREAM_HEADER_SIZE) {
			//message_error("%s: %s", pair->src_name,
				//message_strm(LZMA_DATA_ERROR));
			goto error;
		}

		// Set pos to the beginning of the Index.
		pos -= index_size;

		// See how much memory we can use for decoding this Index.
		uint64_t memlimit = hardware_memlimit_get(MODE_LIST);
		uint64_t memused = 0;
		if (combined_index != NULL) {
			memused = lzma_index_memused(combined_index);
			if (memused > memlimit)
				//message_bug();

			memlimit -= memused;
		}

		// Decode the Index.
		ret = lzma_index_decoder(&strm, &this_index, memlimit);
		if (ret != LZMA_OK) {
			//message_error("%s: %s", pair->src_name,
				//message_strm(ret));
			goto error;
		}

		do {
			// Don't give the decoder more input than the
			// Index size.
			strm.avail_in = my_min(IO_BUFFER_SIZE, index_size);
			if (io_pread(pair, &buf, strm.avail_in, pos))
				goto error;

			pos += strm.avail_in;
			index_size -= strm.avail_in;

			strm.next_in = buf.u8;
			ret = lzma_code(&strm, LZMA_RUN);

		} while (ret == LZMA_OK);

		// If the decoding seems to be successful, check also that
		// the Index decoder consumed as much input as indicated
		// by the Backward Size field.
		if (ret == LZMA_STREAM_END)
			if (index_size != 0 || strm.avail_in != 0)
				ret = LZMA_DATA_ERROR;

			if (ret != LZMA_STREAM_END) {
			// LZMA_BUFFER_ERROR means that the Index decoder
			// would have liked more input than what the Index
			// size should be according to Stream Footer.
			// The message for LZMA_DATA_ERROR makes more
			// sense in that case.
				if (ret == LZMA_BUF_ERROR)
					ret = LZMA_DATA_ERROR;

				//message_error("%s: %s", pair->src_name,
					//message_strm(ret));

			// If the error was too low memory usage limit,
			// show also how much memory would have been needed.
				if (ret == LZMA_MEMLIMIT_ERROR) {
					uint64_t needed = lzma_memusage(&strm);
					if (UINT64_MAX - needed < memused)
						needed = UINT64_MAX;
					else
						needed += memused;

					//message_mem_needed(V_ERROR, needed);
				}

				goto error;
			}

		// Decode the Stream Header and check that its Stream Flags
		// match the Stream Footer.
			pos -= footer_flags.backward_size + LZMA_STREAM_HEADER_SIZE;
			if ((lzma_vli)(pos) < lzma_index_total_size(this_index)) {
				//message_error("%s: %s", pair->src_name,
					//message_strm(LZMA_DATA_ERROR));
				goto error;
			}

			pos -= lzma_index_total_size(this_index);
			if (io_pread(pair, &buf, LZMA_STREAM_HEADER_SIZE, pos))
				goto error;

			ret = lzma_stream_header_decode(&header_flags, buf.u8);
			if (ret != LZMA_OK) {
				//message_error("%s: %s", pair->src_name,
					//message_strm(ret));
				goto error;
			}

			ret = lzma_stream_flags_compare(&header_flags, &footer_flags);
			if (ret != LZMA_OK) {
				//message_error("%s: %s", pair->src_name,
					//message_strm(ret));
				goto error;
			}

		// Store the decoded Stream Flags into this_index. This is
		// needed so that we can print which Check is used in each
		// Stream.
			ret = lzma_index_stream_flags(this_index, &footer_flags);
			if (ret != LZMA_OK)
				//message_bug();

		// Store also the size of the Stream Padding field. It is
		// needed to show the offsets of the Streams correctly.
			ret = lzma_index_stream_padding(this_index, stream_padding);
			if (ret != LZMA_OK)
				//message_bug();

			if (combined_index != NULL) {
			// Append the earlier decoded Indexes
			// after this_index.
				ret = lzma_index_cat(
					this_index, combined_index, NULL);
				if (ret != LZMA_OK) {
					//message_error("%s: %s", pair->src_name,
						//message_strm(ret));
					goto error;
				}
			}

			combined_index = this_index;
			this_index = NULL;

			xfi->stream_padding += stream_padding;

		} while (pos > 0);

		lzma_end(&strm);

	// All OK. Make combined_index available to the caller.
		xfi->idx = combined_index;
		return false;

		error:
	// Something went wrong, free the allocated memory.
		lzma_end(&strm);
		lzma_index_end(combined_index, NULL);
		lzma_index_end(this_index, NULL);
		return true;
}

/// \takes an newly created lzma block and fills it
bool parse_block_header(lzma_block* block, file_pair *pair, const lzma_index_iter *iter)
{
#if IO_BUFFER_SIZE < LZMA_BLOCK_HEADER_SIZE_MAX
#	error IO_BUFFER_SIZE < LZMA_BLOCK_HEADER_SIZE_MAX
#endif
	
	// Get the whole Block Header with one read, but don't read past
	// the end of the Block (or even its Check field).
	const uint32_t size = my_min(iter->block.total_size
				- lzma_check_size(iter->stream.flags->check),
			LZMA_BLOCK_HEADER_SIZE_MAX);
	io_buf buf;
	
	if (io_pread(pair, &buf, size, iter->block.compressed_file_offset))
		return true;

	// Zero would mean Index Indicator and thus not a valid Block.
	if (buf.u8[0] == 0)
		return true;

	// fill the block structure and decode Block Header Size.
	lzma_filter* filters = (lzma_filter*) malloc(sizeof(lzma_filter) * (LZMA_FILTERS_MAX + 1));
	block->version = 0;
	block->check = iter->stream.flags->check;
	block->filters = filters;

	block->header_size = lzma_block_header_size_decode(buf.u8[0]);

	// Decode the Block Header.
	switch (lzma_block_header_decode(block, NULL, buf.u8)) {
	case LZMA_OK:
		break;

	case LZMA_OPTIONS_ERROR:
		free(filters);
		return true;

	case LZMA_DATA_ERROR:
		free(filters);
		return true;

	default:
		free(filters);
		return true;
	}

	return false;
}

extern bool
io_pread(file_pair *pair, io_buf *buf, size_t size, off_t pos)
{
	// Using lseek() and read() is more portable than pread() and
	// for us it is as good as real pread().
	if (lseek(pair->src_fd, pos, SEEK_SET) != pos) {
		//message_error(_("%s: Error seeking the file: %s"),
				//pair->src_name, strerror(errno));
		return true;
	}

	const size_t amount = io_read(pair, buf, size);
	if (amount == SIZE_MAX)
		return true;

	if (amount != size) {
		//message_error(_("%s: Unexpected end of file"),
				//pair->src_name);
		return true;
	}

	return false;
}

size_t
io_read(file_pair *pair, io_buf *buf_union, size_t size)
{
	// We use small buffers here.
	assert(size < SSIZE_MAX);

	uint8_t *buf = buf_union->u8;
	size_t left = size;

	while (left > 0) {
		const ssize_t amount = read(pair->src_fd, buf, left);

		if (amount == 0) {
			pair->src_eof = true;
			break;
		}

		if (amount == -1) {
			if (errno == EINTR) {
				if (user_abort)
					return SIZE_MAX;

				continue;
			}

			//message_error(_("%s: Read error: %s"),
					//pair->src_name, strerror(errno));

			// FIXME Is this needed?
			pair->src_eof = true;

			return SIZE_MAX;
		}

		buf += (size_t)(amount);
		left -= (size_t)(amount);
	}

	return size - left;
}

uint64_t
hardware_memlimit_get(enum operation_mode mode)
{
	// Zero is a special value that indicates the default. Currently
	// the default simply disables the limit. Once there is threading
	// support, this might be a little more complex, because there will
	// probably be a special case where a user asks for "optimal" number
	// of threads instead of a specific number (this might even become
	// the default mode). Each thread may use a significant amount of
	// memory. When there are no memory usage limits set, we need some
	// default soft limit for calculating the "optimal" number of
	// threads.
	const uint64_t memlimit = mode == MODE_COMPRESS
			? memlimit_compress : memlimit_decompress;
	return memlimit != 0 ? memlimit : UINT64_MAX;
}

file_pair* io_open_src(const char *src_name){
	if (is_empty_filename(src_name))
		return NULL;

	// Since we have only one file open at a time, we can use
	// a statically allocated structure.
	static file_pair pair;

	pair = (file_pair){
		.src_name = src_name,
		.dest_name = NULL,
		.src_fd = -1,
		.dest_fd = -1,
		.src_eof = false,
		.dest_try_sparse = false,
		.dest_pending_sparse = 0,
	};

	const bool error = io_open_src_real(&pair);;

	return error ? NULL : &pair;
}

bool
is_empty_filename(const char *filename)
{
	if (filename[0] == '\0') {
		//message_error(_("Empty filename, skipping"));
		return true;
	}

	return false;
}

/// Opens the source file. Returns false on success, true on error.
bool
io_open_src_real(file_pair *pair)
{
	// There's nothing to open when reading from stdin.
	if (pair->src_name == stdin_filename) {
		pair->src_fd = STDIN_FILENO;
#ifdef TUKLIB_DOSLIKE
		setmode(STDIN_FILENO, O_BINARY);
#endif
		return false;
	}

	// Symlinks are not followed unless writing to stdout or --force
	// was used.
	const bool follow_symlinks = opt_stdout || opt_force;

	// We accept only regular files if we are writing the output
	// to disk too. bzip2 allows overriding this with --force but
	// gzip and xz don't.
	const bool reg_files_only = !opt_stdout;

	// Flags for open()
	int flags = O_RDONLY | O_BINARY | O_NOCTTY;

#ifndef TUKLIB_DOSLIKE
	// If we accept only regular files, we need to be careful to avoid
	// problems with special files like devices and FIFOs. O_NONBLOCK
	// prevents blocking when opening such files. When we want to accept
	// special files, we must not use O_NONBLOCK, or otherwise we won't
	// block waiting e.g. FIFOs to become readable.
	if (reg_files_only)
		flags |= O_NONBLOCK;
#endif

#if defined(O_NOFOLLOW)
	if (!follow_symlinks)
		flags |= O_NOFOLLOW;
#elif !defined(TUKLIB_DOSLIKE)
	// Some POSIX-like systems lack O_NOFOLLOW (it's not required
	// by POSIX). Check for symlinks with a separate lstat() on
	// these systems.
	if (!follow_symlinks) {
		struct stat st;
		if (lstat(pair->src_name, &st)) {
			message_error("%s: %s", pair->src_name,
					strerror(errno));
			return true;

		} else if (S_ISLNK(st.st_mode)) {
			message_warning(_("%s: Is a symbolic link, "
					"skipping"), pair->src_name);
			return true;
		}
	}
#else
	// Avoid warnings.
	(void)follow_symlinks;
#endif

	// Try to open the file. If we are accepting non-regular files,
	// unblock the caught signals so that open() can be interrupted
	// if it blocks e.g. due to a FIFO file.
	if (!reg_files_only)
		;//signals_unblock();

	// Maybe this wouldn't need a loop, since all the signal handlers for
	// which we don't use SA_RESTART set user_abort to true. But it
	// doesn't hurt to have it just in case.
	do {
		pair->src_fd = open(pair->src_name, flags);
	} while (pair->src_fd == -1 && errno == EINTR && !user_abort);

	if (!reg_files_only)
		;//signals_block();

	if (pair->src_fd == -1) {
		// If we were interrupted, don't display any error message.
		if (errno == EINTR) {
			// All the signals that don't have SA_RESTART
			// set user_abort.
			assert(user_abort);
			return true;
		}

#ifdef O_NOFOLLOW
		// Give an understandable error message if the reason
		// for failing was that the file was a symbolic link.
		//
		// Note that at least Linux, OpenBSD, Solaris, and Darwin
		// use ELOOP to indicate that O_NOFOLLOW was the reason
		// that open() failed. Because there may be
		// directories in the pathname, ELOOP may occur also
		// because of a symlink loop in the directory part.
		// So ELOOP doesn't tell us what actually went wrong,
		// and this stupidity went into POSIX-1.2008 too.
		//
		// FreeBSD associates EMLINK with O_NOFOLLOW and
		// Tru64 uses ENOTSUP. We use these directly here
		// and skip the lstat() call and the associated race.
		// I want to hear if there are other kernels that
		// fail with something else than ELOOP with O_NOFOLLOW.
		bool was_symlink = false;

#	if defined(__FreeBSD__) || defined(__DragonFly__)
		if (errno == EMLINK)
			was_symlink = true;

#	elif defined(__digital__) && defined(__unix__)
		if (errno == ENOTSUP)
			was_symlink = true;

#	elif defined(__NetBSD__)
		if (errno == EFTYPE)
			was_symlink = true;

#	else
		if (errno == ELOOP && !follow_symlinks) {
			const int saved_errno = errno;
			struct stat st;
			if (lstat(pair->src_name, &st) == 0
					&& S_ISLNK(st.st_mode))
				was_symlink = true;

			errno = saved_errno;
		}
#	endif

		if (was_symlink)
			;//message_warning(_("%s: Is a symbolic link, "
			//		"skipping"), pair->src_name);
		else
#endif
			// Something else than O_NOFOLLOW failing
			// (assuming that the race conditions didn't
			// confuse us).
			//message_error("%s: %s", pair->src_name,
			//		strerror(errno));

		return true;
	}

#ifndef TUKLIB_DOSLIKE
	// Drop O_NONBLOCK, which is used only when we are accepting only
	// regular files. After the open() call, we want things to block
	// instead of giving EAGAIN.
	if (reg_files_only) {
		flags = fcntl(pair->src_fd, F_GETFL);
		if (flags == -1)
			goto error_msg;

		flags &= ~O_NONBLOCK;

		if (fcntl(pair->src_fd, F_SETFL, flags) == -1)
			goto error_msg;
	}
#endif

	// Stat the source file. We need the result also when we copy
	// the permissions, and when unlinking.
	if (fstat(pair->src_fd, &pair->src_st))
		goto error_msg;

	if (S_ISDIR(pair->src_st.st_mode)) {
		//message_warning(_("%s: Is a directory, skipping"),
		//		pair->src_name);
		goto error;
	}

	if (reg_files_only && !S_ISREG(pair->src_st.st_mode)) {
		//message_warning(_("%s: Not a regular file, skipping"),
		//		pair->src_name);
		goto error;
	}

#ifndef TUKLIB_DOSLIKE
	if (reg_files_only && !opt_force) {
		if (pair->src_st.st_mode & (S_ISUID | S_ISGID)) {
			// gzip rejects setuid and setgid files even
			// when --force was used. bzip2 doesn't check
			// for them, but calls fchown() after fchmod(),
			// and many systems automatically drop setuid
			// and setgid bits there.
			//
			// We accept setuid and setgid files if
			// --force was used. We drop these bits
			// explicitly in io_copy_attr().
			//message_warning(_("%s: File has setuid or "
			//		"setgid bit set, skipping"),
			//		pair->src_name);
			goto error;
		}

		if (pair->src_st.st_mode & S_ISVTX) {
			;//message_warning(_("%s: File has sticky bit "
			//		"set, skipping"),
			//		pair->src_name);
			goto error;
		}

		if (pair->src_st.st_nlink > 1) {
			//message_warning(_("%s: Input file has more "
			//		"than one hard link, "
			//		"skipping"), pair->src_name);
			goto error;
		}
	}
#endif

	return false;

error_msg:
	return false;

error:
	(void)close(pair->src_fd);
	return true;
}
