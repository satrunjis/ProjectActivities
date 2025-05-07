#ifndef LZ77_H
#define LZ77_H

#include <stdio.h>
#include <stdint.h>

// Параметры алгоритма
#define MAX_BUFFER_SIZE_EXP 6
#define LOOKAHEAD_BUFFER_SIZE 0xff
#define MIN_MATCH_LENGTH 3
#define MAX_MATCH_INDICES 8
#define HASH_LOG 13
#define MAX_COPY (1 << 15)
#define HASH_TABLE_SIZE (1 << HASH_LOG)
#define MAX_MATCH_LENGTH (LOOKAHEAD_BUFFER_SIZE)
#define HASH_MASK (HASH_TABLE_SIZE - 1)
#define SEARCH_BUFFER_SIZE (HASH_TABLE_SIZE)
#define MAX_BUFFER_SIZE ((SEARCH_BUFFER_SIZE + LOOKAHEAD_BUFFER_SIZE) << 1)
#define HALF_BUFFER_SIZE (MAX_BUFFER_SIZE >> 1)

// Структура для хранения статистики блока
typedef struct {
    uint32_t block_number;    // Номер блока
    uint32_t bytes_read;      // Количество считанных байт
    uint32_t match_count;     // Количество совпадений
    uint32_t total_match_len; // Суммарная длина совпадений
} BlockStats;

// Узел связного списка для истории блоков
typedef struct BlockStatsNode {
    BlockStats stats;
    struct BlockStatsNode *next;
} BlockStatsNode;

int lz77_compress(FILE *input, FILE *output, FILE *log);
int lz77_decompress(FILE *input, FILE *output, FILE *log);

#endif

