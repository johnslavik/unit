#ifndef UNIT_EXECUTABLE_BUFFER_H
#define UNIT_EXECUTABLE_BUFFER_H

#include <unit/base.h>
#include <unit/compilation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *code;
    void *rodata;
    UNIT_Size code_size;
    UNIT_Size rodata_size;
} UNIT_ExecutableBuffer;

UNIT_Status
UNIT_CompiledProcedure_JIT(const UNIT_CompiledProcedure *compiled,
                           UNIT_ExecutableBuffer *buffer);

void
UNIT_ExecutableBuffer_Clear(UNIT_ExecutableBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif
