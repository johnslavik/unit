#ifndef UNIT_ERROR_H
#define UNIT_ERROR_H

#include <stdio.h>

#include <unit/context.h>

UNIT_ErrorCode
UNIT_Error_GetCode(const UNIT_Context *context);

void
UNIT_Error_Clear(UNIT_Context *context);

const char *
UNIT_Error_GetMessage(const UNIT_Context *context);

void
UNIT_Error_Print(const UNIT_Context *context, FILE *stream);

void
_UNIT_SetError(UNIT_Context *context, UNIT_ErrorCode code, const char *message);

void
_UNIT_SetErrorFormat(UNIT_Context *context, UNIT_ErrorCode code,
                     const char *format, ...);

void
_UNIT_SetOSError(UNIT_Context *context, const char *what);

#endif
