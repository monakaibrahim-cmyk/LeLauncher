#ifndef __VECTOR_H__
#define __VECTOR_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define VECTOR_INIT_CAPACITY 4
#define VECTOR_GROWTH_FACTOR 2

#if defined(_MSC_VER)
    #ifndef __attribute__
        #define __attribute__(x)
    #endif
#endif

/*
 * Vector Template Generator
*/

#define DEFINE_VECTOR(Type, Name)                                                       \
    typedef struct {                                                                    \
        Type* data;                                                                     \
        size_t size;                                                                    \
        size_t capacity;                                                                \
    } Name;                                                                             \
                                                                                        \
    /* Constructor: returns zero-initialized vector */                                  \
    static inline __attribute__((always_inline))                                        \
    Name Name##_create(void) {                                                          \
        return (Name){ .data = NULL, .size = 0, .capacity = 0 };                        \
    }                                                                                   \
                                                                                        \
    /* In-place initialization */                                                       \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    void Name##_init(Name* vec) {                                                       \
        vec->data = NULL;                                                               \
        vec->size = 0;                                                                  \
        vec->capacity = 0;                                                              \
    }                                                                                   \
                                                                                        \
    /* Destructor: frees allocated memory and resets fields */                          \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    void Name##_free(Name* vec) {                                                       \
        if (vec->data) {                                                                \
            free(vec->data);                                                            \
            vec->data = NULL;                                                           \
        }                                                                               \
        vec->size = 0;                                                                  \
        vec->capacity = 0;                                                              \
    }                                                                                   \
                                                                                        \
    /* std::vector::size */                                                             \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    size_t Name##_size(const Name* vec) {                                               \
        return vec->size;                                                               \
    }                                                                                   \
                                                                                        \
    /* std::vector::capacity */                                                         \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    size_t Name##_capacity(const Name* vec) {                                           \
        return vec->capacity;                                                           \
    }                                                                                   \
                                                                                        \
    /* std::vector::empty */                                                            \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    bool Name##_empty(const Name* vec) {                                                \
        return vec->size == 0;                                                          \
    }                                                                                   \
                                                                                        \
    /* std::vector::reserve */                                                          \
    static inline __attribute__((always_inline, nonnull(1), warn_unused_result))        \
    bool Name##_reserve(Name* vec, size_t new_cap) {                                    \
        if (new_cap <= vec->capacity) return true;                                      \
        Type* tmp = (Type*)realloc(vec->data, new_cap * sizeof(Type));                  \
        if (!tmp) return false;                                                         \
        vec->data = tmp;                                                                \
        vec->capacity = new_cap;                                                        \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::shrink_to_fit */                                                    \
    static inline __attribute__((always_inline, nonnull(1), warn_unused_result))        \
    bool Name##_shrink_to_fit(Name* vec) {                                              \
        if (vec->size == vec->capacity) return true;                                    \
        if (vec->size == 0) {                                                           \
            free(vec->data);                                                            \
            vec->data = NULL;                                                           \
            vec->capacity = 0;                                                          \
            return true;                                                                \
        }                                                                               \
        Type* tmp = (Type*)realloc(vec->data, vec->size * sizeof(Type));                \
        if (!tmp) return false;                                                         \
        vec->data = tmp;                                                                \
        vec->capacity = vec->size;                                                      \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::push_back */                                                        \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    bool Name##_push_back(Name* vec, Type value) {                                      \
        if (vec->size >= vec->capacity) {                                               \
            size_t new_cap = (vec->capacity == 0)                                       \
                ? VECTOR_INIT_CAPACITY                                                  \
                : vec->capacity * VECTOR_GROWTH_FACTOR;                                 \
            if (!Name##_reserve(vec, new_cap)) return false;                            \
        }                                                                               \
        vec->data[vec->size++] = value;                                                 \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::pop_back */                                                         \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    bool Name##_pop_back(Name* vec) {                                                   \
        if (vec->size == 0) return false;                                               \
        vec->size--;                                                                    \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::at (bounds-checked accessor returning pointer, or NULL if OOB) */   \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    Type* Name##_at(Name* vec, size_t index) {                                          \
        if (index >= vec->size) return NULL;                                            \
        return &vec->data[index];                                                       \
    }                                                                                   \
                                                                                        \
    /* std::vector::front */                                                            \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    Type* Name##_front(Name* vec) {                                                     \
        return vec->size > 0 ? &vec->data[0] : NULL;                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::back */                                                             \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    Type* Name##_back(Name* vec) {                                                      \
        return vec->size > 0 ? &vec->data[vec->size - 1] : NULL;                        \
    }                                                                                   \
                                                                                        \
    /* std::vector::data */                                                             \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    Type* Name##_data(Name* vec) {                                                      \
        return vec->data;                                                               \
    }                                                                                   \
                                                                                        \
    /* std::vector::clear */                                                            \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    void Name##_clear(Name* vec) {                                                      \
        vec->size = 0;                                                                  \
    }                                                                                   \
                                                                                        \
    /* std::vector::insert */                                                           \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    bool Name##_insert(Name* vec, size_t index, Type value) {                           \
        if (index > vec->size) return false;                                            \
        if (vec->size >= vec->capacity) {                                               \
            size_t new_cap = (vec->capacity == 0)                                       \
                ? VECTOR_INIT_CAPACITY                                                  \
                : vec->capacity * VECTOR_GROWTH_FACTOR;                                 \
            if (!Name##_reserve(vec, new_cap)) return false;                            \
        }                                                                               \
        if (index < vec->size) {                                                        \
            memmove(&vec->data[index + 1], &vec->data[index],                           \
                    (vec->size - index) * sizeof(Type));                                \
        }                                                                               \
        vec->data[index] = value;                                                       \
        vec->size++;                                                                    \
        return true;                                                                    \
    }                                                                                   \
                                                                                        \
    /* std::vector::erase */                                                            \
    static inline __attribute__((always_inline, nonnull(1)))                            \
    bool Name##_erase(Name* vec, size_t index) {                                        \
        if (index >= vec->size) return false;                                           \
        if (index < vec->size - 1) {                                                    \
            memmove(&vec->data[index], &vec->data[index + 1],                           \
                    (vec->size - index - 1) * sizeof(Type));                            \
        }                                                                               \
        vec->size--;                                                                    \
        return true;                                                                    \
    }

