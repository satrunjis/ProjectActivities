#ifndef LZ77_H
#define LZ77_H

#include <stddef.h>

int lz77_compress(FILE *input, FILE *output, FILE *log);
int lz77_decompress(FILE *input, FILE *output, FILE *log);

#endif
