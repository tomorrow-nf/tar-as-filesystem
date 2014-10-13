/*
 This code is licensed under the LGPLv2:
  LGPL (http://www.gnu.org/copyleft/lgpl.html
 and owned/created by "The Taylor Lab at Johns
 Hopkins University.

 Modifications have been made by Kyle Davidson
 and Tyler Morrow for use in our MQP project at
 Worcester Polytechnic Institute
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "micro-bunzip.h"
#include "bitmapstructs.h"

#define BUF_SIZE 8192
#define BYTES_IN_GB 1073741824
#define BITS_IN_BYTE 8

/**
 * Read a bzip2 file from stdin (now from a file) and print
 * 1) The block size
 * 2) The starting offset (in BITS) of each block of compressed data
 * 3) store the offsets
 *
 * This does not completely uncompress the data, so does not do CRC checks,
 * (gaining 60% or so speedup), bzip2 --test can be used to verify files
 * first if desired.
 */


/* Function Form of the main
    -returns 0 on success
*/
int map_bzip2(char* filename, struct blockmap* offsets)
{
    int tarfile = open(filename, O_RDONLY);
    if(tarfile < 0) {
        printf("Unable to open file: %s\n", filename);
        return 1;
    }

    bunzip_data *bd;
    int status;
    unsigned long long bits_read;
    char * c;
    char buffer[BUF_SIZE];
    int gotcount;
    int totalcount;
    int kpdavidson_blockno = 0;
    int kpdavidson_GB = 0;
    int kpdavidson_Bytes = 0;
    int kpdavidson_Bits = 0;

    /* Attempt to open the bzip2 file, if successfull this consumes the
     * entire header and moves us to the start of the first block.
     */
    if ( ! ( status = start_bunzip( &bd, tarfile, 0, 0 ) ) )
    {

        for ( ; ; )
        {
            kpdavidson_blockno++;

            /* Determine bits read in the last block */
            // NOTE: bits_read and bd->position are only added to, never used so
            // they should be safe to zero
            bits_read = bd->position - bd->inbufCount + bd->inbufPos;
            bits_read = bits_read * 8 - bd->inbufBitCount;

            // reset position
            bd->position = 0;

            /* Read one block */
            status = get_next_block( bd );

            /* Reset the total size counter for each block */
            totalcount = 0;

            /* Non-zero return value indicates an error, break out */
            if ( status ) break;

            /* This is really the only other thing init_block does, hrmm */
            bd->writeCRC = 0xffffffffUL;

            /* Decompress the block and throw away, but keep track of the
               total size of the decompressed data */
            for ( ; ; )
            {
                gotcount = read_bunzip( bd, buffer, BUF_SIZE );
                if ( gotcount < 0 )
                {
                    status = gotcount;
                    goto bzip_table_finish;
                }
                else if ( gotcount == 0 )
                {
                    break;
                }
                else
                {
                    totalcount += gotcount;
                }
            }

            if((offsets->maxsize - 10) <= kpdavidson_blockno) {
               offsets->maxsize = (offsets->maxsize * 2);
               offsets->blocklocations = (struct blocklocation*) realloc(offsets->blocklocations, offsets->maxsize);
            }

            //syphon bits from Bits into Bytes then GB
            kpdavidson_Bits = kpdavidson_Bits + bits_read;

            ((offsets->blocklocations)[kpdavidson_blockno]).GB = 0;
            ((offsets->blocklocations)[kpdavidson_blockno]).bytes = 0;
            ((offsets->blocklocations)[kpdavidson_blockno]).bits = kpdavidson_Bits;
            ((offsets->blocklocations)[kpdavidson_blockno]).uncompressedSize = totalcount;
        }
    }

bzip_table_finish:

    /* If we reached the last block, do a CRC check */
    if ( status == RETVAL_LAST_BLOCK && bd->headerCRC == bd->totalCRC )
    {
        status = RETVAL_OK;
    }

    /* Free decompression buffer and bzip_data */
    if ( bd->dbuf ) free( bd->dbuf );
    free( bd );

    /* Print error if required */
    if ( status )
    {
        fprintf( stderr, "\n%s\n", bunzip_errors[-status] );
    }
    
    close(tarfile);

    return status;
}
