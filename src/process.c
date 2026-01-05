#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include "process.h" 


// Vérifie si une entrée est un PID
int is_pid(const char *name) {
    for (int i = 0; name[i]; i++) { // s'arrete à /0
        if (!isdigit(name[i])) return 0; // vérifie sir le caractère est un chiffre
    }
    return 1;
}

// Lit /proc/<pid>/stat
// Extrait le PID, nom, état, temps CPU, priorité, nice
int read_stat(const char *pid_str, ProcessInfo *info) {
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%s/stat", pid_str); // Construit le chemin du fichier du processus
    f = fopen(path, "r"); // Ouvre le fichier en lecture
    if (!f) return 0;

    int pid;
    char comm[256], state;
    unsigned long utime, stime;
    long priority, nice;

    // Lecture des 3 premiers champs (PID, COMM, STATE)
    fscanf(f, "%d %s %c", &pid, comm, &state);

    // Ignorer les champs
    for (int i = 4; i <= 13; i++) fscanf(f, "%*s"); // * ignore
    
    // utime (14e champ) : temps CPU utilisateur
    // stime (15e champ) : temps CPU noyau
    fscanf(f, "%lu %lu", &utime, &stime);
    
    // Ignorer
    for (int i = 16; i <= 17; i++) fscanf(f, "%*s");
    
    // priority (18e champ)
    // nice (19e champ)
    fscanf(f, "%ld %ld", &priority, &nice);

    fclose(f);

    info->pid = pid;

    // Nettoyer la commande (souvent entourée de parenthèses)
    size_t len = strlen(comm);
    if (len > 0 && comm[0] == '(' && comm[len - 1] == ')') {
        comm[len - 1] = '\0'; // Supprimer la parenthèse fermante
        strncpy(info->name, comm + 1, sizeof(info->name) - 1); // Copier sans la parenthèse ouvrante
    } else {
        strncpy(info->name, comm, sizeof(info->name));
    }

    info->name[sizeof(info->name) - 1] = '\0'; // S'assurer que la chaîne est terminée
    
    info->state = state;
    info->priority = priority;
    info->nice = nice;
    info->time = utime + stime;

    return 1;
}

// Lit /proc/<pid>/statm 
// Extrait la memoire
int read_statm(const char *pid_str, ProcessInfo *info, unsigned long mem_total) {
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%s/statm", pid_str); // construction du chemin du fichier
    f = fopen(path, "r");
    if (!f) return 0;

    unsigned long size, resident, shared;
    // statm contient 6 champs, on ne lit que les 3 premiers
    // size : memmoire virtuelle totale
    // resident : RAM
    if (fscanf(f, "%lu %lu %lu", &size, &resident, &shared) != 3) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // /proc/statm donne des nombres de pages, pas des octets donc on convertit en octets
    long page_size = sysconf(_SC_PAGESIZE);
    info->virt = size * page_size;
    info->res  = resident * page_size;
    info->shr  = shared * page_size;
    info->mem_percent = (mem_total > 0) ? (100.0 * info->res / mem_total) : 0.0; // en %

    return 1;
}

// Lit /proc/<pid>/status pour USER
int read_user(const char *pid_str, ProcessInfo *info) {
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%s/status", pid_str);
    f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    int uid = -1;
    
    while (fgets(line, sizeof(line), f)) { // lire ligne par ligne
        if (strncmp(line, "Uid:", 4) == 0) { // compare les 4 1er caractères
            if (sscanf(line, "Uid: %d", &uid) == 1) {
                struct passwd *pw = getpwuid(uid); // conversion
                if (pw) {
                    strncpy(info->user, pw->pw_name, sizeof(info->user)); // si utilisateur trouvé alors on donne le nom
                } else {
                    snprintf(info->user, sizeof(info->user), "%u", uid); // sinon UID brut
                }
                info->user[sizeof(info->user) - 1] = '\0'; //sécurité mémoire
            }
            break;
        }
    }
    fclose(f);
    return uid != -1; // Retourne vrai si l'UID a été trouvé
}


