#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include "sqloptions.h"
#include "bzip_seek/bitmapstructs.h" //this is just for the blockmap struct
#include "common_functions.h"
#include "xzfuncs.h"

#define TARFLAG 0
#define BZ2FLAG 1
#define XZFLAG 2

/*
TAR-Browser: File system in userspace to examine the contents
of a TAR archive

This file primarily handles FUSE operations

Initial release supports uncompressed TAR archives, 
tar.xz archives, and tar.bz2 archives

Developed by Kyle Davidson (kpdavidson@wpi.edu) and Tyler Morrow (tomorrow@wpi.edu)
as an MQP project at Worcester Polytechnic Institute (2014-2015)

Released to the public domain.

*/

//special variable to hold stats for top level directory
struct stat topdir;

// parses a path into useful strings
// -path -- the path to be parsed, path is copied and the original string is unaffected
// -archivename, filepath, filename -- pass in "pointers to char pointers", they are malloced in here
// -file_ext -- the file extention, just a pointer pointing to the first . in archivename
void parsepath(const char *path, char** archivename, char** filepath, char** filename, char** file_ext) {
	char* tmpstr = (char*) malloc(sizeof(char) * (strlen(path) + 1));
	strcpy(tmpstr, path);

	//case path = /
	if(strcmp(tmpstr, "/") == 0) {
	*archivename = NULL;
	*filepath = NULL;
	*filename = NULL;
	*file_ext = NULL;
	}
	else {
		char* readptr = tmpstr;
		readptr++; //move past initial /
		
		//get archive name
		*archivename = (char*) malloc(sizeof(char) * 256);
		char* writeptr = *archivename;
		do {
			*writeptr = *readptr;
			writeptr++;
			readptr++;
		}while(*readptr != '/' && *readptr != '\0');
		*writeptr = '\0';

		//get file extension
		*file_ext = strrchr(*archivename, '.');

		//there is a path inside the tarfile
		if(*readptr != '\0') {
			*filepath = (char*) malloc(sizeof(char) * (strlen(readptr) + 2));
			*filename = (char*) malloc(sizeof(char) * 257);
			strcpy(*filepath, readptr);
			
			int i;
			char* backtracking = *filepath;
			for(i=0;i<(strlen(*filepath) - 1);i++) {
				backtracking++;
			}

			while(*backtracking != '/') {
				backtracking--;
			}
			backtracking++;
			strcpy(*filename, backtracking);
			*backtracking = '\0';
		}
	}

	free(tmpstr);
}


// similar to grab_block() for XZ, but for bzip2. Decompresses a single block into a buffer
void* getblock_bzip(char* filename, int blocknum, struct blockmap* offsets) {
    
    void* blockbuf = (char*) malloc(((offsets->blocklocations)[blocknum]).uncompressedSize); // Build a buffer to hold a single block

    // Read a single block into the buffer
    int err = uncompressblock( filename, ((offsets->blocklocations)[blocknum]).position,
                        blockbuf );

    if (!err){
        return blockbuf;
    }
    else {
        return NULL;
    }
}


// I've implemented a quick definition for "filetype"
// This is to prevent the slight overhead from strcmp() and to be lazy.
// TARFLAG = 0, BZ2FLAG = 1, XZFLAG = 2
// I've also combined the functionality of XZ and BZ2 reads into this one function, as the code
// was more or less identical except for grab_block() vs getblock_bzip(). Just pass in the type when calling this
long long int read_compressed(char* filename, int filetype, int blocknum, long long int offset, long long int size, char* outbuf, struct blockmap* offsets) {

	long long int bytes_to_read = size;
	char* block = NULL;

	// Check file type, then use the appropriate function to grab a block
	if (filetype == XZFLAG){
		block = grab_block(blocknum, filename);
	}
	else if (filetype == BZ2FLAG){
		block = getblock_bzip(filename, blocknum, offsets);
	}
	
	char* location = block;
	location = location + offset;
	int current_blocknum = blocknum;

	// make sure this block exists
	if(block == NULL) {
		return -EIO;
	}
	
	// convenience
	long long int data_left = ((offsets->blocklocations)[current_blocknum]).uncompressedSize - offset;
	
	while(1) {
		// Check if this is the last block to read from
		if(data_left > bytes_to_read) {
			memcpy(outbuf, location, bytes_to_read);
			free(block);
			break;
		}
		else {
			// For all blocks except the last, copy everything from the block into
			// the buffer, update location info, then work with the next block
			memcpy(outbuf, location, data_left);
			bytes_to_read = bytes_to_read - data_left;
			location = location + data_left;
			free(block);
			current_blocknum++;

			// Check file type, then use the appropriate function to grab a block again
			if (filetype == XZFLAG){
				block = grab_block(current_blocknum, filename);
			}
			else if (filetype == BZ2FLAG){
				block = getblock_bzip(filename, current_blocknum, offsets);
			}
			
			// If this is the last block or the next is corrupted, error
			if(block == NULL) return -EIO;
			data_left = ((offsets->blocklocations)[current_blocknum]).uncompressedSize;
		}
	}

	return size;
}



