// --- CONFIGURATION COULEURS ---
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define RED "\033[91m"
#define YELLOW "\033[93m"
#define CYAN "\033[96m"
#define GRAY "\033[90m"

/* activer les extensions diverses/GNU (ex: usleep, DT_DIR) */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* s'assurer que les fonctionnalités POSIX comme strdup sont déclarées */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif