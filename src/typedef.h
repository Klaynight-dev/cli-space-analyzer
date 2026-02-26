// Structure pour stocker l'arborescence
typedef struct Node {
    char *name;
    long long size;
    int is_dir;
    struct Node **children;
    int child_count;
    int child_capacity;
} Node;