#include "chunk.h"
#include "debug.h"

int main(int argc, const char *argv[])
{
    Chunk chunk;
    init_chunk(&chunk);
    for (int i = 0; i < 260; ++i)
    {
        write_constant(&chunk, i, i + 1);
    }
    write_chunk(&chunk, OP_RETURN, 123);
    disassemble_chunk(&chunk, "test chunk");
    free_chunk(&chunk);
    return 0;
}
