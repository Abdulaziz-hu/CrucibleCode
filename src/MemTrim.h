#pragma once
#include <QTimer>

#if defined(__GLIBC__)
#  include <malloc.h>
#endif

// Hands freed heap memory back to the OS (glibc only; a no-op elsewhere, where the C runtime already
// returns large blocks). Coalesced: any number of calls within a second cause a single trim.
inline void trimMemoryNow()
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

inline void trimMemorySoon(int delayMs = 1000)
{
#if defined(__GLIBC__)
    static bool pending = false;
    if (pending) return;
    pending = true;
    QTimer::singleShot(delayMs, [] { pending = false; malloc_trim(0); });
#else
    Q_UNUSED(delayMs)
#endif
}
