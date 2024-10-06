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

    write_constant(&chunk, 1, 123);
    write_constant(&chunk, 2, 123);
    write_chunk(&chunk, OP_MULTIPLY, 123);
    write_constant(&chunk, 3, 123);
    write_chunk(&chunk, OP_ADD, 123);

    write_constant(&chunk, 1, 124);
    write_constant(&chunk, 2, 124);
    write_constant(&chunk, 3, 124);
    write_chunk(&chunk, OP_MULTIPLY, 124);
    write_chunk(&chunk, OP_ADD, 124);

    write_constant(&chunk, 3, 125);
    write_constant(&chunk, 2, 125);
    write_chunk(&chunk, OP_SUBTRACT, 125);
    write_constant(&chunk, 1, 125);
    write_chunk(&chunk, OP_SUBTRACT, 125);

    write_constant(&chunk, 1, 126);
    write_constant(&chunk, 2, 126);
    write_constant(&chunk, 3, 126);
    write_chunk(&chunk, OP_MULTIPLY, 126);
    write_chunk(&chunk, OP_ADD, 126);
    write_constant(&chunk, 4, 126);
    write_constant(&chunk, 5, 126);
    write_chunk(&chunk, OP_NEGATE, 126);
    write_chunk(&chunk, OP_DIVIDE, 126);
    write_chunk(&chunk, OP_SUBTRACT, 126);

    write_chunk(&chunk, OP_RETURN, 127);

    printf("*** DISASSEMBLE ***\n");
    disassemble_chunk(&chunk, "test chunk");
    printf("*** RUN ***\n");
    interpret(&vm, &chunk);

    free_chunk(&chunk);
    free_vm(&vm);

    return 0;
}
