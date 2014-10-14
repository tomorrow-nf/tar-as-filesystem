/*
 This code is licensed under the LGPLv2:
  LGPL (http://www.gnu.org/copyleft/lgpl.html
 and owned/created by The Taylor Lab at Johns
 Hopkins University.

 Modifications have been made by Kyle Davidson
 and Tyler Morrow for use in our MQP project at
 Worcester Polytechnic Institute
*/

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "micro-bunzip.h"

#define BUF_SIZE 8192
#define BYTES_IN_GB 1073741824
#define BITS_IN_BYTE 8

/**
 * Seek the bunzip_data `bz` to a specific position in bits `pos` by lseeking
 * the underlying file descriptor and priming the buffer with appropriate
 * bits already consumed. This probably only makes sense for seeking to the
 * start of a compressed block.
 */
unsigned int seek_bits( bunzip_data *bd, unsigned long long pos )
{
    off_t n_byte = pos / 8;
    char n_bit = pos % 8;

    // Seek the underlying file descriptor
    if ( lseek( bd->in_fd, n_byte, SEEK_SET ) != n_byte )
    {
        return -1;
    }

    // Init the buffer at the right bit position
    bd->inbufBitCount = bd->inbufPos = bd->inbufCount = 0;
    get_bits( bd, n_bit );

    // // Update the bit position counter to match
    // bd->inPosBits = pos;

    return 1;
}

/* Open, seek to block, and uncompress into buffer */

int uncompressblock( char* filename, unsigned long long position, void* buf )
{
    bunzip_data *bd;
    int status;
    int gotcount;
    char outbuf[BUF_SIZE];
    unsigned long long int bytes_written = 0;

    int src_fd = open(filename, O_RDONLY);
    if(src_fd < 0) {
        printf("Unable to open file: %s\n", filename);
        return 1;
    }

    if ( !( status = start_bunzip( &bd, src_fd, 0, 0 ) ) )
    {
        seek_bits( bd, position );

        /* Fill the decode buffer for the block */
        if ( ( status = get_next_block( bd ) ) )
            goto seek_bunzip_finish;

        /* Init the CRC for writing */
        bd->writeCRC = 0xffffffffUL;

        /* Zero this so the current byte from before the seek is not written */
        bd->writeCopies = 0;

        /* Decompress the block and write to stdout */
        for ( ; ; )
        {
            gotcount = read_bunzip( bd, outbuf, BUF_SIZE );
            if ( gotcount < 0 )
            {
                status = gotcount;
                break;
            }
            else if ( gotcount == 0 )
            {
                break;
            }
            else
            {
                memcpy( (void*)((char*)buf + bytes_written), outbuf, gotcount );
                bytes_written = bytes_written + gotcount;
            }
        }
    }

seek_bunzip_finish:

    if ( bd->dbuf ) free( bd->dbuf );
    free( bd );
    close(src_fd);

    return status;
}
