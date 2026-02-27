#define _DEFAULT_SOURCE
#include "./include.h"


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
        // calcul du temps écoulé
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start_time.tv_sec) +
                         (now.tv_nsec - start_time.tv_nsec) / 1e9;
        double est_remain = 0;
        
        if (is_analyzing && total_items > 0 && scanned_items > 0 && total_items > scanned_items) {
            // temps restant estimé basé sur le taux actuel
            est_remain = elapsed * ((double)(total_items - scanned_items) / scanned_items);
        }

        const char* phase = is_analyzing ? "Analyse" : "Comptage";

        char display_path[80] = "";
        if (current_path[0]) {
            char clean[2048];
            int ci = 0;
            for (int j = 0; current_path[j] && ci < (int)sizeof(clean)-1; ++j) {
                unsigned char c = current_path[j];
                if (c >= 32 && c != 127) {
                    clean[ci++] = c;
                }
            }
            clean[ci] = '\0';
            const char *p = clean;
            int len = strlen(p);
            if (len > 60) {
                snprintf(display_path, sizeof(display_path), "...%s", p + len - 57);
            } else {
                strncpy(display_path, p, sizeof(display_path) - 1);
                display_path[sizeof(display_path)-1] = '\0';
            }
        }

        char size_buf[32] = "";
        format_size(current_total_size, size_buf);

        printf("\r\033[K\033[94m%s\033[0m %s", chars[i % 4], phase);
        if (display_path[0]) {
            printf(" %s", display_path);
        }
        
        if (is_analyzing) {
            if (total_items > 0) {
                printf(" [%lld/%lld]", scanned_items, total_items);
            }
        } else {
            printf(" [%lld]", total_items);
        }

        printf(" (%s)", size_buf);
        printf(" %ds", (int)elapsed);
        if (is_analyzing && est_remain > 0) {
            printf(" restants %ds", (int)est_remain);
        }
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
    pthread_mutex_init(&node->mutex, NULL);
    return node;
}

void add_child(Node* parent, Node* child) {
    pthread_mutex_lock(&parent->mutex);
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = (Node**)realloc(parent->children, parent->child_capacity * sizeof(Node*));
    }
    parent->children[parent->child_count++] = child;
    pthread_mutex_unlock(&parent->mutex);
}

void tq_push(const char *path, Node *parent) {
    Task *task = (Task*)malloc(sizeof(Task));
    task->path = strdup(path);
    task->parent = parent;
    task->next = NULL;

    pthread_mutex_lock(&tq.mutex);
    if (tq.tail) {
        tq.tail->next = task;
        tq.tail = task;
    } else {
        tq.head = tq.tail = task;
    }
    tq.count++;
    pthread_cond_signal(&tq.cond);
    pthread_mutex_unlock(&tq.mutex);
}

Task* tq_pop() {
    pthread_mutex_lock(&tq.mutex);
    while (tq.head == NULL && !tq.stop) {
        pthread_cond_wait(&tq.cond, &tq.mutex);
    }
    if (tq.stop && tq.head == NULL) {
        pthread_mutex_unlock(&tq.mutex);
        return NULL;
    }
    Task *task = tq.head;
    tq.head = task->next;
    if (tq.head == NULL) tq.tail = NULL;
    tq.count--;
    tq.working_count++;
    pthread_mutex_unlock(&tq.mutex);
    return task;
}

void tq_finish_task() {
    pthread_mutex_lock(&tq.mutex);
    tq.working_count--;
    if (tq.working_count == 0 && tq.head == NULL) {
        pthread_cond_broadcast(&tq.cond);
    }
    pthread_mutex_unlock(&tq.mutex);
}

