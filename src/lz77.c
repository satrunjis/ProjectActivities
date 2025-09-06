#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "lz77.h"

// Глобальные переменные
int block_num = 1, byte = 0;

// Добавление статистики блока
void add_block_stats(BlockStatsNode **head, uint32_t bytes_read, uint32_t match_count, uint32_t match_len, FILE *log)
{
    BlockStatsNode *node = malloc(sizeof(BlockStatsNode));
    if (!node)
    {
        if (log)
            fprintf(log, "[DEBUG] add_block_stats: ERROR: Failed to allocate BlockStatsNode\n");
        return;
    }
    node->stats.block_number = block_num;
    node->stats.bytes_read = bytes_read;
    node->stats.match_count = match_count;
    node->stats.total_match_len = match_len;
    node->next = *head;
    *head = node;
}

// Освобождение списка статистики
void free_block_stats(BlockStatsNode **head)
{
    BlockStatsNode *current = *head;
    while (current)
    {
        BlockStatsNode *temp = current;
        current = current->next;
        free(temp);
    }
    *head = NULL;
}

// Вывод статистики в лог
void log_block_stats(BlockStatsNode *head, FILE *log)
{
    if (!log || !head)
        return;
    uint32_t total_blocks = 0, total_bytes = 0, total_matches = 0, total_len = 0;
    BlockStatsNode *current = head;
    fprintf(log, "[DEBUG --- LZ77 Compression Block Statistics --- ]\n");
    while (current)
    {
        total_blocks++;
        total_bytes += current->stats.bytes_read;
        total_matches += current->stats.match_count;
        total_len += current->stats.total_match_len;
        fprintf(log, "\t Block #%u: bytes_read=%u, matches=%u, total_match_len=%u\n",
                current->stats.block_number, current->stats.bytes_read,
                current->stats.match_count, current->stats.total_match_len);
        current = current->next;
    }
    float avg_match_len = total_matches ? (float)total_len / total_matches : 0;
    fprintf(log, "[DEBUG] lz77_compress: Summary: blocks=%u, total_bytes=%u, total_matches=%u, avg_match_len=%.2f\n",
            total_blocks, total_bytes, total_matches, avg_match_len);
}

#define add_hash(x) hash_table[x][pos_in_hash_tabel[x]++ & 7] = pos_in_buf++

uint16_t hash(uint8_t *ctx)
{
    return ((*((uint32_t *)ctx) & 0x00ffffff) * 2654435769LL) >> (32 - HASH_LOG) & HASH_MASK;
}

void print_literals(uint8_t *buffer_start, uint32_t start, uint32_t end, FILE *output, FILE *log)
{
    if (!output || start >= MAX_BUFFER_SIZE)
    {
        if (log)
            fprintf(log, "[DEBUG] print_literals: ERROR: output is NULL or start (%u) >= MAX_BUFFER_SIZE (%u)\n", start, MAX_BUFFER_SIZE);
        return;
    }

    uint32_t len = end < start ? MAX_BUFFER_SIZE - start : end - start;
    uint8_t *ptr = buffer_start + start;

    do
    {
        while (len)
        {
            uint32_t chunk = len < MAX_COPY ? len : MAX_COPY;
            uint8_t axiscyd = (chunk & 0x7F) << 1 | 1;
            fwrite(&axiscyd, 1, 1, output);
            axiscyd = (chunk >> 7) & 0xFF;
            fwrite(&axiscyd, 1, 1, output);
            if (log)
                fprintf(log, "[DEBUG] print_literals: Writing literal: length=%u, pos=%u\n", chunk, start);
            uint32_t written = fwrite(ptr, 1, chunk, output);
            if (written != chunk && log)
                fprintf(log, "[DEBUG] print_literals: ERROR: Failed to write %u bytes, wrote %d\n", chunk, written);
            ptr += chunk;
            len -= chunk;
        }
        if (end < start && end)
            len = end, ptr = buffer_start, end = 0;
        else
            break;
    } while (len);
}

