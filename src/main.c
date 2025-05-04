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

void print_usage(void)
{
    printf("\n");
    printf(" ____ ____ ____ ____ \n");
    printf("||L |||Z |||7 |||7 ||\n");
    printf("||__|||__|||__|||__||\n");
    printf("|/__\\|/__\\|/__\\|/__\\|\n");
    printf("\n");
    printf("Утилита для сжатия и распаковки файлов с использованием алгоритма LZ77\n");
    printf("Использование:\n");
    printf("  lz77 [-f] -c <input_file>    Сжать файл (выход: <input_file>.lz)\n");
    printf("  lz77 [-f] -d <input_file>.lz Распаковать файл (выход: d_<input_file>)\n");
    printf("  lz77 -h                      Показать справку\n");
    printf("Флаги:\n");
    printf("  -f                           Разрешить перезапись выходного файла\n");
    printf("Примеры:\n");
    printf("  lz77 -c document.txt         → создаст document.txt.lz\n");
    printf("  lz77 -d document.txt.lz      → создаст d_document.txt\n");
    printf("  lz77 -f -c document.txt      → перезапишет document.txt.lz, если существует\n");
    printf("  lz77 -c ../word_direct/test_input.txt → обработает файл по указанному пути\n");
}

// Предполагается, что эти функции определены в lz77.h
int lz77_compress(FILE *input, FILE *output, FILE *log);
int lz77_decompress(FILE *input, FILE *output, FILE *log);

// Получение размера файла
long get_file_size(const char *filename)
{
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

// Проверка существования файла
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

int main(int argc, char *argv[])
{
    int force_overwrite = 0;
    char *mode = NULL;
    char *input_filename = NULL;

    // Обработка аргументов
    if (argc < 2 || argc > 4)
    {
        print_usage();
        return 1;
    }

    // Проверка флага -h
    if (strcmp(argv[1], "-h") == 0)
    {
        print_usage();
        return 0;
    }

    // Разбор аргументов
    if (argc == 4 && strcmp(argv[1], "-f") == 0)
    {
        force_overwrite = 1;
        mode = argv[2];
        input_filename = argv[3];
    }
    else if (argc == 3)
    {
        mode = argv[1];
        input_filename = argv[2];
    }
    else
    {
        fprintf(stderr, RED "Ошибка: неверное количество аргументов\n" RESET);
        print_usage();
        return 1;
    }

    // Проверка режима
    if (strcmp(mode, "-c") != 0 && strcmp(mode, "-d") != 0)
    {
        fprintf(stderr, RED "Ошибка: неизвестный режим %s\n" RESET, mode);
        print_usage();
        return 1;
    }

    char output_filename[256];
    char log_filename[256];
    char base_name[256];
    char extension[256] = "";

    // Извлечение имени файла и расширения
    strncpy(base_name, input_filename, sizeof(base_name) - 1);
    base_name[sizeof(base_name) - 1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot && strcmp(dot, ".lz") != 0) // Сохраняем расширение, если не .lz
    {
        strcpy(extension, dot);
        *dot = '\0';
    }
    else if (dot && strcmp(dot, ".lz") == 0)
    {
        *dot = '\0'; // Удаляем .lz для распаковки
    }

    // Формирование имени выходного файла
    if (strcmp(mode, "-c") == 0)
    {
        // Проверка, что входной файл не имеет .lz
        if (strstr(input_filename, ".lz"))
        {
            fprintf(stderr, YELLOW "Предупреждение: входной файл %s уже имеет расширение .lz\n" RESET, input_filename);
        }
        snprintf(output_filename, sizeof(output_filename), "%s.lz", input_filename);
    }
    else
    {
        // Проверка, что входной файл имеет .lz
        if (!strstr(input_filename, ".lz"))
        {
            fprintf(stderr, RED "Ошибка: входной файл %s должен иметь расширение .lz\n" RESET, input_filename);
            return 1;
        }
        snprintf(output_filename, sizeof(output_filename), "d_%s%s", base_name, extension);
    }

    // Формирование имени лог-файла
    snprintf(log_filename, sizeof(log_filename), "%s.log", input_filename);

    // Проверка существования входного файла
    FILE *input_file = fopen(input_filename, "rb");
    if (!input_file)
    {
        fprintf(stderr, RED "Ошибка: не удалось открыть входной файл %s\n" RESET, input_filename);
        return 1;
    }

    // Проверка существования выходного файла
    if (!force_overwrite && file_exists(output_filename))
    {
        fprintf(stderr, RED "Ошибка: выходной файл %s уже существует. Используйте -f для перезаписи\n" RESET, output_filename);
        fclose(input_file);
        return 1;
    }

    // Проверка существования лог-файла
    if (!force_overwrite && file_exists(log_filename))
    {
        fprintf(stderr, RED "Ошибка: лог-файл %s уже существует. Используйте -f для перезаписи\n" RESET, log_filename);
        fclose(input_file);
        return 1;
    }

    FILE *output_file = fopen(output_filename, "wb");
    if (!output_file)
    {
        fprintf(stderr, RED "Ошибка: не удалось создать выходной файл %s\n" RESET, output_filename);
        fclose(input_file);
        return 1;
    }

    // Открытие лог-файла
    FILE *log_file = fopen(log_filename, "w");
    if (!log_file)
    {
        fprintf(stderr, YELLOW "Предупреждение: не удалось создать лог-файл %s, логи не будут записаны\n" RESET, log_filename);
        log_file = NULL; // Продолжаем без логов
    }
    else
    {
        fprintf(stderr, GREEN "Логирование в %s\n" RESET, log_filename);
    }

    // Получение размера входного файла
    long input_size = get_file_size(input_filename);
    if (input_size < 0)
    {
        fprintf(stderr, RED "Ошибка: не удалось определить размер входного файла\n" RESET);
        fclose(input_file);
        fclose(output_file);
        if (log_file)
            fclose(log_file);
        return 1;
    }

    int result;
    if (strcmp(mode, "-c") == 0)
    {
        printf("Сжатие %s → %s...\n", input_filename, output_filename);
        result = lz77_compress(input_file, output_file, log_file);
        if (result == 0)
        {
            fclose(input_file);
            fclose(output_file);
            if (log_file)
                fclose(log_file);
            long output_size = get_file_size(output_filename);
            printf(GREEN "Сжатие успешно завершено: %s\n" RESET, output_filename);
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
        printf("Распаковка %s → %s...\n", input_filename, output_filename);
        result = lz77_decompress(input_file, output_file, log_file);
        if (result == 0)
        {
            fclose(input_file);
            fclose(output_file);
            if (log_file)
                fclose(log_file);
            long output_size = get_file_size(output_filename);
            printf(GREEN "Распаковка успешно завершена: %s\n" RESET, output_filename);
            printf("Размер сжатого файла: %ld байт\n", input_size);
            printf("Размер распакованного файла: %ld байт\n", output_size);
        }
        else
        {
            fprintf(stderr, RED "Ошибка распаковки\n" RESET);
        }
    }

    fclose(input_file);
    fclose(output_file);
    if (log_file)
        fclose(log_file);
    return result;
}

