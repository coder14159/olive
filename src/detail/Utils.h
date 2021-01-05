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

#if defined SPMC_MODULUS

#define MODULUS(number, divisor) (number - (divisor * (number / divisor)))

#elif defined SPMC_MODULUS_DIVISOR_POWER_OF_2
 /*
  * Marginally faster if and *only if* divisor is a power of 2
  * Probably not worth using as too inflexible
  */
#define MODULUS(number, divisor) (number & (divisor - 1))

#else

#define MODULUS(number, divisor) (number % divisor)

#endif

#endif // IPC_DETAIL_UTILS_H
