int stop_loader = 0;              // Indicateur pour arrêter le thread du chargeur
int is_analyzing = 0;             // 0 = phase de comptage, 1 = phase d'analyse
volatile long long scanned_items = 0;
volatile long long total_items = 0;
volatile long long current_total_size = 0;
char current_path[2048] = {0};
struct timespec start_time;

pthread_mutex_t progress_mutex;   // Mutex pour les compteurs globaux
int max_depth_glob;               // Profondeur maximale de scan
TaskQueue tq;                     // File d'attente des tâches pour les workers