#include "./include.h"
#include "./typedef.h"
#include "./def.h"
#include "./define.h"
#include "./var.h"

// Formatage de la taille
void format_size(long long size_bytes, char *buffer) {
    const char *units[] = {"B", "Ko", "Mo", "Go", "To"};
    if (size_bytes == 0) {
        sprintf(buffer, "0 B");
        return;
    }
    int i = (int)floor(log(size_bytes) / log(1024));
    double s = size_bytes / pow(1024, i);
    sprintf(buffer, "%.2f %s", s, units[i]);
}

void* loader_thread(void* arg) {
    (void)arg; 
    const char* chars[] = {"-", "\\", "|", "/"};
    int i = 0;
    struct timespec ts = {0, 100000000}; // 100ms

    while (!stop_loader) {
        printf("\r\033[94m%s\033[0m Analyse en cours...", chars[i % 4]);
        fflush(stdout);
        i++;
        nanosleep(&ts, NULL);
    }
    printf("\r\033[K");
    fflush(stdout);
    return NULL;
}

// Fonction de comparaison pour trier par taille décroissante
int compare_nodes(const void* a, const void* b) {
    Node* nodeA = *(Node**)a;
    Node* nodeB = *(Node**)b;
    if (nodeA->size < nodeB->size) return 1;
    if (nodeA->size > nodeB->size) return -1;
    return 0;
}

// Création d'un nouveau noeud
Node* create_node(const char* name, int is_dir) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->name = strdup(name);
    node->size = 0;
    node->is_dir = is_dir;
    node->child_count = 0;
    node->child_capacity = 10;
    node->children = (Node**)malloc(node->child_capacity * sizeof(Node*));
    return node;
}

// Ajout d'un enfant à un noeud parent
void add_child(Node* parent, Node* child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = (Node**)realloc(parent->children, parent->child_capacity * sizeof(Node*));
    }
    parent->children[parent->child_count++] = child;
}

// normalize windows-style names (backslashes, drive letters) to Unix/WSL form
static void normalize_path(char *p) {
    if (!p || !*p) return;
    for (char *q = p; *q; ++q) {
        if (*q == '\\') *q = '/';
    }
    if (isalpha(p[0]) && p[1] == ':' && p[2] == '/') {
        char drive = tolower(p[0]);
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "/mnt/%c%s", drive, p+2);
        strncpy(p, tmp, 1024);
    }
}

// Parcours récursif du dossier
Node* scan_directory(const char* path, int current_depth, int max_depth) {
    Node* root = create_node(path, 1);
    DIR *dir = opendir(path);
    
    if (!dir) {
        perror(path);
        return root; // Accès refusé ou erreur
    }

    struct dirent *entry;
    char full_path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat statbuf;
        if (stat(full_path, &statbuf) != 0) continue;

        if (S_ISDIR(statbuf.st_mode)) {
            Node* child_dir = scan_directory(full_path, current_depth + 1, max_depth);
            root->size += child_dir->size;
            
            if (current_depth < max_depth) {
                // Remplacer le chemin complet par juste le nom du dossier pour l'affichage
                free(child_dir->name);
                child_dir->name = strdup(entry->d_name);
                add_child(root, child_dir);
            } else {
                // Libérer si on ne garde pas pour l'affichage
                free(child_dir->name);
                free(child_dir->children);
                free(child_dir);
            }
        } else {
            root->size += statbuf.st_size;
            if (current_depth < max_depth) {
                Node* child_file = create_node(entry->d_name, 0);
                child_file->size = statbuf.st_size;
                add_child(root, child_file);
            }
        }
    }
    closedir(dir);

    // Tri des enfants
    qsort(root->children, root->child_count, sizeof(Node*), compare_nodes);

    return root;
}

// Affichage de l'arbre
void print_tree(Node* node, const char* prefix, int is_last, int is_root) {
    char size_str[32];
    format_size(node->size, size_str);

    const char* connector = is_root ? "" : (is_last ? "└── " : "├── ");
    
    // Détermination de la couleur
    const char* color = GRAY;
    if (node->size > 1073741824LL) color = RED;        // > 1 Go
    else if (node->size > 104857600LL) color = YELLOW; // > 100 Mo
    else if (node->size > 10485760LL) color = CYAN;    // > 10 Mo

    printf("%s%s%s%s%s%s [%s%s%s]\n", 
           prefix, GRAY, connector, RESET,
           node->is_dir ? BOLD : "", node->name, 
           color, size_str, RESET);

    if (node->child_count > 0) {
        char new_prefix[1024];
        snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, is_last && !is_root ? "    " : "│   ");
        
        for (int i = 0; i < node->child_count; i++) {
            print_tree(node->children[i], new_prefix, i == node->child_count - 1, 0);
        }
    }
}

// Libération de la mémoire
void free_tree(Node* node) {
    for (int i = 0; i < node->child_count; i++) {
        free_tree(node->children[i]);
    }
    free(node->name);
    free(node->children);
    free(node);
}

int main() {
    char path[1024];
    char depth_str[10];

    printf("\033[H\033[J"); // Clear screen
    printf("%s📊 ANALYSEUR DE POIDS DE DOSSIERS%s\n\n", BOLD, RESET);

    printf("%s📂 Chemin du dossier : %s", CYAN, RESET);
    if (fgets(path, sizeof(path), stdin) == NULL) return 1;
    path[strcspn(path, "\r\n")] = 0; // Retire le saut de ligne
    normalize_path(path);

    printf("%s🔍 Profondeur (0=Aucun détail, 1=Tout les dossiers de la racine, vide=Tout) : %s", CYAN, RESET);
    if (fgets(depth_str, sizeof(depth_str), stdin) == NULL) return 1;
    depth_str[strcspn(depth_str, "\r\n")] = 0;

    int max_depth = (strlen(depth_str) == 0) ? 9999 : atoi(depth_str);

    // Lancement du loader
    pthread_t thread_id;
    stop_loader = 0;
    pthread_create(&thread_id, NULL, loader_thread, NULL);

    // Scan
    Node* tree = scan_directory(path, 0, max_depth);

    // Arrêt du loader
    stop_loader = 1;
    pthread_join(thread_id, NULL);

    // Affichage de l'arbre
    printf("\n");
    print_tree(tree, "", 1, 1);

    // Libération de la mémoire
    free_tree(tree);

    return 0;
}