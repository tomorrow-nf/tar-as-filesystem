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

// parses a path into useful strings
// -path -- the path to be parsed, path is copied and the original string is unaffected
// -archivename, filepath, filename -- pass in "pointers to char pointers", they are malloced in here 
void parsepath(const char *path, char** archivename, char** filepath, char** filename) {
	char* tmpstr = (char*) malloc(sizeof(char) * (strlen(path) + 1));
	strcpy(tmpstr, path);

	//case path = /
	if(strcmp(tmpstr, "/") == 0) {
	*archivename = NULL;
	*filepath = NULL;
	*filename = NULL;
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

// TODO: 
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
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//TODO
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				//file does not exist, set not found error
				errornumber = -ENOENT;
			}
			else {
				MYSQL_ROW row = mysql_fetch_row(result);
				if(lstat(row[0], stbuf) == -1) {
					errornumber = -errno;
				}
			}
			mysql_free_result(result);
		}
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}
//TODO: 
// if the file does not exist in the database return (-1 * ENOENT)
// else if (mask == F_OK || mask == R_OK) return 0
// else return (-1 * EACCES);
static int tar_access(const char *path, int mask)
{
	/* DEBUG */
	if(mask == F_OK) printf("Tested access level F_OK %s\n", path);
	else if(mask == R_OK) printf("Tested access level R_OK %s\n", path);
	else if(mask == W_OK) printf("Tested access level W_OK %s\n", path);
	else if(mask == X_OK) printf("Tested access level X_OK %s\n", path);
	else if(mask == (R_OK | W_OK)) printf("Tested access level R_OK | W_OK %s\n", path);
	else if(mask == (R_OK | X_OK)) printf("Tested access level R_OK | X_OK %s\n", path);
	else if(mask == (W_OK | X_OK)) printf("Tested access level W_OK | X_OK %s\n", path);
	else if(mask == (W_OK | X_OK | R_OK)) printf("Tested access level R_OK | W_OK | X_OK %s\n", path);
	/* DEBUG END */

	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		errornumber = 0;
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				//file does not exist, set not found error
				errornumber = -ENOENT;
			}
			else {
				MYSQL_ROW row = mysql_fetch_row(result);
				errornumber = 0;
			}
			mysql_free_result(result);
		}
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}
//TODO:
// perform operation of man readlink(2) 
// http://linux.die.net/man/2/readlink 
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
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
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
		//TODO
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
}

//TODO research more
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
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//TODO
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		//TODO
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
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

//TODO
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
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//TODO
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		//TODO
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
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
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//TODO
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		//TODO
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
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

// TODO: Mimic statvfs(2), http://linux.die.net/man/2/statvfs 
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
	
	int errornumber = 0;

	// connect to database, begin a transaction
	MYSQL *con = mysql_init(NULL);
	//read options from file
	mysql_options(con, MYSQL_READ_DEFAULT_FILE, SQLCONFILE); //SQLCONFILE defined in sqloptions.h
	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, SQLGROUP);

	if(!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		//exit, connection failed
		mysql_close(con);
		//TODO errornumber = ?;
		return errornumber;
	}

	char* archivename = NULL;
	char* within_tar_path = NULL;
	char* within_tar_filename = NULL;
	parsepath(path, &archivename, &within_tar_path, &within_tar_filename);
	char insQuery[1000];

	// path is "/"
	if(archivename == NULL) {
		//TODO
	}
	// path is "/TarArchive.tar" or "/TarArchive.tar.bz2" or "/TarArchive.tar.xz"
	else if(within_tar_path == NULL) {
		sprintf(insQuery, "SELECT ArchivePath from ArchiveList WHERE ArchiveName = '%s'", archivename);
		if(mysql_query(con, insQuery)) {
			//query error
			errornumber = -ENOENT;
		}
		else {
			MYSQL_RES* result = mysql_store_result(con);
			if(mysql_num_rows(result) == 0) {
				//file does not exist, set not found error
				errornumber = -ENOENT;
			}
			else {
				MYSQL_ROW row = mysql_fetch_row(result);
				if(statvfs(row[0], stbuf) == -1) {
					errornumber = -errno;
				}
			}
			mysql_free_result(result);
		}
	}
	// path is /TarArchive.tar/more
	else {
		//TODO
	}
	//free possible mallocs and mysql connection
	mysql_close(con);
	if(archivename != NULL) free(archivename);
	if(within_tar_path != NULL) free(within_tar_path);
	if(within_tar_filename != NULL) free(within_tar_filename);

	return errornumber;
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
{
	umask(0);
	return fuse_main(argc, argv, &tar_oper, NULL);
}
