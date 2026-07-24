#include "unity.h"
#include "pca2131.h"

void setUp(void) {}
void tearDown(void) {}

// We need a wrapper to access the internal validation functions since they are static
// BUT we can't easily test static functions without #include "pca2131.c" or removing static.
// Wait, the roadmap says we test PCA2131_IsValidDateTime which we don't have. 
// Ah, `PCA2131_Is_Valid_DateTime` is exposed in rtc_app.c, or we can just test the PCA2131_Driver_IsValidDateTime from pca2131.h.
// Let's look at pca2131.h. It exposes PCA2131_Driver_IsValidDateTime.
// Let's test it!

void test_IsValidDateTime_rejects_hour_24(void)
{
    PCA2131_DateTime_t dt = { .hour=24, .minute=0, .second=0, .day=1, .month=1, .year=0, .weekday=0 };
    TEST_ASSERT_EQUAL_UINT8(0, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_rejects_day_0(void)
{
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=0, .month=1, .year=0, .weekday=0 };
    TEST_ASSERT_EQUAL_UINT8(0, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_rejects_month_13(void)
{
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=1, .month=13, .year=0, .weekday=0 };
    TEST_ASSERT_EQUAL_UINT8(0, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_feb_28_non_leap(void)
{
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=28, .month=2, .year=23, .weekday=0 }; // 2023 is not leap
    TEST_ASSERT_EQUAL_UINT8(1, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_feb_29_leap_year(void)
{
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=29, .month=2, .year=24, .weekday=0 }; // 2024 is leap
    TEST_ASSERT_EQUAL_UINT8(1, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_feb_29_non_leap_rejected(void)
{
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=29, .month=2, .year=23, .weekday=0 };
    TEST_ASSERT_EQUAL_UINT8(0, PCA2131_Driver_IsValidDateTime(&dt));
}

void test_IsValidDateTime_year_0_is_leap(void)
{
    // The bug mentioned: year 0 (2000) should be a leap year.
    // Let's see if our logic catches the failure if it's broken.
    PCA2131_DateTime_t dt = { .hour=12, .minute=0, .second=0, .day=29, .month=2, .year=0, .weekday=0 };
    TEST_ASSERT_EQUAL_UINT8(1, PCA2131_Driver_IsValidDateTime(&dt));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_IsValidDateTime_rejects_hour_24);
    RUN_TEST(test_IsValidDateTime_rejects_day_0);
    RUN_TEST(test_IsValidDateTime_rejects_month_13);
    RUN_TEST(test_IsValidDateTime_feb_28_non_leap);
    RUN_TEST(test_IsValidDateTime_feb_29_leap_year);
    RUN_TEST(test_IsValidDateTime_feb_29_non_leap_rejected);
    RUN_TEST(test_IsValidDateTime_year_0_is_leap);
    return UNITY_END();
}
