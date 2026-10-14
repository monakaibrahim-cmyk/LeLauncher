#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
    #define INITIALIZER(f)                                                                          \
        static void f(void) __attribute__((constructor));                                           \
        static void f(void)

    #define DESTRUCTOR(f)                                                                           \
        static void f(void) __attribute__((destructor));                                            \
        static void f(void)
#elif defined(_MSC_VER)
    #pragma section(".CRT$XCU", read)
    
    #define INITIALIZER(f)                                                                          \
        static void f(void)                                                                         \
        __declspec(allocate(".CRT$XCU")) static void (*f##_ptr)(void) = f;                          \
        static void f(void)

    #define DEDESTRUCTOR(f)                                                                         \
        static void f(void)                                                                         \
        static void f##_reg(void) {                                                                 \
            atexit(f);                                                                              \
        }                                                                                           \
        __declspec(allocate(".CRT$XCU")) static void (*f##_reg_ptr)(void) = f##_reg;                \
        static void f(void)
#else
    #error "Compiler not supported for pre-main initializers."
#endif

#endif // __GLOBALS_H__
