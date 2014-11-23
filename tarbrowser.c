#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>

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
	int res;
	res = access(path, mask);
	if (res == -1)
		return -errno;
	return 0;
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

// TODO: RETURN ERROR
static int tar_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
/* On Linux this could just be 'mknod(path, mode, rdev)' but this
is more portable */
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
	res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_mkdir(const char *path, mode_t mode)
{
	int res;
	res = mkdir(path, mode);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_unlink(const char *path)
{
	int res;
	res = unlink(path);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_rmdir(const char *path)
{
	int res;
	res = rmdir(path);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_symlink(const char *from, const char *to)
{
	int res;
	res = symlink(from, to);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_rename(const char *from, const char *to)
{
	int res;
	res = rename(from, to);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_link(const char *from, const char *to)
{
	int res;
	res = link(from, to);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_chmod(const char *path, mode_t mode)
{
	int res;
	res = chmod(path, mode);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
static int tar_truncate(const char *path, off_t size)
{
	int res;
	res = truncate(path, size);
	if (res == -1)
		return -errno;
	return 0;
}
// TODO: RETURN ERROR
#ifdef HAVE_UTIMENSAT
static int tar_utimens(const char *path, const struct timespec ts[2])
{
	int res;
/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;
	return 0;
}
#endif

//TODO
// if fi->flags != RD_ONLY return ERROR
// else check for existance of file in appropriate table, if not found return ERROR
//         else put file’s ID number into the fuse file structure
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
// TODO: ERROR
static int tar_write(const char *path, const char *buf, size_t size,
	off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	(void) fi;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;
	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;
	close(fd);
	return res;
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
// TODO ERROR
#ifdef HAVE_POSIX_FALLOCATE
static int tar_fallocate(const char *path, int mode,
	off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;
	(void) fi;
	if (mode)
		return -EOPNOTSUPP;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;
	res = -posix_fallocate(fd, offset, length);
	close(fd);
	return res;
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