// Fill stbuf structure similar to the lstat() function, some comes from lstat of the archive file, others come from database
static int tar_getattr(const char *path, struct stat *stbuf)
{	
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		memcpy(stbuf, &topdir, sizeof(topdir));
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath, Timestamp from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//file does not exist, set not found error
					errornumber = -ENOENT;
				}
				else {
					MYSQL_ROW row = mysql_fetch_row(result);
					if(lstat(row[0], stbuf) == -1) {
						errornumber = -errno;
					}
					//check timestamp
					else {
						char* mod_time = ctime(&(stbuf->st_mtime));
						if(strcmp(row[1], mod_time) != 0) {
							errornumber = -ENOENT;
						}
						//set to appear as directory
						stbuf->st_mode = topdir.st_mode; //directory w/ usual permissions
					}
				}
				mysql_free_result(result);
			}
		}
	}
	// path is /TarArchive.tar/more
	else {
		/* Seperate mysql queries for different filetypes */
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT MemberLength, Mode, Uid, Gid, Dirflag, LinkFlag from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT MemberLength, Mode, Uid, Gid, Dirflag, LinkFlag from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT MemberLength, Mode, Uid, Gid, Dirflag, LinkFlag from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//file does not exist, set not found error
						errornumber = -ENOENT;
					}
					else {
						MYSQL_ROW row = mysql_fetch_row(result);
						memcpy(stbuf, &topdir, sizeof(topdir));
						//stbuf->st_dev = same as topdir
						stbuf->st_ino = 999; //big useless number
						if(strncmp(row[5], "1", 1) == 0 || strncmp(row[5], "2", 1) == 0) {
							stbuf->st_mode = 0 + strtol(row[1], NULL, 10) + S_IFLNK;
						}
						else if(strcmp(row[4], "N") == 0) {
							stbuf->st_mode = 0 + strtol(row[1], NULL, 10) + S_IFREG;
						}
						else {
							stbuf->st_mode = 0 + strtol(row[1], NULL, 10) + S_IFDIR;
						}
						stbuf->st_nlink = 0;
						stbuf->st_uid = 0 + strtol(row[2], NULL, 10);
						stbuf->st_gid = 0 + strtol(row[3], NULL, 10);
						//stbuf->st_rdev = same as topdir
						stbuf->st_size = 0 + strtoll(row[0], NULL, 10);
						//stbuf->st_blksize = same as topdir
						stbuf->st_blocks = 0; //just set blocks to 0
						//access, modification, and stat times come from topdir
					}
					mysql_free_result(result);
				}
			}
		}
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}

