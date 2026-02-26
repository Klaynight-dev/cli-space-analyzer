void format_size(long long size_bytes, char *buffer);
void* loader_thread(void* arg);
int compare_nodes(const void* a, const void* b);
Node* create_node(const char* name, int is_dir);
void add_child(Node* parent, Node* child);
Node* scan_directory(const char* path, int current_depth, int max_depth);
void print_tree(Node* node, const char* prefix, int is_last, int is_root);
void free_tree(Node* node);