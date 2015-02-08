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

/*
TAR-Browser: File system in userspace to examine the contents
of a TAR archive

This file primarily handles FUSE operations

Initial release supports uncompressed TAR archives, 
tar.xz archives, and tar.bz2 archives

Developed by Kyle Davidson (kpdavidson@wpi.edu) and Tyler Morrow (tomorrow@wpi.edu)
as an MQP project at Worcester Polytechnic Institute (2014-2015)

Adapted from example FUSE file fusexmp.c, which holds the following
copyright and license information:
		Copyright (C) 2001-2007 Miklos Szeredi <miklos@szeredi.hu>
		Copyright (C) 2011 Sebastian Pipping <sebastian@pipping.org>
		This program can be distributed under the terms of the GNU GPL.
		See the file COPYING.
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


// Fill stbuf structure similar to the lstat() function, some comes from lstat of the archive file, others come from database
static int tar_getattr(const char *path, struct stat *stbuf)
{	
	//original code
	/*int res;
	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;
	return 0;*/

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EACCES;
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
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EACCES;
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
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EACCES;
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
						if(strncmp(row[5], "1") == 0, strncmp(row[5], "2") == 0) {
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
		errornumber = -EACCES;
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
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EACCES;
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
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -ENOENT;
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

//TODO
//takes a symbolic link and puts its referenced path in buf
static int tar_readlink(const char *path, char *buf, size_t size)
{
	//original code
	/*
	int res;
	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;
	buf[res] = '\0';
	return 0;
	*/

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EACCES;
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
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -EACCES;
				}
				else {
					if(mysql_num_rows(result) == 0) {
						//file does not exist, set not found error
						errornumber = -ENOENT;
					}
					else {
						MYSQL_ROW row = mysql_fetch_row(result);
						//check if its a hardlink "1" or softlink "2"
						if(strncmp(row[0], "2") == 0) {
							strncpy(buf, row[1], (size-1));
							buf[(size-1)] = '\0';
						}
						else if(strncmp(row[0], "1") == 0) {
							char linkstring[5000];
							sprintf(linkstring, "/%s/%s", archivename, row[1])
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
	//original code
	/*
	DIR *dp;
	struct dirent *de;
	(void) offset;
	(void) fi;
	dp = opendir(path);
	if (dp == NULL)
		return -errno;
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));

		//can get these 2 from using lstat on file
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12; 
		
		//buf : not touched
		//de->d_name : a string name
		if (filler(buf, de->d_name, &st, 0))
			break;
	}
	closedir(dp);
	return 0; */

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = 0;
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
			//query error, just stop and return nothing
			errornumber = 0;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = 0;
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
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = 0;
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
							memcpy(&st, &topdir, sizeof(topdir));
							//st->st_dev = same as topdir
							st.st_ino = 999; //big useless number
							if(strncmp(row[6], "1") == 0, strncmp(row[6], "2") == 0) {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFLNK;
							}
							else if(strcmp(row[0], "N") == 0) {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFREG;
							}
							else {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFDIR;
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
		//no file extension
		if(file_ext == NULL) errornumber = -ENOENT;
		//.tar
		else if(strcmp(".tar", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from UncompTar WHERE ArchiveName = '%s' AND MemberPath = '%s%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.bz2 //TODO add other forms of bz2 extention
		else if(strcmp(".bz2", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from Bzip2_files WHERE ArchiveName = '%s' AND MemberPath = '%s%s'", archivename, within_tar_path, within_tar_filename);
		}
		//.xz or .txz
		else if(strcmp(".xz", file_ext) == 0 || strcmp(".txz", file_ext) == 0) {
			sprintf(insQuery, "SELECT DirFlag, MemberLength, MemberName, Mode, Uid, Gid, LinkFlag from CompXZ WHERE ArchiveName = '%s' AND MemberPath = '%s%s'", archivename, within_tar_path, within_tar_filename);
		}
		//unrecognized file extension
		else errornumber = -ENOENT;

		//if no error send query and use result
		if(errornumber == 0) {
			if(mysql_query(con, insQuery)) {
				//query error
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = 0;
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
							memcpy(&st, &topdir, sizeof(topdir));
							//st.st_dev = same as topdir
							st.st_ino = 999; //big useless number
							if(strncmp(row[6], "1") == 0, strncmp(row[6], "2") == 0) {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFLNK;
							}
							else if(strcmp(row[0], "N") == 0) {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFREG;
							}
							else {
								stbuf->st_mode = 0 + strtol(row[3], NULL, 10) + S_IFDIR;
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
	//original code
	/*
	int res;
	res = open(path, fi->flags);
	if (res == -1)
		return -errno;
	close(res);
	return 0; */

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EACCES;
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
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -EACCES;
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
						else if(fi->flags != O_RDONLY) {
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
				errornumber = -ENOENT;
			}
			else {
				MYSQL_RES* result = mysql_store_result(con);
				if(result == NULL) {
					errornumber = -ENOENT;
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
							if(fi->flags != O_RDONLY) {
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
// TODO: read “size” bytes from the file after moving 
// “offset” through the file, use math to determine the block
static int tar_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	//original code
	/*
	int fd;
	int res;
	(void) fi;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;
	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;
	close(fd);
	return res; */

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		errornumber = -EACCES;
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
		long long int archiveid = 0 + fi->fd; //get archive id from file_info
		sprintf(insQuery, "SELECT ArchivePath, Timestamp from ArchiveList WHERE ArchiveID = %lld", archiveid);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(result == NULL) {
				errornumber = -ENOENT;
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
							int fi_des = open(path, O_RDONLY);
							if (fi_des == -1)
								errornumber = -errno;
							else {
								if (pread(fi_des, buf, size, offset) == -1) res = -errno;
								close(fd);
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
		//TODO
		errornumber = -EACCES;
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

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
	// original code
	/*
	int res;
	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;
	return 0;
	*/

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