// if the file does not exist in the database return (-1 * ENOENT)
// else if the file is a directory check for mask containing W_OK flag
// else only F_OK or R_OK masks allowed
static int tar_access(const char *path, int mask)
{
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//directory, only thing not allowed is write
		if(mask == (W_OK | X_OK | R_OK) ||
			mask == (W_OK | X_OK) ||
			mask == (W_OK | R_OK) ||
			mask == W_OK) {
			
			errornumber = -EACCES;
		}
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath, Timestamp from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//file does not exist, set not found error
					errornumber = -ENOENT;
				}
				else {
					MYSQL_ROW row = mysql_fetch_row(result);
					struct stat statbuff;
					if(lstat(row[0], &statbuff) == -1) {
						errornumber = -errno;
					}
					//check timestamp
					else {
						char* mod_time = ctime(&(statbuff.st_mtime));
						if(strcmp(row[1], mod_time) != 0) {
							errornumber = -ENOENT;
						}
						//check mask directory, only thing not allowed is write
						else if(mask == (W_OK | X_OK | R_OK) ||
							mask == (W_OK | X_OK) ||
							mask == (W_OK | R_OK) ||
							mask == W_OK) {
							
							errornumber = -EACCES;
						}
						else errornumber = 0;
					}
				}
				mysql_free_result(result);
			}
		}
	}
	// path is /TarArchive.tar/more
	else {
		/* Seperate mysql queries for different filetypes */
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//file does not exist, set not found error
						errornumber = -ENOENT;
					}
					else {
						MYSQL_ROW row = mysql_fetch_row(result);
						if(strcmp(row[0], "Y") == 0) {
							//directory, only thing not allowed is write
							if(mask == (W_OK | X_OK | R_OK) ||
								mask == (W_OK | X_OK) ||
								mask == (W_OK | R_OK) ||
								mask == W_OK) {
								
								errornumber = -EACCES;
							}
						}
						else {
							//file, only reading allowed
							if(mask != F_OK && mask != R_OK) {
								errornumber = -EACCES;
							}
						}
					}
					mysql_free_result(result);
				}
			}
		}
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}


//takes a symbolic link and puts its referenced path in buf
static int tar_readlink(const char *path, char *buf, size_t size)
{
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		errornumber = -EINVAL;
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		errornumber = -EINVAL;
	}
	// path is /TarArchive.tar/more
	else {
		/* Seperate mysql queries for different filetypes */
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT LinkFlag, LinkTarget from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT LinkFlag, LinkTarget from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT LinkFlag, LinkTarget from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//file does not exist, set not found error
						errornumber = -ENOENT;
					}
					else {
						MYSQL_ROW row = mysql_fetch_row(result);
						//check if its a hardlink "1" or softlink "2"
						if(strncmp(row[0], "2", 1) == 0) {
							strncpy(buf, row[1], (size-1));
							buf[(size-1)] = '\0';
						}
						else if(strncmp(row[0], "1", 1) == 0) {
							char linkstring[5000];
							char converted_linkstring[8000] = "";
							sprintf(linkstring, "%s/%s", archivename, row[1]);
							int dots = 0;
							int i;
							for(i=0;i<strlen(path);i++) {
								if(path[i] == '/') dots++;
							}

							for(i=1;i<dots;i++) {
								strcat(converted_linkstring, "../");
							}
							strcat(converted_linkstring, linkstring);
							strncpy(buf, converted_linkstring, (size-1));
							buf[(size-1)] = '\0';
						}
						else {
							errornumber = -EINVAL;
						}
					}
					mysql_free_result(result);
				}
			}
		}
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}