/* Backward compatibility with original macro name */
#define DECLARE_VECTOR_GLOBALS(Type, Name) DEFINE_VECTOR(Type, Name)

/* ==========================================================================
 * RAII Auto-Cleanup Macro
 * Uses GCC/Clang __attribute__((cleanup)) to automatically free vectors when
 * they go out of scope, mimicking C++ std::vector destructor behavior.
 * ========================================================================== */
#define auto_vector(Name, var) \
    __attribute__((cleanup(Name##_free))) Name var = Name##_create()

/* ==========================================================================
 * Built-in Standard Type Declarations
 * ========================================================================== */
DEFINE_VECTOR(int, IntVector)
DEFINE_VECTOR(double, DoubleVector)
DEFINE_VECTOR(char, CharVector)
DEFINE_VECTOR(float, FloatVector)
DEFINE_VECTOR(size_t, SizeVector)
DEFINE_VECTOR(void*, PointerVector)

/* ==========================================================================
 * Polymorphic _Generic Dispatch Macros
 * Provides an overloaded std::vector-like API across all standard vectors.
 * ========================================================================== */

#define vector_size(v) _Generic((v), \
    IntVector*: IntVector_size, \
    const IntVector*: IntVector_size, \
    DoubleVector*: DoubleVector_size, \
    const DoubleVector*: DoubleVector_size, \
    CharVector*: CharVector_size, \
    const CharVector*: CharVector_size, \
    FloatVector*: FloatVector_size, \
    const FloatVector*: FloatVector_size, \
    SizeVector*: SizeVector_size, \
    const SizeVector*: SizeVector_size, \
    PointerVector*: PointerVector_size, \
    const PointerVector*: PointerVector_size \
)(v)

#define vector_capacity(v) _Generic((v), \
    IntVector*: IntVector_capacity, \
    const IntVector*: IntVector_capacity, \
    DoubleVector*: DoubleVector_capacity, \
    const DoubleVector*: DoubleVector_capacity, \
    CharVector*: CharVector_capacity, \
    const CharVector*: CharVector_capacity, \
    FloatVector*: FloatVector_capacity, \
    const FloatVector*: FloatVector_capacity, \
    SizeVector*: SizeVector_capacity, \
    const SizeVector*: SizeVector_capacity, \
    PointerVector*: PointerVector_capacity, \
    const PointerVector*: PointerVector_capacity \
)(v)

#define vector_empty(v) _Generic((v), \
    IntVector*: IntVector_empty, \
    const IntVector*: IntVector_empty, \
    DoubleVector*: DoubleVector_empty, \
    const DoubleVector*: DoubleVector_empty, \
    CharVector*: CharVector_empty, \
    const CharVector*: CharVector_empty, \
    FloatVector*: FloatVector_empty, \
    const FloatVector*: FloatVector_empty, \
    SizeVector*: SizeVector_empty, \
    const SizeVector*: SizeVector_empty, \
    PointerVector*: PointerVector_empty, \
    const PointerVector*: PointerVector_empty \
)(v)

