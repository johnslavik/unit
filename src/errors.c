#include <string.h>

#include <unit/context.h>

const char *
UNIT_ErrorCode_ToString(UNIT_ErrorCode code)
{
    switch (code) {
#define NAME(name) case name: return #name

        NAME(NO_ERROR);
        NAME(NO_MEMORY);
        NAME(INVALID_USAGE);
        NAME(OS_ERROR);

#undef NAME
    }
}

UNIT_ErrorCode
UNIT_GetErrorCode(UNIT_Context *context)
{
    assert(context != NULL);
    return context->_internal.error.code;
}

void
UNIT_ClearError(UNIT_Context *context)
{
    assert(context != NULL);
    context->_internal.error.code = NO_ERROR;
    memset(context->_internal.error.message, 0, 256);
}
