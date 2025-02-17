
#ifndef M_EXPORT_H
#define M_EXPORT_H

#ifdef M_STATIC_DEFINE
#  define M_EXPORT
#  define M_NO_EXPORT
#else
#  ifndef M_EXPORT
#    ifdef mutable_EXPORTS
        /* We are building this library */
#      define M_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define M_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef M_NO_EXPORT
#    define M_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef M_DEPRECATED
#  define M_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef M_DEPRECATED_EXPORT
#  define M_DEPRECATED_EXPORT M_EXPORT M_DEPRECATED
#endif

#ifndef M_DEPRECATED_NO_EXPORT
#  define M_DEPRECATED_NO_EXPORT M_NO_EXPORT M_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef M_NO_DEPRECATED
#    define M_NO_DEPRECATED
#  endif
#endif

#endif /* M_EXPORT_H */