#define vector_push_back(v, val) _Generic((v), \
    IntVector*: IntVector_push_back, \
    DoubleVector*: DoubleVector_push_back, \
    CharVector*: CharVector_push_back, \
    FloatVector*: FloatVector_push_back, \
    SizeVector*: SizeVector_push_back, \
    PointerVector*: PointerVector_push_back \
)((v), (val))

#define vector_pop_back(v) _Generic((v), \
    IntVector*: IntVector_pop_back, \
    DoubleVector*: DoubleVector_pop_back, \
    CharVector*: CharVector_pop_back, \
    FloatVector*: FloatVector_pop_back, \
    SizeVector*: SizeVector_pop_back, \
    PointerVector*: PointerVector_pop_back \
)(v)

#define vector_reserve(v, n) _Generic((v), \
    IntVector*: IntVector_reserve, \
    DoubleVector*: DoubleVector_reserve, \
    CharVector*: CharVector_reserve, \
    FloatVector*: FloatVector_reserve, \
    SizeVector*: SizeVector_reserve, \
    PointerVector*: PointerVector_reserve \
)((v), (n))

#define vector_shrink_to_fit(v) _Generic((v), \
    IntVector*: IntVector_shrink_to_fit, \
    DoubleVector*: DoubleVector_shrink_to_fit, \
    CharVector*: CharVector_shrink_to_fit, \
    FloatVector*: FloatVector_shrink_to_fit, \
    SizeVector*: SizeVector_shrink_to_fit, \
    PointerVector*: PointerVector_shrink_to_fit \
)(v)

#define vector_at(v, idx) _Generic((v), \
    IntVector*: IntVector_at, \
    DoubleVector*: DoubleVector_at, \
    CharVector*: CharVector_at, \
    FloatVector*: FloatVector_at, \
    SizeVector*: SizeVector_at, \
    PointerVector*: PointerVector_at \
)((v), (idx))

#define vector_front(v) _Generic((v), \
    IntVector*: IntVector_front, \
    DoubleVector*: DoubleVector_front, \
    CharVector*: CharVector_front, \
    FloatVector*: FloatVector_front, \
    SizeVector*: SizeVector_front, \
    PointerVector*: PointerVector_front \
)(v)

#define vector_back(v) _Generic((v), \
    IntVector*: IntVector_back, \
    DoubleVector*: DoubleVector_back, \
    CharVector*: CharVector_back, \
    FloatVector*: FloatVector_back, \
    SizeVector*: SizeVector_back, \
    PointerVector*: PointerVector_back \
)(v)

#define vector_data(v) _Generic((v), \
    IntVector*: IntVector_data, \
    DoubleVector*: DoubleVector_data, \
    CharVector*: CharVector_data, \
    FloatVector*: FloatVector_data, \
    SizeVector*: SizeVector_data, \
    PointerVector*: PointerVector_data \
)(v)

#define vector_clear(v) _Generic((v), \
    IntVector*: IntVector_clear, \
    DoubleVector*: DoubleVector_clear, \
    CharVector*: CharVector_clear, \
    FloatVector*: FloatVector_clear, \
    SizeVector*: SizeVector_clear, \
    PointerVector*: PointerVector_clear \
)(v)

#define vector_free(v) _Generic((v), \
    IntVector*: IntVector_free, \
    DoubleVector*: DoubleVector_free, \
    CharVector*: CharVector_free, \
    FloatVector*: FloatVector_free, \
    SizeVector*: SizeVector_free, \
    PointerVector*: PointerVector_free \
)(v)

#define vector_insert(v, idx, val) _Generic((v), \
    IntVector*: IntVector_insert, \
    DoubleVector*: DoubleVector_insert, \
    CharVector*: CharVector_insert, \
    FloatVector*: FloatVector_insert, \
    SizeVector*: SizeVector_insert, \
    PointerVector*: PointerVector_insert \
)((v), (idx), (val))

#define vector_erase(v, idx) _Generic((v), \
    IntVector*: IntVector_erase, \
    DoubleVector*: DoubleVector_erase, \
    CharVector*: CharVector_erase, \
    FloatVector*: FloatVector_erase, \
    SizeVector*: SizeVector_erase, \
    PointerVector*: PointerVector_erase \
)((v), (idx))

#endif // __VECTOR_H__