void* worker_thread(void* arg) {
    (void)arg;
    while (1) {
        Task *task = tq_pop();
        if (!task) break;

        if (is_analyzing) {
            scan_directory_recursive(task->parent, task->path, 1, max_depth_glob);
        } else {
            DIR *dir = opendir(task->path);
            if (dir) {
                struct dirent *entry;
                char full_path[2048];
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                    
                    pthread_mutex_lock(&progress_mutex);
                    total_items++;
                    if (total_items % 100 == 0) {
                        strncpy(current_path, task->path, sizeof(current_path)-1);
                        current_path[sizeof(current_path)-1] = '\0';
                    }
                    pthread_mutex_unlock(&progress_mutex);

                    snprintf(full_path, sizeof(full_path), "%s/%s", task->path, entry->d_name);
                    struct stat statbuf;
                    int has_stat = (stat(full_path, &statbuf) == 0);

                    int is_dir = 0;
#ifdef _DIRENT_HAVE_D_TYPE
                    if (entry->d_type != DT_UNKNOWN && entry->d_type != DT_LNK) {
                        is_dir = (entry->d_type == DT_DIR);
                    } else {
#endif
                        if (has_stat) {
                            is_dir = S_ISDIR(statbuf.st_mode);
                        }
#ifdef _DIRENT_HAVE_D_TYPE
                    }
#endif
                    if (is_dir) {
                        tq_push(full_path, NULL);
                    } else {
                        if (has_stat) {
                            pthread_mutex_lock(&progress_mutex);
                            current_total_size += statbuf.st_size;
                            pthread_mutex_unlock(&progress_mutex);
                        }
                    }
                }
                closedir(dir);
            }
        }

        free(task->path);
        free(task);
        tq_finish_task();
    }
    return NULL;
}

// normalisation des noms de style Windows (backslashes, lettres de lecteur) vers le format Unix/WSL
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

// Pré-scan pour compter les éléments rapidement

void wait_tq_empty() {
    pthread_mutex_lock(&tq.mutex);
    while (tq.head != NULL || tq.working_count > 0) {
        pthread_cond_wait(&tq.cond, &tq.mutex);
    }
    pthread_mutex_unlock(&tq.mutex);
}

void count_items_mt(const char* path) {
    tq_push(path, NULL);
    wait_tq_empty();
}

void scan_directory_recursive(Node* root, const char* path, int current_depth, int max_depth) {
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    char full_path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) != 0) {
            pthread_mutex_lock(&progress_mutex);
            scanned_items++;
            pthread_mutex_unlock(&progress_mutex);
            continue;
        }

        pthread_mutex_lock(&progress_mutex);
        strncpy(current_path, full_path, sizeof(current_path)-1);
        current_path[sizeof(current_path)-1] = '\0';
        scanned_items++;
        pthread_mutex_unlock(&progress_mutex);

        if (S_ISDIR(statbuf.st_mode)) {
            Node* child_dir = create_node(entry->d_name, 1);
            scan_directory_recursive(child_dir, full_path, current_depth + 1, max_depth);
            
            pthread_mutex_lock(&root->mutex);
            root->size += child_dir->size;
            if (current_depth < max_depth) {
                add_child(root, child_dir);
            } else {
                free_tree(child_dir);
            }
            pthread_mutex_unlock(&root->mutex);
        } else {
            pthread_mutex_lock(&progress_mutex);
            current_total_size += statbuf.st_size;
            pthread_mutex_unlock(&progress_mutex);
            pthread_mutex_lock(&root->mutex);
            root->size += statbuf.st_size;
            if (current_depth < max_depth) {
                Node* child_file = create_node(entry->d_name, 0);
                child_file->size = statbuf.st_size;
                add_child(root, child_file);
            }
            pthread_mutex_unlock(&root->mutex);
        }
    }
    closedir(dir);
}

