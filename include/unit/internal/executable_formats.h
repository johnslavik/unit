#ifndef UNIT_EXECUTABLE_FORMATS_H
#define UNIT_EXECUTABLE_FORMATS_H

#include <unit/internal/compile_context.h>

UNIT_Status
_UNIT_ELF_WriteObjectFile(const _UNIT_CompileContext *context, const char *path);

#endif
