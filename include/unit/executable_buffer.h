#ifndef UNIT_EXECUTABLE_BUFFER_H
#define UNIT_EXECUTABLE_BUFFER_H

#include <unit/base.h>
#include <unit/compilation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _UNIT_ExecutableBuffer UNIT_ExecutableBuffer;

UNIT_ExecutableBuffer *
UNIT_CompiledProcedure_JIT(const UNIT_CompiledProcedure *compiled);

void *
UNIT_ExecutableBuffer_GetPointer(const UNIT_ExecutableBuffer *buffer);

void
UNIT_ExecutableBuffer_Free(UNIT_ExecutableBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif
