// Simulatore su Windows (MSYS2/MinGW): le due funzioni POSIX che usa l'interfaccia.
#pragma once

#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
static inline int setenv(const char* name, const char* value, int overwrite) {
    (void)overwrite;
    return _putenv_s(name, value);
}
#ifdef __cplusplus
}
#endif
#endif
