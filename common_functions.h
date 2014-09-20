#ifndef COMMON_FUNCTIONS_H
#define COMMON_FUNCTIONS_H

#define BYTES_IN_GB 1073741824
#define ENTRYNAMESIZE (sizeof(char) * 100)
#define FILELENGTHFIELDSIZE (sizeof(char) * 12)
#define USTARFIELDSIZE (sizeof(char) * 8)
#define PREFIXSIZE (sizeof(char) * 155)

long long int strtolonglong(char* string);

#endif
