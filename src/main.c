#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lz77.h"

// Цвета для вывода
#define RED "\033[0;31m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[0;33m"
#define RESET "\033[0m"

// Перечисление для режимов работы
typedef enum
{
    MODE_COMPRESS,
    MODE_DECOMPRESS,
    MODE_HELP
} OperationMode;

// Объединение для представления имени файла
typedef union
{
    char full_name[256];
    struct
    {
        char base_name[200];
        char extension[56];
    } parts;
} FileName;

void print_usage(void)
{
    printf(" ____ ____ ____ ____ \n");
    printf("||L |||Z |||7 |||7 ||\n");
    printf("||__|||__|||__|||__||\n");
    printf("|/__\\|/__\\|/__\\|/__\\|\n");
    printf("\n");
    printf("Утилита для сжатия и распаковки файлов с использованием алгоритма LZ77\n");
    printf("Использование:\n");
    printf("  lz77 [-f] -c <input_file>    Сжать файл (выход: <input_file>.lz, лог: <имя_без_расширения>_compress.log)\n");
    printf("  lz77 [-f] -d <input_file>.lz Распаковать файл (выход: d_<input_file>, лог: <имя_без_расширения>_unpack.log)\n");
    printf("  lz77 -h | --help             Показать справку\n");
    printf("Флаги:\n");
    printf("  -f                           Разрешить перезапись выходного файла и логов\n");
    printf("Примеры:\n");
    printf("  lz77 -c document.txt         → создаст document.txt.lz, document_compress.log\n");
    printf("  lz77 -d document.txt.lz      → создаст d_document.txt, document_unpack.log\n");
    printf("  lz77 -f -c document.txt      → перезапишет document.txt.lz и document_compress.log\n");
    printf("  lz77 -c ../word_direct/test_input.txt → обработает файл по указанному пути\n");
}

int lz77_compress(FILE *input, FILE *output, FILE *log);
int lz77_decompress(FILE *input, FILE *output, FILE *log);

long get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

int file_exists(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file)
    {
        fclose(file);
        return 1;
    }
    return 0;
}

// Парсинг имени файла и формирование выходных имён
int parse_filename(const char *input_filename, OperationMode mode, FileName *input_file, FileName *output_file, FileName *log_file)
{
    // Копирование имени входного файла
    strncpy(input_file->full_name, input_filename, sizeof(input_file->full_name) - 1);
    input_file->full_name[sizeof(input_file->full_name) - 1] = '\0';

    // Извлечение базового имени и расширения
    strncpy(input_file->parts.base_name, input_filename, sizeof(input_file->parts.base_name) - 1);
    input_file->parts.base_name[sizeof(input_file->parts.base_name) - 1] = '\0';
    char *dot = strrchr(input_file->parts.base_name, '.');
    if (dot)
    {
        if (mode == MODE_DECOMPRESS && strcmp(dot, ".lz") == 0)
        {
            *dot = '\0';
            char *prev_dot = strrchr(input_file->parts.base_name, '.');
            if (prev_dot)
            {
                strcpy(input_file->parts.extension, prev_dot);
                *prev_dot = '\0';
            }
            else
            {
                input_file->parts.extension[0] = '\0';
            }
        }
        else
        {
            strcpy(input_file->parts.extension, dot);
            *dot = '\0';
        }
    }
    else
    {
        input_file->parts.extension[0] = '\0';
    }

    // Формирование имени выходного файла
    if (mode == MODE_COMPRESS)
    {
        if (strstr(input_filename, ".lz"))
        {
            fprintf(stderr, YELLOW "Предупреждение: входной файл %s уже имеет расширение .lz\n" RESET, input_filename);
        }
        snprintf(output_file->full_name, sizeof(output_file->full_name), "%s.lz", input_filename);
    }
    else
    {
        if (!strstr(input_filename, ".lz"))
        {
            fprintf(stderr, RED "Ошибка: входной файл %s должен иметь расширение .lz\n" RESET, input_filename);
            return 1;
        }
        snprintf(output_file->full_name, sizeof(output_file->full_name), "d_%s%s", input_file->parts.base_name, input_file->parts.extension);
    }

    // Формирование имени лог-файла
    snprintf(log_file->full_name, sizeof(log_file->full_name), "%s_%s.log", 
             input_file->parts.base_name, mode == MODE_COMPRESS ? "compress" : "unpack");

    return 0;
}