// -extra slashes are omitted (ex. "/home/" becomes "/home"
static int tar_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
	//printf("\nDEBUG READDIR: entered - %s\n", path);
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		sprintf(insQuery, "SELECT ArchiveName, ArchivePath, Timestamp from ArchiveList");
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//no results
					errornumber = 0;
				}
				else {
					//while there are rows to be fetched
					MYSQL_ROW row;
					while((row = mysql_fetch_row(result))) {
						//get the real stats of the file
						struct stat st;
						memset(&st, 0, sizeof(st));
						if(lstat(row[1], &st) == 0) {
							//redefine as directory and pass to filler
							st.st_mode = topdir.st_mode; //directory w/ usual permissions;
							//check timestamp
							char* mod_time = ctime(&(st.st_mtime));
							if(strcmp(row[2], mod_time) == 0) {
								if (filler(buf, row[0], &st, 0)) break;
							}
						}
					}
				}
				mysql_free_result(result);
			}
		}
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		//printf("DEBUG READDIR: readdir on archive.tar\n");
		/* Seperate mysql queries for different filetypes */
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '/'", archivename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '/'", archivename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '/'", archivename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			//printf("DEBUG READDIR: sending query %s\n", insQuery);
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//printf("DEBUG READDIR: NO RESULTS\n");
						//no results
						errornumber = 0;
					}
					else {
						//while there are rows to be fetched
						MYSQL_ROW row;
						while((row = mysql_fetch_row(result))) {
							//get the real stats of the file
							struct stat st;
							memcpy(&st, &topdir, sizeof(topdir));
							//st.st_dev = same as topdir
							st.st_ino = 999; //big useless number
							if(strncmp(row[6], "1", 1) == 0 || strncmp(row[6], "2", 1) == 0) {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFLNK;
							}
							else if(strcmp(row[0], "N") == 0) {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFREG;
							}
							else {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFDIR;
							}
							st.st_nlink = 0;
							st.st_uid = 0 + strtoll(row[4], NULL, 10);
							st.st_gid = 0 + strtoll(row[5], NULL, 10);
							//st.st_rdev = same as topdir
							st.st_size = 0 + strtoll(row[1], NULL, 10);
							//st->st_blksize = same as topdir
							st.st_blocks = 0; //just set blocks to 0
							//access, modification, and stat times come from topdir
							if (filler(buf, row[2], &st, 0)) break;
						}
					}
					mysql_free_result(result);
				}
			}
		}
	}
	// path is /TarArchive.tar/more
	else {
		/* Seperate mysql queries for different filetypes */
		//printf("DEBUG READDIR: readdir on archive.tar/more\n");
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s%s/'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s%s/'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s%s/'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			//printf("DEBUG READDIR: sending query %s\n", insQuery);
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//printf("DEBUG READDIR: NO RESULTS\n");
						//no results
						errornumber = 0;
					}
					else {
						//while there are rows to be fetched
						MYSQL_ROW row;
						while((row = mysql_fetch_row(result))) {
							//get the real stats of the file
							struct stat st;
							memcpy(&st, &topdir, sizeof(topdir));
							//st.st_dev = same as topdir
							st.st_ino = 999; //big useless number
							if(strncmp(row[6], "1", 1) == 0 || strncmp(row[6], "2", 1) == 0) {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFLNK;
							}
							else if(strcmp(row[0], "N") == 0) {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFREG;
							}
							else {
								st.st_mode = 0 + strtol(row[3], NULL, 10) + S_IFDIR;
							}
							st.st_nlink = 0;
							st.st_uid = 0 + strtoll(row[4], NULL, 10);
							st.st_gid = 0 + strtoll(row[5], NULL, 10);
							//st.st_rdev = same as topdir
							st.st_size = 0 + strtoll(row[1], NULL, 10);
							//st.st_blksize = same as topdir
							st.st_blocks = 0; //just set blocks to 0
							//access, modification, and stat times come from topdir
							if (filler(buf, row[2], &st, 0)) break;
						}
					}
					mysql_free_result(result);
				}
			}
		}
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}

// attempts to make a special file
// NOT ALLOWED
static int tar_mknod(const char *path, mode_t mode, dev_t rdev)
{
	/* DEBUG */
	printf("attempted illegal operation mknod\n");
	/* DEBUG END */

	return (-1 * EACCES); //permission not allowed
}

// attempt to create directory
// NOT ALLOWED
static int tar_mkdir(const char *path, mode_t mode)
{
	/* DEBUG */
	printf("attempted illegal operation mkdir\n");
	/* DEBUG END */
	return (-1 * EACCES); //permission not allowed
}