int lz77_compress(FILE *input, FILE *output, FILE *log)
{
    if (!input || !output)
    {
        if (log)
            fprintf(log, "[DEBUG] lz77_compress: ERROR: input or output is NULL\n");
        return -1;
    }

    uint8_t buffer[MAX_BUFFER_SIZE + LOOKAHEAD_BUFFER_SIZE] = {0};

    uint32_t pos_in_buf = 0;
    uint16_t cycle_pos = 0;
    uint32_t buffer_refill_trigger[] = {SEARCH_BUFFER_SIZE + 1, SEARCH_BUFFER_SIZE + HALF_BUFFER_SIZE + 1, MAX_BUFFER_SIZE + 1};
    uint32_t hash_table[HASH_TABLE_SIZE][MAX_MATCH_INDICES] = {0};
    uint8_t pos_in_hash_tabel[HASH_TABLE_SIZE] = {0};
    uint32_t last_pos_math = 0;
    int bytes_read;
    uint32_t match_count = 0;
    uint32_t total_match_len = 0;
    uint32_t old_pos_in_buf = 0;
    uint32_t main_loop_count = 0;
    uint32_t inner_loop_count = 0;
    uint32_t search_limit = SEARCH_BUFFER_SIZE;
    BlockStatsNode *block_stats = NULL;

    if (log)
        fprintf(log, "[DEBUG] lz77_compress: Initialized: MAX_BUFFER_SIZE=%d, HALF_BUFFER_SIZE=%d, SEARCH_BUFFER_SIZE=%d\n",
                MAX_BUFFER_SIZE, HALF_BUFFER_SIZE, SEARCH_BUFFER_SIZE);

    uint32_t border = buffer_refill_trigger[0];
    while ((bytes_read = fread(buffer + (HALF_BUFFER_SIZE) * (cycle_pos++ & 1), 1, HALF_BUFFER_SIZE, input)))
    {
        byte += bytes_read;
        main_loop_count++;
        if (log)
            fprintf(log, "[DEBUG] lz77_compress: Main loop #%u: read=%d bytes, cycle_pos=%u\n", main_loop_count, bytes_read, cycle_pos);
        if (0) // main_loop_count > 10000
        {
            if (log)
                fprintf(log, "[DEBUG] lz77_compress: ERROR: Main loop exceeded 10000 iterations\n");
            free_block_stats(&block_stats);
            return -1;
        }

        if (cycle_pos & 1)
            memcpy(buffer + MAX_BUFFER_SIZE, buffer, LOOKAHEAD_BUFFER_SIZE);

        match_count = 0;
        total_match_len = 0;
        if (bytes_read != HALF_BUFFER_SIZE)
        {
            buffer_refill_trigger[0] = bytes_read;
            buffer_refill_trigger[1] = bytes_read + HALF_BUFFER_SIZE;
            search_limit = bytes_read - 1;
            if (cycle_pos & 1 && !pos_in_buf)
                border = bytes_read;
            if ((cycle_pos & 1) == 0)
                border = buffer_refill_trigger[(cycle_pos ^ 1) & 1];
        }
        inner_loop_count = 0;
        for (uint32_t ihash = hash(buffer + pos_in_buf), max_len_match = MIN_MATCH_LENGTH - 1, max_math_index = 0;
             (pos_in_buf < border);
             ihash = hash(buffer + pos_in_buf), max_len_match = MIN_MATCH_LENGTH - 1, max_math_index = 0)
        {
            inner_loop_count++;
            if (inner_loop_count > 20000)
            {
                if (log)
                    fprintf(log, "[DEBUG] lz77_compress: ERROR: Inner loop exceeded 100000 iterations, pos_in_buf=%u, border=%u\n",
                            pos_in_buf, border);
                free_block_stats(&block_stats);
                return -1;
            }

            if (inner_loop_count % 1000 == 0 && log)
                fprintf(log, "[DEBUG] lz77_compress: Inner loop #%u: pos_in_buf=%u, old_pos_in_buf=%u, border=%u\n",
                        inner_loop_count, pos_in_buf, old_pos_in_buf, border);

            old_pos_in_buf = pos_in_buf;
            if (pos_in_buf >= MAX_BUFFER_SIZE)
            {
                pos_in_buf -= MAX_BUFFER_SIZE;
                border = buffer_refill_trigger[0];
                if (log)
                    fprintf(log, "[DEBUG] lz77_compress: Buffer wrap: pos_in_buf=%u, new border=%u\n", pos_in_buf, border);
            }
            if (bytes_read != HALF_BUFFER_SIZE)
                search_limit = border - pos_in_buf - 1;

            for (uint32_t j = 0, i = 0, distance, next_pos; i < MAX_MATCH_INDICES; i++, j = 0)
            {
                next_pos = hash_table[ihash][i];
                distance = (pos_in_buf >= next_pos) ? (pos_in_buf - next_pos) : (pos_in_buf + MAX_BUFFER_SIZE - next_pos);

                if (distance < MIN_MATCH_LENGTH || distance > SEARCH_BUFFER_SIZE)
                    continue;

                while (buffer[pos_in_buf + j] == buffer[next_pos + j] && distance > j && j < search_limit && j < LOOKAHEAD_BUFFER_SIZE)
                    j++;

                if (j > max_len_match)
                {
                    max_len_match = j;
                    max_math_index = distance;
                }
            }

            if (max_len_match >= MIN_MATCH_LENGTH)
            {
                print_literals(buffer, last_pos_math, pos_in_buf, output, log);
                match_count++;
                total_match_len += max_len_match;
                if (log)
                    fprintf(log, "[DEBUG] lz77_compress: Writing match: distance=%u, length=%u, next_char=%c, pos_in_buf=%u\n",
                            max_math_index, max_len_match, buffer[pos_in_buf], pos_in_buf);
                for (uint32_t i = 0; i < max_len_match; i++, ihash = hash(buffer + pos_in_buf))
                    add_hash(ihash);
                uint8_t axiscyd = (max_math_index & 0x7F) << 1;
                fwrite(&axiscyd, 1, 1, output);
                axiscyd = max_math_index >> 7;
                fwrite(&axiscyd, 1, 1, output);
                axiscyd = max_len_match;
                fwrite(&axiscyd, 1, 1, output);
                fwrite(&buffer[pos_in_buf], 1, 1, output);
                last_pos_math = pos_in_buf + 1;
                if (last_pos_math >= MAX_BUFFER_SIZE)
                    last_pos_math -= MAX_BUFFER_SIZE;
            }

            add_hash(ihash);
        }
        if (pos_in_buf >= MAX_BUFFER_SIZE)
            pos_in_buf -= MAX_BUFFER_SIZE;
        uint32_t distance = (pos_in_buf >= last_pos_math) ? (pos_in_buf - last_pos_math) : (pos_in_buf + MAX_BUFFER_SIZE - last_pos_math);
        if (distance >= SEARCH_BUFFER_SIZE)
        {
            print_literals(buffer, last_pos_math, pos_in_buf, output, log);
            last_pos_math = pos_in_buf;
        }
        border = buffer_refill_trigger[1 + ((cycle_pos ^ 1) & 1)];

        // Сохранение статистики блока
        add_block_stats(&block_stats, bytes_read, match_count, total_match_len, log);
        block_num++;
    }
    print_literals(buffer, last_pos_math, buffer_refill_trigger[cycle_pos & 1], output, log);
    if (log)
        fprintf(log, "[DEBUG] lz77_compress: Completed: total_bytes_read=%d, blocks=%d, matches=%u\n", byte, block_num, match_count);

    // Вывод статистики блоков
    log_block_stats(block_stats, log);
    free_block_stats(&block_stats);
    return 0;
}

