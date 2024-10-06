#include <stdio.h>
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char *argv[])
{
    Vm vm;
    init_vm(&vm);

    Chunk chunk;
    init_chunk(&chunk);

    write_constant(&chunk, 1.2, 123);
    write_constant(&chunk, 3.4, 123);
    write_chunk(&chunk, OP_ADD, 123);
    write_constant(&chunk, 5.6, 123);
    write_chunk(&chunk, OP_DIVIDE, 123);
    write_chunk(&chunk, OP_NEGATE, 123);
    write_chunk(&chunk, OP_RETURN, 124);

    printf("*** DISASSEMBLE ***\n");
    disassemble_chunk(&chunk, "test chunk");
    printf("*** RUN ***\n");
    interpret(&vm, &chunk);

    free_chunk(&chunk);
    free_vm(&vm);

    return 0;
}
