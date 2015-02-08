#ifndef COMMON_FUNCTIONS_H
#define COMMON_FUNCTIONS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define BYTES_IN_GB 1073741824
#define MEMBERNAMESIZE (sizeof(char) * 100)
#define FILELENGTHFIELDSIZE (sizeof(char) * 12)
#define USTARFIELDSIZE (sizeof(char) * 8)
#define PREFIXSIZE (sizeof(char) * 155)
#define BLOCKSIZE (sizeof(char) * 512)

// www.mkssoftware.com/docs/man4/tar.4.asp
struct headerblock {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char typeflag[1];
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char trash[12];
};

long long int strtolonglong(char* string);

int analyze_tarfile(char* f_name, struct stat filestats);
int analyze_bz2(char* f_name, struct stat filestats);
//int analyze_gzip(char* f_name, struct stat filestats);
int analyze_xz(char* f_name, struct stat filestats);

int extract_xz_member(char* filename, int blocknum, long long int offset, long long int size);



#endif
