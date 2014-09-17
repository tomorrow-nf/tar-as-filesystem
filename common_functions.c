#include "common_functions.h"
#include <string.h>

/*
    converts a string like "0000014" to a long integer
      - must be long long int because the string can be 11 characters long
*/
long long int strtolonglong(char* string) {
    long long int total = 0;
    int i;
    long long int tmp = 0;
    long long int tens = 1;

    for(i=(strlen(string) - 1); i>=0; i--) {
        if(string[i] == '0') tmp = 0;
        else if(string[i] == '1') tmp = 1;
        else if(string[i] == '2') tmp = 2;
        else if(string[i] == '3') tmp = 3;
        else if(string[i] == '4') tmp = 4;
        else if(string[i] == '5') tmp = 5;
        else if(string[i] == '6') tmp = 6;
        else if(string[i] == '7') tmp = 7;
        else if(string[i] == '8') tmp = 8;
        else if(string[i] == '9') tmp = 9;
        else {
            tmp = 11;
        }

        if(tmp == 11) {
            total = 0;
            break;
        }
        else {
            total = total + (tmp * tens);
            tens = tens * 10;
        }
    }

    return total;
}