void cpy(uint8_t *output_buffer, int32_t pos, int32_t offset, int32_t len)
{
    if (offset <= 0 || offset > MAX_OUTPUT_BUFFER_SIZE || len <= 0 || len > MAX_OUTPUT_BUFFER_SIZE)
    {
        fprintf(stderr, "[ERROR] cpy: Invalid offset=%d or len=%d\n", offset, len);
        return;
    }
    int32_t top_pos = (pos - offset) & OUTPUT_BUF_MASK;
    if (top_pos + len > MAX_OUTPUT_BUFFER_SIZE) {
        int first_right = MAX_OUTPUT_BUFFER_SIZE - top_pos;
        memcpy(output_buffer + pos, output_buffer + top_pos, first_right);
        memcpy(output_buffer+ pos+ first_right, output_buffer, len - first_right);
    } else {
        if(len+pos > MAX_OUTPUT_BUFFER_SIZE){
            int first_right = MAX_OUTPUT_BUFFER_SIZE - pos;
            memcpy(output_buffer + pos, output_buffer + top_pos, first_right);
            memcpy(output_buffer, output_buffer + top_pos+first_right, len - first_right);
        }else
            memcpy(output_buffer + pos, output_buffer + top_pos, len);
    }

}

int lz77_decompress(FILE *input, FILE *output, FILE *log)
{
    uint8_t output_buffer[MAX_OUTPUT_BUFFER_SIZE + HALF_OUTPUT_BUFFER_SIZE] = {0};
    int32_t cycle_pos = 1;
    int32_t dwdiw[] = {HALF_OUTPUT_BUFFER_SIZE, 0};
    int32_t pos = 0;
    int32_t count;
    int32_t byte;
    int64_t total_written = 0;
    int64_t total_written_to_file = 0;
    int32_t block_number = 0;

    if (log)
    {
        fprintf(log, "[INFO] Starting decompression\n");
    }

    while ((byte = fgetc(input)) != EOF)
    {
        count = byte >> 1;
        if (byte & 1)
        { // Литерал
            int next_byte = fgetc(input);
            if (next_byte == EOF)
            {
                fprintf(stderr, "[ERROR] EOF at literal length\n");
                return -1;
            }
            count |= next_byte << 7;
            if (count <= 0 || count > MAX_LITERAL_LENGTH)
            {
                fprintf(stderr, "[ERROR] Invalid literal length=%d\n", count);
                return -1;
            }

            size_t bytes_to_read = count;
            while (bytes_to_read > 0)
            {
                size_t chunk = bytes_to_read;
                if (pos + chunk > MAX_OUTPUT_BUFFER_SIZE)
                {
                    chunk = MAX_OUTPUT_BUFFER_SIZE - pos;
                }
                if (fread(output_buffer + pos, 1, chunk, input) != chunk)
                {
                    fprintf(stderr, "[ERROR] Read error at literal\n");
                    return -1;
                }
                total_written += chunk;
                bytes_to_read -= chunk;
                pos = (pos + chunk) & OUTPUT_BUF_MASK;
            }
        }
        else
        { // Совпадения
            int next_byte = fgetc(input);
            if (next_byte == EOF)
            {
                fprintf(stderr, "[ERROR] EOF at match distance\n");
                return -1;
            }
            count |= (next_byte << 7);
            if (count <= 0 || count > MAX_OUTPUT_BUFFER_SIZE)
            {
                fprintf(stderr, "[ERROR] Invalid match distance=%d\n", count);
                return -1;
            }

            int32_t len = fgetc(input);
            if (len == EOF)
            {
                fprintf(stderr, "[ERROR] EOF at match length\n");
                return -1;
            }
            if (len < MIN_MATCH_LENGTH || len > LOOKAHEAD_BUFFER_SIZE)
            {
                fprintf(stderr, "[ERROR] Invalid match length=%u\n", len);
                return -1;
            }

            cpy(output_buffer, pos, count, len);
            total_written += len;
            pos = (len + pos) & OUTPUT_BUF_MASK;

            int next_char = fgetc(input);
            if (next_char == EOF)
            {
                fprintf(stderr, "[ERROR] EOF at next char\n");
                return -1;
            }
            output_buffer[pos] = next_char;
            total_written++;
            pos = (1 + pos) & OUTPUT_BUF_MASK;
        }

        if (((cycle_pos & 1) && pos >= HALF_OUTPUT_BUFFER_SIZE) || (!(cycle_pos & 1) && pos < HALF_OUTPUT_BUFFER_SIZE))
        {
            size_t written = fwrite(output_buffer + dwdiw[cycle_pos & 1], 1, HALF_OUTPUT_BUFFER_SIZE, output);
            if (written != HALF_OUTPUT_BUFFER_SIZE)
            {
                fprintf(stderr, "[ERROR] Write error, written=%zu\n", written);
                return -1;
            }
            total_written_to_file += written;
            if (log)
            {
                fprintf(log, "[INFO] Block %d written, size=%zu, total_written_to_file=%lld\n",
                        block_number, written, total_written_to_file);
            }
            block_number++;
            cycle_pos++;
        }
    }

    int64_t remaining = total_written - total_written_to_file;
    if (remaining < 0 || remaining > HALF_OUTPUT_BUFFER_SIZE)
    {
        fprintf(stderr, "[ERROR] Invalid remaining: %lld\n", remaining);
        return -1;
    }
    if (remaining > 0)
    {
        int32_t src_offset = dwdiw[cycle_pos & 1];
        if (pos < src_offset)
        {
            src_offset = (src_offset == HALF_OUTPUT_BUFFER_SIZE) ? 0 : HALF_OUTPUT_BUFFER_SIZE;
        }
        size_t written = fwrite(output_buffer + src_offset, 1, remaining, output);
        if (written != remaining)
        {
            fprintf(stderr, "[ERROR] Final write error, written=%zu\n", written);
            return -1;
        }
        total_written_to_file += written;
        if (log)
        {
            fprintf(log, "[INFO] Final block written, size=%zu, total_written_to_file=%lld\n",
                    written, total_written_to_file);
        }
    }

    if (log)
    {
        fprintf(log, "[INFO] Decompression completed, total_written=%lld, total_written_to_file=%lld, blocks=%d\n",
                total_written, total_written_to_file, block_number);
    }
    return 0;
}
