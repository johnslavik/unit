#include <string.h>

#include <unit/context.h>

const char *
UNIT_ErrorCode_ToString(UNIT_ErrorCode code)
{
    switch (code) {
        case UNIT_ERROR_NONE:
            return "NONE";
        case UNIT_ERROR_INVALID_USAGE:
            return "INVALID_USAGE";
        case UNIT_ERROR_NO_MEMORY:
            return "NO_MEMORY";
        case UNIT_ERROR_OS_FAILURE:
            return "OS_FAILURE";
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
    context->_internal.error.code = UNIT_ERROR_NONE;
    memset(context->_internal.error.message, 0, 256);
}