// Scan parallèle pour le niveau supérieur
Node* scan_directory_mt(const char* path, int max_depth) {
    Node* root = create_node(path, 1);
    DIR *dir = opendir(path);
    if (!dir) return root;

    struct dirent *entry;
    char full_path[2048];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) != 0) {
            pthread_mutex_lock(&progress_mutex);
            scanned_items++;
            pthread_mutex_unlock(&progress_mutex);
            continue;
        }

        pthread_mutex_lock(&progress_mutex);
        strncpy(current_path, full_path, sizeof(current_path)-1);
        current_path[sizeof(current_path)-1] = '\0';
        scanned_items++;
        pthread_mutex_unlock(&progress_mutex);

        if (S_ISDIR(statbuf.st_mode)) {
            Node* child_dir = create_node(entry->d_name, 1);
            tq_push(full_path, child_dir);
            add_child(root, child_dir);
        } else {
            pthread_mutex_lock(&progress_mutex);
            current_total_size += statbuf.st_size;
            pthread_mutex_unlock(&progress_mutex);
            root->size += statbuf.st_size;
            if (max_depth > 0) {
                Node* child_file = create_node(entry->d_name, 0);
                child_file->size = statbuf.st_size;
                add_child(root, child_file);
            }
        }
    }
    closedir(dir);

    wait_tq_empty();

    for (int i = 0; i < root->child_count; i++) {
        root->size += root->children[i]->size;
    }

    qsort(root->children, root->child_count, sizeof(Node*), compare_nodes);
    return root;
}

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

// Vérifie si un dossier est vide
int is_dir_empty(const char *path) {
    int count = 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
            break;
        }
    }
    closedir(dir);
    return count == 0;
}

// Supprime récursivement les dossiers vides
void delete_empty_dirs(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    char full_path[2048];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat statbuf;
        if (stat(full_path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
            delete_empty_dirs(full_path);
        }
    }
    closedir(dir);

    if (is_dir_empty(path)) {
        if (rmdir(path) == 0) {
            printf("%s🗑️  Dossier vide supprimé : %s%s\n", GRAY, path, RESET);
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
    max_depth_glob = max_depth;

    pthread_mutex_init(&progress_mutex, NULL);
    pthread_mutex_init(&tq.mutex, NULL);
    pthread_cond_init(&tq.cond, NULL);
    tq.head = tq.tail = NULL;
    tq.count = 0;
    tq.working_count = 0;
    tq.stop = 0;

    // préparation des compteurs/progrès
    scanned_items = 0;
    total_items = 0;
    current_total_size = 0;

    // Lancement des workers
    pthread_t workers[8];
    for (int i = 0; i < 8; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    // Lancement du loader
    pthread_t loader_tid;
    stop_loader = 0;
    is_analyzing = 0;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    pthread_create(&loader_tid, NULL, loader_thread, NULL);

    // Pre-scan phase
    count_items_mt(path);
    
    // Switch to analysis phase
    is_analyzing = 1;

    // Scan (total_items is now stable)
    Node* tree = scan_directory_mt(path, max_depth);

    // Arrêt du loader et des workers
    stop_loader = 1;
    pthread_join(loader_tid, NULL);

    pthread_mutex_lock(&tq.mutex);
    tq.stop = 1;
    pthread_cond_broadcast(&tq.cond);
    pthread_mutex_unlock(&tq.mutex);
    for (int i = 0; i < 8; i++) {
        pthread_join(workers[i], NULL);
    }

    // Affichage de l'arbre
    printf("\n");
    print_tree(tree, "", 1, 1);

    // Calcul du temps total
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_elapsed = (end_time.tv_sec - start_time.tv_sec) +
                           (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    
    printf("\n%s⏱️  Temps total d'exécution : %.2fs%s\n", BOLD, total_elapsed, RESET);

    // Proposition de suppression des dossiers vides
    printf("\n%s🧹 Voulez-vous supprimer les dossiers vides dans ce chemin ? (y/n) : %s", CYAN, RESET);
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) != NULL && (choice[0] == 'y' || choice[0] == 'Y')) {
        printf("%s🔍 Recherche et suppression des dossiers vides...%s\n", GRAY, RESET);
        delete_empty_dirs(path);
        printf("%s✅ Suppression terminée.%s\n", BOLD, RESET);
    }

    // Libération de la mémoire
    free_tree(tree);

    return 0;
}