// attempt to delete a file and the name its associated with
// NOT ALLOWED
static int tar_unlink(const char *path)
{
	/* DEBUG */
	printf("attempted illegal operation unlink\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to remove a directory
// NOT ALLOWED
static int tar_rmdir(const char *path)
{
	/* DEBUG */
	printf("attempted illegal operation rmdir\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to create symbolic link
static int tar_symlink(const char *from, const char *to)
{
	/* DEBUG */
	printf("attempted illegal operation symlink\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to rename an entity
static int tar_rename(const char *from, const char *to)
{
	/* DEBUG */
	printf("attempted illegal operation rename\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to create a hard link
static int tar_link(const char *from, const char *to)
{
	/* DEBUG */
	printf("attempted illegal operation link\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to change access permissions
static int tar_chmod(const char *path, mode_t mode)
{
	/* DEBUG */
	printf("attempted illegal operation chmod\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to change owner of a file
static int tar_chown(const char *path, uid_t uid, gid_t gid)
{
	/* DEBUG */
	printf("attempted illegal operation chown\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// attempt to truncate a file
static int tar_truncate(const char *path, off_t size)
{
	/* DEBUG */
	printf("attempted illegal operation truncate\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// changes file timestamp
#ifdef HAVE_UTIMENSAT
static int tar_utimens(const char *path, const struct timespec ts[2])
{
	/* DEBUG */
	printf("attempted illegal operation utimens\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}
#endif


// if file exists:
//   if fi->flags != RD_ONLY return (-1 * EACCES);
//   else return 0;
// else return (-1 * ENOENT);
static int tar_open(const char *path, struct fuse_file_info *fi)
{
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		errornumber = -EACCES;
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath, Timestamp, ArchiveID from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//file does not exist, set not found error
					errornumber = -ENOENT;
				}
				else {
					MYSQL_ROW row = mysql_fetch_row(result);
					struct stat statbuff;
					if(lstat(row[0], &statbuff) == -1) {
						errornumber = -errno;
					}
					//check timestamp
					else {
						char* mod_time = ctime(&(statbuff.st_mtime));
						if(strcmp(row[1], mod_time) != 0) {
							errornumber = -ENOENT;
						}
						//check fi->flags for read only
						else if((fi->flags & O_ACCMODE) != O_RDONLY) {
							errornumber = -EACCES;
						}
						else fi->fh = 0 + strtoll(row[2], NULL, 10);
					}
				}
				mysql_free_result(result);
			}
		}
	}
	// path is /TarArchive.tar/more
	else {
		/* Seperate mysql queries for different filetypes */
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, FileID from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, FileID from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, FileID from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s' AND MemberName = '%s'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -EIO;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EIO;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//file does not exist, set not found error
						errornumber = -ENOENT;
					}
					else {
						MYSQL_ROW row = mysql_fetch_row(result);
						if(strcmp(row[0], "Y") == 0) {
							//directory, not allowed to open
							errornumber = -EACCES;
						}
						else {
							//file, only reading allowed
							if((fi->flags & O_ACCMODE) != O_RDONLY) {
								printf("DEBUG, not read only\n");
								errornumber = -EACCES;
							}
							//set file handle to ID
							else {
								fi->fh = 0 + strtoll(row[1], NULL, 10);
							}
						}
					}
					mysql_free_result(result);
				}
			}
		}
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}
// read “size” bytes from the file after moving 
// “offset” through the file, use math to determine the block
static int tar_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EIO;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	char* file_ext = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename, &file_ext);
	char insQuery[1000];

	// read specific variables
	char path_to_archive[5000]; //stored path to the real location of the archive
	struct blockmap* block_offsets = (struct blockmap*) malloc(sizeof(struct blockmap));
	block_offsets->blocklocations = NULL; // map of blocks in archive
	off_t real_offset; // offset of file within block + offset from beginning of file
	size_t real_size; // the real amount to read min("size" , "filesize - offset")
	

	// path is "/"
	if(archivename == NULL) {
		errornumber = -EACCES;
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		long long int archiveid = 0 + fi->fh; //get archive id from file_info
		sprintf(insQuery, "SELECT ArchivePath, Timestamp from ArchiveList WHERE ArchiveID = %lld", archiveid);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//file does not exist, set not found error
					errornumber = -ENOENT;
				}
				else {
					MYSQL_ROW row = mysql_fetch_row(result);
					struct stat statbuff;
					if(lstat(row[0], &statbuff) == -1) {
						errornumber = -errno;
					}
					//check timestamp
					else {
						char* mod_time = ctime(&(statbuff.st_mtime));
						if(strcmp(row[1], mod_time) != 0) {
							errornumber = -ENOENT;
						}
						// open and read file
						else {
							int fi_des = open(row[0], O_RDONLY);
							if (fi_des == -1)
								errornumber = -errno;
							else {
								if (pread(fi_des, buf, size, offset) == -1) errornumber = -errno;
								close(fi_des);
							}
						}
					}
				}
				mysql_free_result(result);
			}
		}
	}
	// path is /TarArchive.tar/more
	else {
		/* Get Archive's real path ********************************************/
		sprintf(insQuery, "SELECT ArchivePath, Timestamp from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -EIO;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EIO;
			}
			else {
				if(mysql_num_rows(result) == 0) {
					//file does not exist, set not found error
					errornumber = -ENOENT;
				}
				else {
					MYSQL_ROW row = mysql_fetch_row(result);
					struct stat statbuff;
					if(lstat(row[0], &statbuff) == -1) {
						errornumber = -errno;
					}
					//check timestamp
					else {
						char* mod_time = ctime(&(statbuff.st_mtime));
						if(strcmp(row[1], mod_time) != 0) {
							errornumber = -ENOENT;
						}
						// copy filepath to easy location
						else {
							strcpy(path_to_archive, row[0]);
						}
					}
				}
				mysql_free_result(result);
			}
		}

		/* Load Blockmap ******************************************************/
		if(errornumber == 0) {
			int needBlockmap = 0;
			//no file extension
			if(file_ext == NULL) errornumber = -ENOENT;
			//.tar
			else if(strcmp(".tar", file_ext) == 0) {
				needBlockmap = 0;
			}
			//.bz2 //TODO add other forms of bz2 extention
			else if(strcmp(".bz2", file_ext) == 0) {
				needBlockmap = 1;
				sprintf(insQuery, "SELECT Blocknumber, BlockOffset, BlockSize from Bzip2_blocks WHERE ArchiveName = '%s'", archivename);
			}
			//.xz or .txz
			else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
				needBlockmap = 1;
				sprintf(insQuery, "SELECT Blocknumber, BlockOffset, BlockSize from CompXZ_blocks WHERE ArchiveName = '%s'", archivename);
			}
			//unrecognized file extension
			else errornumber = -ENOENT;
	
			//if no error and we need a blockmap send query and use result
			if(errornumber == 0 && needBlockmap == 1) {
				if(mysql_query(con, insQuery)) {
					//query error
					errornumber = -EIO;
				}
				else {
					MYSQL_RES* result = mysql_store_result(con);
					if(result == NULL) {
						errornumber = -EIO;
					}
					else {
						int number_of_results = mysql_num_rows(result);
						if(number_of_results == 0) {
							//no results
							errornumber = -ENOENT;
						}
						else {
							MYSQL_ROW row;
							//while there are rows to be fetched
							block_offsets->blocklocations = (struct blocklocation*) malloc(sizeof(struct blocklocation) * (number_of_results + 10)); //slightly too large to be safe
							block_offsets->maxsize = (number_of_results + 10);
							while((row = mysql_fetch_row(result))) {
								long int this_block_num = strtol(row[0], NULL, 10);
								unsigned long long this_pos = strtoull(row[1], NULL, 10);
								unsigned long long this_unC_size = strtoull(row[2], NULL, 10);
								((block_offsets->blocklocations)[this_block_num]).position = this_pos;
								((block_offsets->blocklocations)[this_block_num]).uncompressedSize = this_unC_size;
							}
						}
						mysql_free_result(result);
					}
				}
			}
		}

		/************** get info about file you want to extract and do the read *******/
		if(errornumber == 0) {
			long long int files_id = 0 + fi->fh; //get archive id from file_info
			//no file extension
			if(file_ext == NULL) errornumber = -ENOENT;
			//.tar
			else if(strcmp(".tar", file_ext) == 0) {
				sprintf(insQuery, "SELECT GBoffset, BYTEoffset, MemberLength from UncompTar WHERE FileID = %lld", files_id);
				if(mysql_query(con, insQuery)) {
					//query error
					errornumber = -EIO;
				}
				else {
					MYSQL_RES* result = mysql_store_result(con);
					if(result == NULL) {
						errornumber = -EIO;
					}
					else {
						if(mysql_num_rows(result) == 0) {
							//no results
							errornumber = -ENOENT;
						}
						else {
							MYSQL_ROW row;
							row = mysql_fetch_row(result);
							unsigned long long length_of_file = strtoull(row[2], NULL, 10);
							//check if offset puts us outside file range
							if(offset >= length_of_file) errornumber = -ENXIO;
							else {
								//calculate real offset in tarfile
								real_offset = (strtoull(row[0], NULL, 10) * BYTES_IN_GB) + strtoull(row[1], NULL, 10) + offset;
								//calculate real size to be read
								real_size = length_of_file - offset;
								if(size < real_size) {
									real_size = size;
								}
								
								int fi_des = open(path_to_archive, O_RDONLY);
								if (fi_des == -1)
									errornumber = -errno;
								else {
									unsigned long long Re = pread(fi_des, buf, real_size, real_offset);
									if (Re == -1) errornumber = -errno;
									else errornumber = Re;
									
									close(fi_des);
								}
							}
						}
						mysql_free_result(result);
					}
				}
			}
			//TODO.bz2 //TODO add other forms of bz2 extention
			else if(strcmp(".bz2", file_ext) == 0 || strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
				int fileflag;
				if(strcmp(".bz2", file_ext) == 0) {
					sprintf(insQuery, "SELECT Blocknumber, InsideOffset, MemberLength from Bzip2_files WHERE FileID = %lld", files_id);
					fileflag = BZ2FLAG;
				}
				else {
					sprintf(insQuery, "SELECT Blocknumber, InsideOffset, MemberLength from CompXZ WHERE FileID = %lld", files_id);
					fileflag = XZFLAG;
				}
				if(mysql_query(con, insQuery)) {
					//query error
					errornumber = -EIO;
				}
				else {
					MYSQL_RES* result = mysql_store_result(con);
					if(result == NULL) {
						errornumber = -EIO;
					}
					else {
						if(mysql_num_rows(result) == 0) {
							//no results
							errornumber = -ENOENT;
						}
						else {
							MYSQL_ROW row;
							row = mysql_fetch_row(result);
							unsigned long long length_of_file = strtoull(row[2], NULL, 10);
							//check if offset puts us outside file range
							if(offset >= length_of_file) errornumber = -ENXIO;
							else {
								//find real block and real offset
								long long blocknumber = strtoll(row[0], NULL, 10);
								unsigned long long real_offset = 0 + strtoull(row[1], NULL, 10);
								unsigned long long data_remaining = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize - real_offset;
								
								//calculate real size to be read
								real_size = length_of_file - offset;
								if(size < real_size) {
									real_size = size;
								}

								//move real_offset foward by "offset"
								unsigned long long remaining_seek = 0 + offset;
								printf("remaining_seek: %llu\n", remaining_seek);
								printf("blocknumber: %lld\n", blocknumber);
								while(remaining_seek > data_remaining) {
									blocknumber++;
									remaining_seek = remaining_seek - data_remaining;
									data_remaining = ((block_offsets->blocklocations)[blocknumber]).uncompressedSize;
									real_offset = 0;
								}
								if(remaining_seek != 0) {
									real_offset = 0 + remaining_seek;
								}

								//blocknumber and real_offset now point to the offset within the file desired by read
								unsigned long long bn = 0+blocknumber;
								unsigned long long ro = 0+real_offset;
								unsigned long long rs = 0+real_size;
								printf("block %llu, offset %llu, size %llu\n", bn, ro, rs);
								errornumber = read_compressed(path_to_archive, fileflag, blocknumber, real_offset, real_size, buf, block_offsets);
							}
						}
						mysql_free_result(result);
					}
				}
			}
			//unrecognized file extension
			else errornumber = -ENOENT;
		}
		
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);
	if(block_offsets->blocklocations != NULL) free(block_offsets->blocklocations);
	if(block_offsets != NULL) free(block_offsets);

	return errornumber;
}
// attempt to open and write to a file
static int tar_write(const char *path, const char *buf, size_t size,
	off_t offset, struct fuse_file_info *fi)
{
	/* DEBUG */
	printf("attempted illegal operation write\n");
	/* DEBUG END */
	return (-1 * EACCES); //write permission not allowed
}

// returns information about a mounted file system path
// --we don't allow mounted filesystems within the our FUSE
static int tar_statfs(const char *path, struct statvfs *stbuf)
{
	return -EACCES;
}

// weird not allowed file altering
#ifdef HAVE_POSIX_FALLOCATE
static int tar_fallocate(const char *path, int mode,
	off_t offset, off_t length, struct fuse_file_info *fi)
{
	/* DEBUG */
	printf("attempted illegal operation fallocate\n");
	/* DEBUG END */
	return (-1 * EBADF);
}
#endif

static struct fuse_operations tar_oper = {
	.getattr = tar_getattr,
	.access = tar_access,
	.readlink = tar_readlink,
	.readdir = tar_readdir,
	.mknod = tar_mknod,
	.mkdir = tar_mkdir,
	.symlink = tar_symlink,
	.unlink = tar_unlink,
	.rmdir = tar_rmdir,
	.rename = tar_rename,
	.link = tar_link,
	.chmod = tar_chmod,
	.chown = tar_chown,
	.truncate = tar_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens = tar_utimens,
#endif
	.open = tar_open,
	.read = tar_read,
	.write = tar_write,
	.statfs = tar_statfs,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate = tar_fallocate,
#endif
};


int main(int argc, char *argv[])
{	if(argc > 1) {
		if (lstat(argv[1], &topdir) == -1) {
			printf("unable to lstat %s\n", argv[1]);
			return -1;
		}
	}
	umask(0);
	return fuse_main(argc, argv, &tar_oper, NULL);
}
