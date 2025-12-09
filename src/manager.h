// src/manager.h
#ifndef MANAGER_H
#define MANAGER_H

#include <stddef.h>

// --- Constantes Générales ---
#define MAX_HOSTS 10
#define MAX_NAME_LEN 64
#define MAX_HOST_LEN 256
#define MAX_USER_LEN 64
#define MAX_PASS_LEN 64
#define MAX_PATH_LEN 256


// Structure représentant une machine distante unique
typedef struct {
    char display_name[MAX_NAME_LEN]; // Pour l'onglet/affichage
    char address[MAX_HOST_LEN];
    int port;
    char username[MAX_USER_LEN];
    char password[MAX_PASS_LEN];
    char connection_type[10]; // "ssh" ou "telnet"
    int enabled; // 1 si actif
} RemoteHost;

// Structure de configuration globale
typedef struct {
    int show_help;
    int dry_run;
    int collect_local;  // 1 si on doit scanner la machine locale
    int collect_remote; // 1 si on a des hôtes distants à scanner
    
    // Liste des hôtes distants (via -c, -s ou -l)
    RemoteHost hosts[MAX_HOSTS];
    int host_count;

    // Variables temporaires pour le parsing CLI avant consolidation
    char cli_config_file[MAX_PATH_LEN];
    RemoteHost cli_host; // Pour stocker temporairement les infos de -s, -l, -u, -p
    int cli_host_defined; // Flag pour savoir si -s ou -l a été utilisé
} ManagerConfig;



// --- Prototypes des fonctions ---

void manager_run(int argc, char *argv[]);
void manager_print_help(void);
void manager_ask_input(const char *prompt, char *buffer, size_t size);
int check_file_permissions(const char *path);
void parse_config_file(const char *path, ManagerConfig *cfg);

void stopwatch_init(time_t *lasttime);
int refresh_check(time_t *lasttime, int sec_interval);

#endif