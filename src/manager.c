// src/manager.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include "manager.h"
#include "process.h"
#include "ui.h"


// Options pour getopt_long (La même structure complète)
static struct option long_options[] = {
// ... (Définitions complètes des options) ...
    {"help", no_argument, 0, 'h'},
    {"dry-run", no_argument, 0, 'd'},
    {"remote-config", required_argument, 0, 'c'},
    {"connexion-type", required_argument, 0, 't'},
    {"port", required_argument, 0, 'P'},
    {"login", required_argument, 0, 'l'},
    {"remote-server", required_argument, 0, 's'},
    {"username", required_argument, 0, 'u'},
    {"password", required_argument, 0, 'p'},
    {"all", no_argument, 0, 'a'},
    {0, 0, 0, 0}
};

const char *optstring = "hdc:t:P:l:s:u:p:a";



void manager_print_help(void) {
    printf("Usage: my_top [OPTIONS]\n");
    printf("\nOptions de base:\n");
    printf("  -h, --help                 Affiche l'aide et quitte.\n");
    printf("  --dry-run                  Test de connexion (local et distant) sans lancer l'interface.\n");
    
    printf("\nOptions de configuration des hôtes:\n");
    printf("  -c, --remote-config FILE   Fichier de configuration contenant la liste des machines distantes (droits 600 requis).\n");
    printf("  -s, --remote-server HOST   Adresse IP ou nom DNS de la machine distante à surveiller.\n");
    printf("  -l, --login USER@HOST      Spécifie l'identifiant et la machine distante (Ex: user@server).\n");
    printf("  -a, --all                  Active la collecte des processus sur la machine locale ET les machines distantes (s'utilise avec -c, -s ou -l).\n");
    
    printf("\nOptions de connexion détaillées:\n");
    printf("  -u, --username USER        Spécifie le nom d'utilisateur pour la connexion (si non fourni par -l).\n");
    printf("  -p, --password PASS        Spécifie le mot de passe pour la connexion (si non demandé interactivement).\n");
    printf("  -t, --connexion-type TYPE  Spécifie le type de connexion à utiliser (ssh, telnet).\n");
    printf("  -P, --port PORT            Spécifie le port à utiliser pour la connexion (par défaut: 22 pour SSH).\n");
    printf("\n");
}

// Demande interactive 
void manager_ask_input(const char *prompt, char *buffer, size_t size) {
    fprintf(stdout, "%s", prompt); 
    if (fgets(buffer, size, stdin)) {
        buffer[strcspn(buffer, "\n")] = 0; 
    }
}

// Vérifie les droits 0600 
int check_file_permissions(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT && strcmp(path, ".config") == 0) return 0;
        perror("Erreur accès fichier config");
        return -1;
    }
    if ((st.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        fprintf(stderr, "ALERTE SECURITE: Le fichier '%s' doit avoir les droits 0600 (rw-------).\n", path);
        return -1;
    }
    return 1;
}

// Parse le fichier de configuration
void parse_config_file(const char *path, ManagerConfig *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && cfg->host_count < MAX_HOSTS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        RemoteHost *h = &cfg->hosts[cfg->host_count];
        int ret = sscanf(line, "%63[^:]:%255[^:]:%d:%63[^:]:%63[^:]:%9s",
               h->display_name, h->address, &h->port, h->username, h->password, h->connection_type);
        if (ret == 6) {
            h->enabled = 1;
            cfg->host_count++;
        }
    }
    fclose(f);
}

//fonction gestion du rafraichissement
void stopwatch_init(time_t *lasttime) {
    *lasttime = time(NULL);
}

int refresh_check(time_t *lasttime, int sec_interval){
    time_t cur_time = time(NULL);
    if (difftime(cur_time, *lasttime) >= sec_interval){
        *lasttime = cur_time; //replacing latest refresh date
        return 1; //we refresh
    }else{
        return 0; //too soon
    }
}


