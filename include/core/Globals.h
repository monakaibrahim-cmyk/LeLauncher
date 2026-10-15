#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
    #define INITIALIZER(f)                                                                          \
        static void f(void) __attribute__((constructor));                                           \
        static void f(void)

    #define CONSTRUCTOR(f)                                                                          \
        INITIALIZER(f)

    #define DESTRUCTOR(f)                                                                           \
        static void f(void) __attribute__((destructor));                                            \
        static void f(void)

    #define __FUNCTION_NAME__                                                                       __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
    #pragma section(".CRT$XCU", read)
    
    #define INITIALIZER(f)                                                                          \
        static void f(void);                                                                        \
        __declspec(allocate(".CRT$XCU")) static void (*f##_ptr)(void) = f;                          \
        static void f(void)

    #define CONSTRUCTOR(f)                                                                          \
        INITIALIZER(f)

    #define DESTRUCTOR(f)                                                                           \
        static void f(void);                                                                        \
        static void f##_reg(void) {                                                                 \
            atexit(f);                                                                              \
        }                                                                                           \
        __declspec(allocate(".CRT$XCU")) static void (*f##_reg_ptr)(void) = f##_reg;                \
        static void f(void)

    #define __FUNCTION_NAME__                                                                       __FUNCSIG__
#else
    #error "Compiler not supported for pre-main initializers."
#endif

/* ==============================================================================
 * Compiler Identification & Build Metadata
 * ============================================================================== */
#define __STR_HELPER(x)                                                                             #x
#define __STR(x)                                                                                    __STR_HELPER(x)

#if defined(__clang__)
    #define COMPILER_NAME                                                                           "Clang"
    #define COMPILER_VERSION                                                                        __clang_version__
    #define COMPILER_INFO                                                                           "Clang " __clang_version__
#elif defined(__GNUC__) || defined(__GNUC_MINOR__)
    #define COMPILER_NAME                                                                           "GCC"
    #define COMPILER_VERSION                                                                        __VERSION__
    #define COMPILER_INFO                                                                           "GCC " __VERSION__
#elif defined(_MSC_VER)
    #define COMPILER_NAME                                                                           "MSVC"
    #define COMPILER_VERSION                                                                        __STR(_MSC_VER)
    #define COMPILER_INFO                                                                           "MSVC " __STR(_MSC_VER)
#else
    #define COMPILER_NAME                                                                           "Unknown"
    #define COMPILER_VERSION                                                                        "Unknown"
    #define COMPILER_INFO                                                                           "Unknown Compiler"
#endif

#define BUILD_DATE                                                                                  __DATE__
#define BUILD_TIME                                                                                  __TIME__
#define BUILD_TIMESTAMP                                                                             __DATE__ " " __TIME__

#if defined(DEBUG) || defined(_DEBUG) || !defined(NDEBUG)
    #define BUILD_CONFIG                                                                            "Debug"
#else
    #define BUILD_CONFIG                                                                            "Release"
#endif

#endif // __GLOBALS_H__
