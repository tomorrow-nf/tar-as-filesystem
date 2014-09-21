#ifndef COMMON_FUNCTIONS_H
#define COMMON_FUNCTIONS_H

#define BYTES_IN_GB 1073741824
#define MEMBERNAMESIZE (sizeof(char) * 100)
#define FILELENGTHFIELDSIZE (sizeof(char) * 12)
#define USTARFIELDSIZE (sizeof(char) * 8)
#define PREFIXSIZE (sizeof(char) * 155)
#define BLOCKSIZE (sizeof(char) * 512)

long long int strtolonglong(char* string);

int analyze_tarfile(char* f_name);
int analyze_bz2(char* f_name);
int analyze_gzip(char* f_name);
int analyze_xz(char* f_name);

#endif
