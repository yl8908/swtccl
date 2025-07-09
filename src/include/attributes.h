#ifndef TCCL_ATTRIBUTES_H
#define TCCL_ATTRIBUTES_H

/**
 * __has_attribute
 */
#ifdef __has_attribute
#define TCCL_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define TCCL_HAS_ATTRIBUTE(x) 0
#endif

/**
 * Always Inline
 */
#if defined(__GNUC__)
#define TCCL_ALWAYS_INLINE inline __attribute__((__always_inline__))
#else
#define TCCL_ALWAYS_INLINE inline
#endif

/**
 * Gcc hot/cold attributes
 * Tells GCC that a function is hot or cold. GCC can use this information to
 * improve static analysis, i.e. a conditional branch to a cold function
 * is likely to be not-taken.
 */
#if TCCL_HAS_ATTRIBUTE(hot) || (defined(__GNUC__) && !defined(__clang__))
#define TCCL_ATTRIBUTE_HOT __attribute__((hot))
#else
#define TCCL_ATTRIBUTE_HOT
#endif

#if TCCL_HAS_ATTRIBUTE(cold) || (defined(__GNUC__) && !defined(__clang__))
#define TCCL_ATTRIBUTE_COLD __attribute__((cold))
#else
#define TCCL_ATTRIBUTE_COLD
#endif

/**
 * Likely
 */
#if defined(__GNUC__)
#define TCCL_LIKELY(x) (__builtin_expect((x), 1))
#define TCCL_UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define TCCL_LIKELY(x) (x)
#define TCCL_UNLIKELY(x) (x)
#endif

#define TCCL_ALWAYS_INLINE_HOT TCCL_ALWAYS_INLINE TCCL_ATTRIBUTE_HOT

#endif
