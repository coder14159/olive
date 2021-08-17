#ifndef IPC_DETAIL_UTILS_H
#define IPC_DETAIL_UTILS_H

#if defined(__GNUC__)
    #define SPMC_COND_EXPECT(expr,c) (__builtin_expect((expr), c))
    #define SPMC_EXPECT_TRUE(expr)   (SPMC_COND_EXPECT((expr), true))
    #define SPMC_EXPECT_FALSE(expr)  (SPMC_COND_EXPECT((expr), false))
#else
    #define SPMC_COND_EXPECT(expr,c) ((expr) == c)
    #define SPMC_EXPECT_TRUE(expr)   (SPMC_COND_EXPECT((expr), 1))
    #define SPMC_EXPECT_FALSE(expr)  (SPMC_COND_EXPECT((expr), 0))
#endif

/*
 * Macro defining a faster modulus
 */
#define MODULUS(number, divisor) (number - (divisor * (number / divisor)))

/*
 * Marginally faster if and only if the divisor is a power of 2
 * Probably not worth using as too inflexible
 */
#define MODULUS_POWER_OF_2(number, divisor) (number & (divisor - 1))

#endif // IPC_DETAIL_UTILS_H
