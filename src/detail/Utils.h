#ifndef IPC_DETAIL_UTILS_H
#define IPC_DETAIL_UTILS_H

#if defined(__GNUC__)
    #define SPMC_COND_EXPECT(expr,c) (__builtin_expect((expr), c))
    #define SPMC_EXPECT_TRUE(expr)   (SPMC_COND_EXPECT((expr), 1))
    #define SPMC_EXPECT_FALSE(expr)  (SPMC_COND_EXPECT((expr), 0))
#else
    #define SPMC_COND_EXPECT(expr,c) ((expr) == c)
    #define SPMC_EXPECT_TRUE(expr)   (SPMC_COND_EXPECT((expr), 1))
    #define SPMC_EXPECT_FALSE(expr)  (SPMC_COND_EXPECT((expr), 0))
#endif

#if defined SPMC_MODULUS
#define MODULUS(number, divisor) (number - (divisor * (number / divisor)))
#else
#define MODULUS(number, divisor) (number % divisor)
#endif

#endif // IPC_DETAIL_UTILS_H
