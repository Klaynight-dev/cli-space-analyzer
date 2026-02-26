// Structure pour stocker l'arborescence
typedef struct Node {
    char *name;
    long long size;
    int is_dir;
    struct Node **children;
    int child_count;
    int child_capacity;
    pthread_mutex_t mutex;
} Node;

// Structure for a scan task
typedef struct Task {
    char *path;
    Node *parent;
    struct Task *next;
} Task;

// Structure for the task queue
typedef struct {
    Task *head;
    Task *tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int working_count;
    int stop;
} TaskQueue;