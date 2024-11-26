#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clox_lib/common.h"
#include "clox_lib/vm.h"

static void repl(Vm *vm)
{
    char line[1024];
    for (;;)
    {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }
        interpret(vm, line);
    }
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read file \"%s\".\n", path);
        exit(74);
    }

    size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
    if (bytes_read < file_size)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

static void run_file(Vm *vm, const char *path)
{
    char *source = read_file(path);
    InterpretResult result = interpret(vm, source);
    free(source);
    if (result == INTERPRET_COMPILE_ERROR)
    {
        exit(65);
    }
    if (result == INTERPRET_RUNTIME_ERROR)
    {
        exit(70);
    }
}

int main(int argc, const char *argv[])
{
    Vm vm;
    init_vm(&vm);

    if (argc == 1)
    {
        repl(&vm);
    }
    else if (argc == 2)
    {
        run_file(&vm, argv[1]);
    }
    else
    {
        fprintf(stderr, "Usage: clox [path]\n");
        free_vm(&vm);
        exit(64);
    }

    free_vm(&vm);
    return 0;
}
