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
    for (int i = 0; name[i]; i++) {
        if (!isdigit(name[i])) return 0;
    }
    return 1;
}

// Lit /proc/<pid>/stat
int read_stat(const char *pid_str, ProcessInfo *info) {
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%s/stat", pid_str);
    f = fopen(path, "r");
    if (!f) return 0;

    int pid;
    char comm[256], state;
    unsigned long utime, stime;
    long priority, nice;

    // Lecture des 14 premiers champs (PID, COMM, STATE, ...)
    fscanf(f, "%d %s %c", &pid, comm, &state);
    for (int i = 4; i <= 13; i++) fscanf(f, "%*s");
    
    // utime (14e champ) et stime (15e champ)
    fscanf(f, "%lu %lu", &utime, &stime);
    
    // Lecture des 16e et 17e champs
    for (int i = 16; i <= 17; i++) fscanf(f, "%*s");
    
    // priority (18e champ) et nice (19e champ)
    fscanf(f, "%ld %ld", &priority, &nice);
    fclose(f);

    info->pid = pid;
    // Nettoyer la commande (elle est souvent entourée de parenthèses)
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

// Lit /proc/<pid>/statm pour mémoire
int read_statm(const char *pid_str, ProcessInfo *info, unsigned long mem_total) {
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "/proc/%s/statm", pid_str);
    f = fopen(path, "r");
    if (!f) return 0;

    unsigned long size, resident, shared;
    // statm contient 6 champs, nous ne lisons que les 3 premiers
    if (fscanf(f, "%lu %lu %lu", &size, &resident, &shared) != 3) {
        fclose(f);
        return 0;
    }
    fclose(f);

    long page_size = sysconf(_SC_PAGESIZE);
    info->virt = size * page_size;
    info->res  = resident * page_size;
    info->shr  = shared * page_size;
    info->mem_percent = (mem_total > 0) ? (100.0 * info->res / mem_total) : 0.0;

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
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            // "Uid:    1000    1000    1000    1000"
            // Récupérer le premier UID (Real UID)
            if (sscanf(line, "Uid: %d", &uid) == 1) {
                struct passwd *pw = getpwuid(uid);
                if (pw) {
                    strncpy(info->user, pw->pw_name, sizeof(info->user));
                } else {
                    snprintf(info->user, sizeof(info->user), "%u", uid);
                }
                info->user[sizeof(info->user) - 1] = '\0';
            }
            break;
        }
    }
    fclose(f);
    return uid != -1; // Retourne vrai si l'UID a été trouvé
}


// 2. get_mem_total
unsigned long process_get_mem_total() {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char label[64];
    unsigned long mem_total_kb = 0;
    while (fscanf(f, "%63s %lu", label, &mem_total_kb) == 2) {
        if (strcmp(label, "MemTotal:") == 0) break;
    }
    fclose(f);
    return mem_total_kb * 1024; // Convertir KiB en octets
}

// 3. get_total_cpu_time 
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

// 4. calculate_cpu_percent (Collez ici la fonction telle quelle)
double calculate_cpu_percent(unsigned long current_time,
                             unsigned long prev_time,
                             unsigned long long total_cpu,
                             unsigned long long prev_total) {
    unsigned long delta_proc  = current_time - prev_time;
    unsigned long long delta_total = total_cpu - prev_total;

    if (delta_total > 0)
        return 100.0 * delta_proc / delta_total;
    else
        return 0.0;
}

// 5. compare_cpu
int compare_cpu(const void *a, const void *b) {
    const ProcessInfo *info_a = (const ProcessInfo *)a;
    const ProcessInfo *info_b = (const ProcessInfo *)b;

    if (info_a->cpu_percent < info_b->cpu_percent) return 1;
    if (info_a->cpu_percent > info_b->cpu_percent) return -1;
    return 0;
}

// 6. initial_scan (Renommée)
void process_initial_scan(unsigned long prev_proc_times[]) {
    DIR *dir = opendir("/proc");
    if (!dir) { perror("opendir initial_scan"); return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (is_pid(entry->d_name)) {
            ProcessInfo info = {0};
            if (read_stat(entry->d_name, &info)) {
                if (info.pid < MAX_PID) {
                    prev_proc_times[info.pid] = info.time;
                }
            }
        }
    }
    closedir(dir);
}

// 7. Nouvelle fonction de tri (utilise compare_cpu)
void process_sort_by_cpu(ProcessInfo processes[], int count) {
    qsort(processes, count, sizeof(ProcessInfo), compare_cpu);
}

// 8. list_processes devient process_collect_all
int process_collect_all(ProcessInfo processes[], int max_count,
                        unsigned long long prev_total_cpu,
                        unsigned long prev_proc_times[],
                        unsigned long long current_total_cpu) {

    unsigned long mem_total = process_get_mem_total(); // Utilisation de la fonction du module
    DIR *dir = opendir("/proc");
    if (!dir) return 0;

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (is_pid(entry->d_name)) {
            ProcessInfo *info = &processes[count];

            if (read_stat(entry->d_name, info) &&
                read_statm(entry->d_name, info, mem_total) &&
                read_user(entry->d_name, info)) {

                int pid = info->pid;
                unsigned long prev_time = prev_proc_times[pid];

                info->cpu_percent = calculate_cpu_percent(info->time, prev_time,
                                                         current_total_cpu, prev_total_cpu);

                if (pid < MAX_PID) {
                    prev_proc_times[pid] = info->time;
                }

                count++;
            }
        }
    }
    closedir(dir);
    return count;
}