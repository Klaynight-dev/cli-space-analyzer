int stop_loader = 0;
int is_analyzing = 0;
volatile long long scanned_items = 0;
volatile long long total_items = 0;
char current_path[2048] = {0};
struct timespec start_time;