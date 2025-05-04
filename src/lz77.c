#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// Параметры алгоритма
#define MAX_BUFFER_SIZE_EXP 6          // Экспонента размера половины буфера (2^6 = 64 байта)
#define LOOKAHEAD_BUFFER_SIZE (1 << 8) // Размер буфера предпросмотра
#define MIN_MATCH_LENGTH 3             // Минимальная длина совпадения
#define MAX_MATCH_INDICES 8            // Максимальное количество индексов совпадений
#define HASH_LOG 13                    // Логарифм размера хеш-таблицы для функции hash
#define MAX_COPY (1 << 15)             // Уменьшено до 32768 для согласованности

// Зависимые константы
#define HASH_TABLE_SIZE (1 << HASH_LOG)
#define MAX_MATCH_LENGTH (LOOKAHEAD_BUFFER_SIZE)
#define HASH_MASK (HASH_TABLE_SIZE - 1)
#define SEARCH_BUFFER_SIZE (HASH_TABLE_SIZE)

#define MAX_BUFFER_SIZE ((SEARCH_BUFFER_SIZE + LOOKAHEAD_BUFFER_SIZE) << 1)
#define HALF_BUFFER_SIZE (MAX_BUFFER_SIZE >> 1)

#define add_hash(x) hash_table[x][pos_in_hash_tabel[x]++ & 7] = pos_in_buf++
int block_num = 1, a = 0, b = 0, byte = 0;

uint16_t hash(uint8_t *ctx) // Хеш-функция с использованием числа Кнута
{
    return ((*((uint32_t *)ctx) & 0x00ffffff) * 2654435769LL) >> (32 - HASH_LOG) & HASH_MASK;
}