// Открытие файлов с проверкой
int open_files(const char *input_filename, const char *output_filename, const char *log_filename, 
               int force_overwrite, FILE **input_file, FILE **output_file, FILE **log_file)
{
    *input_file = fopen(input_filename, "rb");
    if (!*input_file)
    {
        fprintf(stderr, RED "Ошибка: не удалось открыть входной файл %s\n" RESET, input_filename);
        return 1;
    }

    if (!force_overwrite && file_exists(output_filename))
    {
        fprintf(stderr, RED "Ошибка: выходной файл %s уже существует. Используйте -f для перезаписи\n" RESET, output_filename);
        fclose(*input_file);
        return 1;
    }

    *output_file = fopen(output_filename, "wb");
    if (!*output_file)
    {
        fprintf(stderr, RED "Ошибка: не удалось создать выходной файл %s\n" RESET, output_filename);
        fclose(*input_file);
        return 1;
    }

    if (!force_overwrite && file_exists(log_filename))
    {
        fprintf(stderr, RED "Ошибка: лог-файл %s уже существует. Используйте -f для перезаписи\n" RESET, log_filename);
        fclose(*input_file);
        fclose(*output_file);
        return 1;
    }

    *log_file = fopen(log_filename, "w");
    if (!*log_file)
    {
        fprintf(stderr, YELLOW "Предупреждение: не удалось создать лог-файл %s, логи не будут записаны\n" RESET, log_filename);
        *log_file = NULL;
    }
    else
    {
        fprintf(stderr, GREEN "Логирование в %s\n" RESET, log_filename);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    OperationMode mode = MODE_HELP;
    int force_overwrite = 0;
    char *input_filename = NULL;
    FileName input_file, output_file, log_file;

    // Обработка аргументов
    if (argc < 2 || argc > 4)
    {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        mode = MODE_HELP;
    }
    else if (argc == 4 && strcmp(argv[1], "-f") == 0)
    {
        force_overwrite = 1;
        if (strcmp(argv[2], "-c") == 0)
            mode = MODE_COMPRESS;
        else if (strcmp(argv[2], "-d") == 0)
            mode = MODE_DECOMPRESS;
        else
        {
            fprintf(stderr, RED "Ошибка: неизвестный режим %s\n" RESET, argv[2]);
            print_usage();
            return 1;
        }
        input_filename = argv[3];
    }
    else if (argc == 3)
    {
        if (strcmp(argv[1], "-c") == 0)
            mode = MODE_COMPRESS;
        else if (strcmp(argv[1], "-d") == 0)
            mode = MODE_DECOMPRESS;
        else
        {
            fprintf(stderr, RED "Ошибка: неизвестный режим %s\n" RESET, argv[1]);
            print_usage();
            return 1;
        }
        input_filename = argv[2];
    }
    else
    {
        fprintf(stderr, RED "Ошибка: неверное количество аргументов\n" RESET);
        print_usage();
        return 1;
    }

    // Основная логика
    if (mode == MODE_HELP)
    {
        print_usage();
        return 0;
    }

    // Парсинг имени файла
    if (parse_filename(input_filename, mode, &input_file, &output_file, &log_file) != 0)
        return 1;

    // Открытие файлов
    FILE *input_file_ptr = NULL, *output_file_ptr = NULL, *log_file_ptr = NULL;
    if (open_files(input_filename, output_file.full_name, log_file.full_name, 
                   force_overwrite, &input_file_ptr, &output_file_ptr, &log_file_ptr) != 0)
        return 1;

    // Получение размера входного файла
    long input_size = get_file_size(input_filename);
    if (input_size < 0)
    {
        fprintf(stderr, RED "Ошибка: не удалось определить размер входного файла\n" RESET);
        fclose(input_file_ptr);
        fclose(output_file_ptr);
        if (log_file_ptr)
            fclose(log_file_ptr);
        return 1;
    }

    // Выполнение операции
    int result;
    if (mode == MODE_COMPRESS)
    {
        printf("Сжатие %s → %s...\n", input_filename, output_file.full_name);
        result = lz77_compress(input_file_ptr, output_file_ptr, log_file_ptr);
        if (result == 0)
        {
            fflush(output_file_ptr);
            long output_size = get_file_size(output_file.full_name);
            printf(GREEN "Сжатие успешно завершено: %s\n" RESET, output_file.full_name);
            printf("Размер исходного файла: %ld байт\n", input_size);
            printf("Размер сжатого файла: %ld байт\n", output_size);
            if (input_size > 0)
                printf("Сжатие: %.2f%%\n", 100.0 * output_size / input_size);
        }
        else
        {
            fprintf(stderr, RED "Ошибка сжатия\n" RESET);
        }
    }
    else
    {
        printf("Распаковка %s → %s...\n", input_filename, output_file.full_name);
        result = lz77_decompress(input_file_ptr, output_file_ptr, log_file_ptr);
        if (result == 0)
        {
            fflush(output_file_ptr);
            long output_size = get_file_size(output_file.full_name);
            printf(GREEN "Распаковка успешно завершена: %s\n" RESET, output_file.full_name);
            printf("Размер сжатого файла: %ld байт\n", input_size);
            printf("Размер распакованного файла: %ld байт\n", output_size);
        }
        else
        {
            fprintf(stderr, RED "Ошибка распаковки\n" RESET);
        }
    }

    // Закрытие файлов
    fclose(input_file_ptr);
    fclose(output_file_ptr);
    if (log_file_ptr)
        fclose(log_file_ptr);

    return result;
}