// --- Fonction Principale ---
void manager_run(int argc, char *argv[]) {
    ManagerConfig config = {0};
    
    // Valeurs par défaut pour l'hôte CLI temporaire
    config.cli_host.port = 22;
    strcpy(config.cli_host.connection_type, "ssh");
    config.cli_config_file[0] = '\0';

    int opt, idx;
    int option_all = 0;

    // 1. Parsing des arguments CLI 
    while ((opt = getopt_long(argc, argv, optstring, long_options, &idx)) != -1) {
        switch (opt) {
            case 'h': config.show_help = 1; break;
            case 'd': config.dry_run = 1; break;
            case 'a': option_all = 1; break;
            case 'c': strncpy(config.cli_config_file, optarg, MAX_PATH_LEN - 1); break;
            case 't': strncpy(config.cli_host.connection_type, optarg, 9); break;
            case 'P': config.cli_host.port = atoi(optarg); break;
            case 'u': strncpy(config.cli_host.username, optarg, MAX_USER_LEN - 1); break;
            case 'p': strncpy(config.cli_host.password, optarg, MAX_PASS_LEN - 1); break;
            case 's':
                strncpy(config.cli_host.address, optarg, MAX_HOST_LEN - 1);
                if (config.cli_host.display_name[0] == '\0') 
                    strcpy(config.cli_host.display_name, optarg);
                config.cli_host_defined = 1;
                break;
            case 'l': { 
                char *at = strchr(optarg, '@');
                if (at) {
                    *at = '\0'; 
                    strncpy(config.cli_host.username, optarg, MAX_USER_LEN - 1);
                    strncpy(config.cli_host.address, at + 1, MAX_HOST_LEN - 1);
                    if (config.cli_host.display_name[0] == '\0')
                        strcpy(config.cli_host.display_name, at + 1);
                } else {
                     fprintf(stderr, "Format invalide pour -l. Attendu: user@host\n");
                     exit(EXIT_FAILURE);
                }
                config.cli_host_defined = 1;
                break;
            }
        }
    }

    if (config.show_help) {
        manager_print_help();
        return;
    }

    // --- LOGIQUE DE PRÉ-EXÉCUTION ---

    // 2. Gestion du fichier de configuration (-c ou .config)
    char *config_path = NULL;
    
    if (config.cli_config_file[0] != '\0') {
        config_path = config.cli_config_file;
    } else if (!config.cli_host_defined) {
        config_path = ".config";
    }

    if (config_path) {
        int perm_status = check_file_permissions(config_path);
        if (perm_status == 1) {
            parse_config_file(config_path, &config);
        } else if (perm_status == -1 && config.cli_config_file[0] != '\0') {
            exit(EXIT_FAILURE);
        }
    }

    // 3. Gestion de l'hôte CLI interactif (-s ou -l)
    if (config.cli_host_defined) {
        // Demandes interactives
        if (config.cli_host.username[0] == '\0') {
            manager_ask_input("Nom d'utilisateur: ", config.cli_host.username, MAX_USER_LEN);
        }
        if (config.cli_host.password[0] == '\0') {
            manager_ask_input("Mot de passe: ", config.cli_host.password, MAX_PASS_LEN);
        }

        // Ajout à la liste des hôtes
        if (config.host_count < MAX_HOSTS) {
            config.hosts[config.host_count++] = config.cli_host;
        }
    }

    // 4. Détermination des modes de collecte (Local vs Distant)
    if (config.host_count > 0) {
        config.collect_remote = 1;
        if (option_all) {
            config.collect_local = 1;
        } else {
            config.collect_local = 0; // Remote seul
        }
    } else {
        config.collect_local = 1; // Local par défaut
        config.collect_remote = 0;
    }

    // --- EXÉCUTION (Section 5) ---

    if (config.dry_run) {
        printf("[DRY-RUN] Démarrage...\n");
        if (config.collect_local) printf("[DRY-RUN] Accès Local: OK\n");
        
        for(int i=0; i<config.host_count; i++) {
            printf("[DRY-RUN] Test connexion vers %s (%s@%s:%d)... SIMULATION OK\n", 
                   config.hosts[i].display_name, 
                   config.hosts[i].username, 
                   config.hosts[i].address, 
                   config.hosts[i].port);
        }
        return; // Arrêt
    }

    // --- Boucle Principale ---
    
    // Initialisation UI
    ui_init();
    
    // Initialisation des états (variables statiques et tableaux)
    ProcessInfo local_procs[MAX_PROCESSES];
    static unsigned long prev_times[MAX_PID] = {0};
    unsigned long long prev_total_cpu = 0;
    int is_first = 1;

    if (config.collect_local) {
        process_initial_scan(prev_times);
        prev_total_cpu = process_get_total_cpu_time();
    }

    //initialisation de la stopwatch
    time_t *last_time = malloc(sizeof(time_t));
    stopwatch_init(last_time);

    while (1) {
        int count = 0;
        if(is_first || refresh_check(last_time,config.collect_local ? 2 : 5)){
        // Collecte Locale
            if (config.collect_local) {
                 unsigned long long curr_total = process_get_total_cpu_time();
                count = process_collect_all(local_procs, MAX_PROCESSES, prev_total_cpu, prev_times, curr_total);
                process_sort_by_cpu(local_procs, count);
                prev_total_cpu = curr_total;
             
                ui_refresh_process_list(local_procs, count, is_first);
            }
        
        // Collecte Distante (Placeholder)
        // for(int i=0; i<config.host_count; i++) { network_collect(&config.hosts[i]); }
        }else{
            //cpu protection : we prevent the loop from running at full throttle 
            nanosleep(&(struct timespec){0,10000000},NULL);
        }
        is_first = 0;
        //sleep(config.collect_local ? 2 : 5); deprecated : we now use a non-blocking stopwatch in order to still catch inputs
    }

}