// get_mem_total
unsigned long process_get_mem_total() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char label[64];
    unsigned long mem_total_kb = 0;
    while (fscanf(f, "%63s %lu", label, &mem_total_kb) == 2) { //lecture fichier ligne par ligne
        if (strcmp(label, "MemTotal:") == 0) break; // jusqu'à trouver la ligne correspondante
    }
    fclose(f);
    return mem_total_kb * 1024; // Convertir KiB en octets
}

// get_total_cpu_time 
unsigned long long process_get_total_cpu_time() {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    char cpu[5];
    unsigned long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
    if (fscanf(f, "%s %llu %llu %llu %llu %llu %llu %llu %llu",
           cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 9) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

// calculate_cpu_percent entre 2 mesures
double calculate_cpu_percent(unsigned long current_time,
                             unsigned long prev_time,
                             unsigned long long total_cpu,
                             unsigned long long prev_total) {
    unsigned long delta_proc  = current_time - prev_time; // nombre de ticks
    unsigned long long delta_total = total_cpu - prev_total;

    if (delta_total > 0)
        return 100.0 * delta_proc / delta_total;
    else
        return 0.0;
}

// compare_cpu
// tri par comparaison, on utilise qsort pour trier ensuite
int compare_cpu(const void *a, const void *b) {
    const ProcessInfo *info_a = (const ProcessInfo *)a;
    const ProcessInfo *info_b = (const ProcessInfo *)b;

    if (info_a->cpu_percent < info_b->cpu_percent) return 1;
    if (info_a->cpu_percent > info_b->cpu_percent) return -1;
    return 0;
}



//Fonction de comparaison pour qsort pour trier par MEM% (décroissant)
int compare_mem(const void *a, const void *b) {
    const ProcessInfo *info_a = (const ProcessInfo *)a;
    const ProcessInfo *info_b = (const ProcessInfo *)b;

    // Comparaison de deux doubles.
    // Retourne 1 si A < B (donc B vient avant A), -1 si A > B (A vient avant B).
    if (info_a->mem_percent < info_b->mem_percent) return 1;
    if (info_a->mem_percent > info_b->mem_percent) return -1;
    
    return 0; // Les pourcentages sont égaux
}

// initial_scan 
// Initialiser le point de référence pour le calcul de l'utilisation CPU.
void process_initial_scan(unsigned long prev_proc_times[]) {
    DIR *dir = opendir("/proc");
    if (!dir) { perror("opendir initial_scan"); return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (is_pid(entry->d_name)) {
            ProcessInfo info = {0};
            if (read_stat(entry->d_name, &info)) {
                if (info.pid < MAX_PID) {
                    prev_proc_times[info.pid] = info.time; // stock le nombre de tick
                }
            }
        }
    }
    closedir(dir);
}

// Nouvelle fonction de tri
void process_sort(ProcessInfo processes[], int count, SortMode mode) { 
    if (mode == SORT_CPU) { 
        qsort(processes, count, sizeof(ProcessInfo), compare_cpu);
    } else if (mode == SORT_MEM) { 
        qsort(processes, count, sizeof(ProcessInfo), compare_mem);
    }
}

// process_collect_all
// fonction moteur qui regroupe et qui actualise pour remplir le tableau de structure PorcessInfo
int process_collect_all(ProcessInfo processes[], int max_count,
                        unsigned long long prev_total_cpu,
                        unsigned long prev_proc_times[],
                        unsigned long long current_total_cpu) {

    unsigned long mem_total = process_get_mem_total(); 
    DIR *dir = opendir("/proc"); // ouvrre le /proc
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_count) { //parcours le /proc
        if (is_pid(entry->d_name)) {  // verifie qu'il y a un pid
            ProcessInfo *info = &processes[count]; //pointeur vers la structure ProcessInfo

            if (read_stat(entry->d_name, info) && //récupere les infos utiles
                read_statm(entry->d_name, info, mem_total) &&
                read_user(entry->d_name, info)) {

                int pid = info->pid;
                unsigned long prev_time = prev_proc_times[pid];

                info->cpu_percent = calculate_cpu_percent(info->time, prev_time,
                                                         current_total_cpu, prev_total_cpu);

                if (pid < MAX_PID) {
                    prev_proc_times[pid] = info->time; // met à jour temps CPU
                }

                count++;
            }
        }
    }
    closedir(dir);
    return count;
}