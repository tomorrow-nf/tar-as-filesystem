#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
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

// TODO: 
// Fill stbuf structure similar to the lstat() function, some comes from lstat of the archive file, others come from database
static int tar_getattr(const char *path, struct stat *stbuf)
{
	int res;
	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;
	return 0;
}
//TODO: 
// if the file does not exist in the database return (-1 * ENOENT)
// else if (mask == F_OK || mask == R_OK) return 0
// else return (-1 * EACCES);
static int tar_access(const char *path, int mask)
{
	//check if file exists
	if(fileexists) {
		if(mask == F_OK || mask == R_OK) {
			return 0; //right permission
		}
		else {
			return (-1 * EACCES); //wrong permission
		}
	}
	else {
		//file does not exist
		return (-1 * ENOENT);
	}
	return -1; //SHOULD NEVER REACH HERE
}
//TODO:
// perform operation of man readlink(2) 
// http://linux.die.net/man/2/readlink 
static int tar_readlink(const char *path, char *buf, size_t size)
{
	int res;
	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;
	buf[res] = '\0';
	return 0;
}
//TODO research more
static int tar_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *fi)
{
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
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}
	closedir(dp);
	return 0;
}

// attempts to make a special file
// NOT ALLOWED
static int tar_mknod(const char *path, mode_t mode, dev_t rdev)
{
	return (-1 * EACCES); //permission not allowed
}

// attempt to create directory
// NOT ALLOWED
static int tar_mkdir(const char *path, mode_t mode)
{
	return (-1 * EACCES); //permission not allowed
}

// attempt to delete a file and the name its associated with
// NOT ALLOWED
static int tar_unlink(const char *path)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to remove a directory
// NOT ALLOWED
static int tar_rmdir(const char *path)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to create symbolic link
static int tar_symlink(const char *from, const char *to)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to rename an entity
static int tar_rename(const char *from, const char *to)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to create a hard link
static int tar_link(const char *from, const char *to)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to change access permissions
static int tar_chmod(const char *path, mode_t mode)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to change owner of a file
static int tar_chown(const char *path, uid_t uid, gid_t gid)
{
	return (-1 * EACCES); //write permission not allowed
}

// attempt to truncate a file
static int tar_truncate(const char *path, off_t size)
{
	return (-1 * EACCES); //write permission not allowed
}

// changes file timestamp
#ifdef HAVE_UTIMENSAT
static int tar_utimens(const char *path, const struct timespec ts[2])
{
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
	int res;
	res = open(path, fi->flags);
	if (res == -1)
		return -errno;
	close(res);
	return 0;
}
// TODO: read “size” bytes from the file after moving 
// “offset” through the file, use math to determine the block
static int tar_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
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
	return res;
}
// attempt to open and write to a file
static int tar_write(const char *path, const char *buf, size_t size,
	off_t offset, struct fuse_file_info *fi)
{
	return (-1 * EACCES); //write permission not allowed
}

// TODO: Mimic statvfs(2), http://linux.die.net/man/2/statvfs 
static int tar_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;
	return 0;
}

// weird not allowed file altering
#ifdef HAVE_POSIX_FALLOCATE
static int tar_fallocate(const char *path, int mode,
	off_t offset, off_t length, struct fuse_file_info *fi)
{
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