void print_literals(uint8_t *buffer_start, uint32_t start, uint32_t end, FILE *output, FILE *log)
{
    if (!output || start >= MAX_BUFFER_SIZE)
    {
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
            axiscyd = (chunk >> 7) & 0x7F;
            fwrite(&axiscyd, 1, 1, output);
            fprintf(log, "[DEBUG] print_literals: Writing literal: length=%u, pos=%u\n", chunk, start);
            uint32_t written = fwrite(ptr, 1, chunk, output);
            if (written != chunk)
            {
                fprintf(log, "[DEBUG] print_literals: ERROR: Failed to write %u bytes, wrote %d\n", chunk, written);
            }
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
        fprintf(log, "[DEBUG] lz77_compress: ERROR: input or output is NULL\n");
        return -1;
    }

    uint8_t buffer[MAX_BUFFER_SIZE + SEARCH_BUFFER_SIZE] = {0};
    uint32_t pos_in_buf = 0;
    uint16_t pos_in_sercle = 0;
    uint32_t buffer_refill_trigger[] = {SEARCH_BUFFER_SIZE, SEARCH_BUFFER_SIZE + HALF_BUFFER_SIZE, MAX_BUFFER_SIZE};
    uint32_t hash_table[HASH_TABLE_SIZE][MAX_MATCH_INDICES] = {0};
    uint8_t pos_in_hash_tabel[HASH_TABLE_SIZE] = {0};
    uint32_t last_pos_math = 0;
    int bytes_read;
    uint32_t match_count = 0;
    uint32_t old_pos_in_buf = 0;
    uint32_t main_loop_count = 0;
    uint32_t inner_loop_count = 0;
    uint32_t search_limit = SEARCH_BUFFER_SIZE;

    fprintf(log, "[DEBUG] lz77_compress: Initialized: MAX_BUFFER_SIZE=%d, HALF_BUFFER_SIZE=%d, SEARCH_BUFFER_SIZE=%d\n",
            MAX_BUFFER_SIZE, HALF_BUFFER_SIZE, SEARCH_BUFFER_SIZE);

    uint32_t border = buffer_refill_trigger[0];
    while ((bytes_read = fread(buffer + (HALF_BUFFER_SIZE) * (pos_in_sercle++ & 1), 1, HALF_BUFFER_SIZE, input)))
    {
        byte += bytes_read;
        main_loop_count++;
        fprintf(log, "[DEBUG] lz77_compress: Main loop #%u: read=%d bytes, pos_in_sercle=%u\n", main_loop_count, bytes_read, pos_in_sercle);
        if (main_loop_count > 10000)
        {
            fprintf(log, "[DEBUG] lz77_compress: ERROR: Main loop exceeded 10000 iterations\n");
            break;
        }

        if (pos_in_sercle & 1)
            memcpy(buffer + MAX_BUFFER_SIZE, buffer, SEARCH_BUFFER_SIZE);

        inner_loop_count = 0;
        if (bytes_read != HALF_BUFFER_SIZE)
        {
            buffer_refill_trigger[0] = bytes_read;
            buffer_refill_trigger[1] = bytes_read + HALF_BUFFER_SIZE;
            search_limit = bytes_read - 1;
            if (pos_in_sercle & 1 && !pos_in_buf)
                border = bytes_read;
            if ((pos_in_sercle & 1) == 0)
                border = buffer_refill_trigger[(pos_in_sercle ^ 1) & 1];
        }

        for (uint32_t ihash = hash(buffer + pos_in_buf), max_len_match = MIN_MATCH_LENGTH - 1, max_math_index = 0;
             (pos_in_buf <= border);
             ihash = hash(buffer + pos_in_buf), max_len_match = MIN_MATCH_LENGTH - 1, max_math_index = 0)
        {
            inner_loop_count++;
            if (inner_loop_count > 100000)
            {
                fprintf(log, "[DEBUG] lz77_compress: ERROR: Inner loop exceeded 100000 iterations, pos_in_buf=%u, border=%u\n",
                        pos_in_buf, border);
                return -1;
            }

            if (inner_loop_count % 1000 == 0)
                fprintf(log, "[DEBUG] lz77_compress: Inner loop #%u: pos_in_buf=%u, old_pos_in_buf=%u, border=%u\n",
                        inner_loop_count, pos_in_buf, old_pos_in_buf, border);

            old_pos_in_buf = pos_in_buf;
            if (pos_in_buf >= MAX_BUFFER_SIZE)
            {
                pos_in_buf -= MAX_BUFFER_SIZE;
                border = buffer_refill_trigger[0];
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

                while (buffer[pos_in_buf + j] == buffer[next_pos + j] && distance > j && j < search_limit && j < SEARCH_BUFFER_SIZE)
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
        border = buffer_refill_trigger[1 + ((pos_in_sercle ^ 1) & 1)];
        block_num++;
    }
    print_literals(buffer, last_pos_math, buffer_refill_trigger[pos_in_sercle & 1], output, log);
    fprintf(log, "[DEBUG] lz77_compress: Completed: total_bytes_read=%d, main_loops=%d, matches=%u\n", byte, block_num, match_count);
    return 0;
}

#define MAX_INPUT_SIZE (1024 * 1024)

int lz77_decompress(FILE *input, FILE *output, FILE *log)
{
    uint8_t input_buffer[MAX_INPUT_SIZE];
    uint8_t output_buffer[MAX_INPUT_SIZE] = {0};
    uint32_t input_size = 0;
    uint32_t input_pos = 0;

    input_size = fread(input_buffer, 1, MAX_INPUT_SIZE, input);
    fprintf(log, "[DEBUG] lz77_decompress: Read %u bytes from input\n", input_size);
    if (input_size == 0)
    {
        fprintf(log, "[DEBUG] lz77_decompress: ERROR: No data read from input\n");
        return -1;
    }
    uint32_t output_pos = 0;
    while (input_pos < input_size)
    {
        uint8_t type = input_buffer[input_pos] & 1;
        uint32_t count = input_buffer[input_pos++] >> 1;
        if (type)
        {
            count |= (input_buffer[input_pos++] << 7);
            fprintf(log, "[DEBUG] lz77_decompress: Reading literal: length=%u, input_pos=%u\n", count, input_pos);
            for (uint32_t i = 0; i < count && input_pos < input_size; i++)
                output_buffer[output_pos++] = input_buffer[input_pos++];
            if (input_pos > input_size)
                fprintf(log, "[DEBUG] lz77_decompress: WARNING: Input buffer overrun, input_pos=%u, input_size=%u\n", input_pos, input_size);
        }
        else
        {
            count |= (input_buffer[input_pos++] << 7);
            uint32_t count2 = input_buffer[input_pos++];
            fprintf(log, "[DEBUG] lz77_decompress: Reading match: distance=%u, length=%u, input_pos=%u\n", count, count2, input_pos);
            for (uint32_t i = 0; i < count2 && output_pos >= count; i++)
                {output_buffer[output_pos] = output_buffer[output_pos - count];output_pos++;}
            if (output_pos < count)
                fprintf(log, "[DEBUG] lz77_decompress: ERROR: Invalid match, output_pos=%u, distance=%u\n", output_pos, count);
            if (input_pos < input_size)
                output_buffer[output_pos++] = input_buffer[input_pos++];
        }
    }
    fprintf(log, "[DEBUG] lz77_decompress: Writing %u bytes to output\n", output_pos);
    fwrite(output_buffer, 1, output_pos, output);
    return 0;
}


