#include "test_util.h"

#include <unit/internal/size_vector.h>

static void test_append_and_get(UNIT_Context *context)
{
    _UNIT_SizeVector vec;
    ASSERT_OK(context, _UNIT_SizeVector_Init(&vec, context, 4));
    ASSERT_OK(context, _UNIT_SizeVector_Append(&vec, 10));
    ASSERT_OK(context, _UNIT_SizeVector_Append(&vec, 20));
    ASSERT_OK(context, _UNIT_SizeVector_Append(&vec, 30));
    ASSERT_EQ(_UNIT_SizeVector_SIZE(&vec), 3);
    ASSERT_EQ(_UNIT_SizeVector_GET(&vec, 0), 10);
    ASSERT_EQ(_UNIT_SizeVector_GET(&vec, 1), 20);
    ASSERT_EQ(_UNIT_SizeVector_GET(&vec, 2), 30);
    _UNIT_SizeVector_Clear(&vec);
}

static void test_grow(UNIT_Context *context)
{
    _UNIT_SizeVector vec;
    ASSERT_OK(context, _UNIT_SizeVector_Init(&vec, context, 2));
    for (UNIT_Size i = 0; i < 10000; ++i) {
        ASSERT_OK(context, _UNIT_SizeVector_Append(&vec, i * 5));
    }
    ASSERT_EQ(_UNIT_SizeVector_SIZE(&vec), 10000);
    for (UNIT_Size i = 0; i < 10000; ++i) {
        ASSERT_EQ(_UNIT_SizeVector_GET(&vec, i), i * 5);
    }
    _UNIT_SizeVector_Clear(&vec);
}

static void test_empty(UNIT_Context *context)
{
    _UNIT_SizeVector vec;
    ASSERT_OK(context, _UNIT_SizeVector_Init(&vec, context, 4));
    ASSERT_EQ(_UNIT_SizeVector_SIZE(&vec), 0);
    _UNIT_SizeVector_Clear(&vec);
}

int main(void)
{
    UNIT_Context context;
    ASSERT(!UNIT_FAILED(UNIT_Context_Init(&context)));
    printf("test_size_vector:\n");
    RUN_TEST(test_append_and_get, &context);
    RUN_TEST(test_grow, &context);
    RUN_TEST(test_empty, &context);
    UNIT_Context_Clear(&context);
    return 0;
}
