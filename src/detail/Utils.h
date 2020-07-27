#ifndef IPC_DETAIL_UTILS_H
#define IPC_DETAIL_UTILS_H

#if defined(__GNUC__)
    #define SPMC_COND_EXPECT(exp,c) (__builtin_expect((exp), c))
    #define SPMC_EXPECT_TRUE(exp)   (__builtin_expect((exp) == true, false))
    #define SPMC_EXPECT_FALSE(exp)  (__builtin_expect((exp) == false, true))
#else
    #define SPMC_COND_EXPECT (exp,c) ((exp) == c)
    #define SPMC_EXPECT_TRUE  (exp)  ((exp) == true,  false)
    #define SPMC_EXPECT_FALSE (exp)  ((exp) == false), true)
#endif

#endif // IPC_DETAIL_UTILS_H
