// --- CONFIGURATION COULEURS ---
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define RED "\033[91m"
#define YELLOW "\033[93m"
#define CYAN "\033[96m"
#define GRAY "\033[90m"

/* enable miscellaneous/GNU extensions (e.g. usleep) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* ensure POSIX features like strdup are declared */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif