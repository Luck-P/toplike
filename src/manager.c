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
#include "network.h"

// Options pour getopt_long (La même structure complète)
static struct option long_options[] = {
// ... (Définitions complètes des options) ...
    {"help", no_argument, 0, 'h'},
    {"dry-run", no_argument, 0, 'd'},
    {"remote-config", required_argument, 0, 'c'},
    //{"connexion-type", required_argument, 0, 't'}, nous n'utilisons que le ssh 
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
    printf("Usage: my_htop [OPTIONS]\n");
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
    //printf("  -t, --connexion-type TYPE  Spécifie le type de connexion à utiliser (ssh, telnet).\n");
    printf("  -P, --port PORT            Spécifie le port à utiliser pour la connexion (par défaut: 22 pour SSH).\n");
    
    printf("\nContrôles des processus dans l'interface graphique\n");
    printf("\t<q>       quitte le programme\n");
    
    printf("\t<m>       trier par taux d'occupation de la mémoire vive\n");
    printf("\t<p>       trier par taux d'utilisation CPU\n");
    printf("\t<r>       passe à la machine suivante (avec l'option -a)\n");
    printf("\t<c>       ligne de commande : \n");
    printf("\t\thelp, h     affiche les commandes disponibles\n");
    printf("\t\tquit, q     quitte le programme\n");
    printf("\t\tkill <pid>      termine le processus <pid>\n");
    printf("\t\tpause <pid>     met en pause le processus <pid>\n");
    printf("\t\tresume <pid>    relance le processus <pid>\n");
    printf("\t\trestart <pid>   redemarre le processus <pid> si ce dernier le permet\n");
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
//Chronomètre :
// Mémorise l'heure de départ
void stopwatch_init(time_t *lasttime) {
    *lasttime = time(NULL);
}

// Refresh si on a atteint le temps de refresh demandé
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
            //case 't': strncpy(config.cli_host.connection_type, optarg, 9); break;
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
        config.cli_host.enabled = 1;
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
    //ui_init(); ncurses 
    

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

    //initialisation intercepteur de clavier
    term_init();

    SortMode current_mode = SORT_CPU;

    //initialisation de la session distante 
    ssh_session remote_sessions[MAX_HOSTS] = {0};
    int active_rem_hosts=0;
    
    /*gestion des sessions : 
        -1  : processes locaux 
        0   : processes de la session distante n°1
        1   : processes de la session distante n°2 
        etc 
    */

    int display_source=-1;
    if (!config.collect_local && config.collect_remote) {
        display_source = 0; //remote seulement -> nous n'affichons pas les processus locaux
    }


    if(config.collect_remote){
        printf("connexion à la machine distante...\n");
        for(int i=0; i<config.host_count; i++) {
            if (network_connect(&config.hosts[i], &remote_sessions[i]) != 0) {
                 printf("Connexion échouée vers %s\n", config.hosts[i].display_name);
                 config.hosts[i].enabled = 0; // Disable this host
            }else{
                printf("Connexion réussie vers %s\n", config.hosts[i].display_name);
                
                active_rem_hosts++;
            }
        }   
    }
    if (active_rem_hosts == 0 && !config.collect_local) {
        fprintf(stderr, "\nAucune connexion active - presser une touche pour quitter \n");
        getchar();
        exit(EXIT_FAILURE);
    }
    if (active_rem_hosts < config.host_count) {
        sleep(2);   //si une connexion ssh a échoué, l'utilisateur a le temps de lire l'erreur avant que le programme ne poursuive
    }

    
    while (1) {
        //entrée dans la boucle -> passage clavier mode RAW
        term_toggle(1);
        //vérification du buffer keyhit_check() - 0 = vide / 1 = non-vide
        if(!keyhit_check()){
            if(is_first || refresh_check(last_time,2)){
                int count = 0;
                // Collecte Locale
                if (config.collect_local && display_source==-1) {
                    unsigned long long curr_total = process_get_total_cpu_time();
                    count = process_collect_all(local_procs, MAX_PROCESSES, prev_total_cpu, prev_times, curr_total);
                    process_sort(local_procs, count, current_mode);
                    prev_total_cpu = curr_total;
                    system("clear"); 
                    if(config.collect_remote) printf("[ LOCAL ]\n");
                    ui_refresh_process_list(local_procs, count, is_first);
                }
        
            // Collecte Distante 
                if (config.collect_remote && display_source>=0 && display_source<config.host_count){ //remote seule 
                    if (config.hosts[display_source].enabled) {
                        ProcessInfo remote_procs[MAX_PROCESSES];
                        int r_count = network_collect(remote_sessions[display_source], remote_procs, MAX_PROCESSES);
                        
                        if (r_count > 0) {
                            process_sort(remote_procs, r_count, current_mode);
                            
                            // Display Header for Remote
                            system("clear");
                            printf(" [ REMOTE: %s ]\n", config.hosts[display_source].display_name);
                            ui_refresh_process_list(remote_procs, r_count, is_first);
                        } else {
                           printf("Waiting for data from %s...\n", config.hosts[display_source].display_name);
                        }
                    } else {
                        printf("Host %s is disconnected.\n", config.hosts[display_source].display_name);
                    }
                    /*for(int i=0; i<config.host_count; i++) {
                        if (!config.hosts[i].enabled) continue;

                        ProcessInfo remote_procs[MAX_PROCESSES];
                        int r_count = network_collect(remote_sessions[i], remote_procs, MAX_PROCESSES);
                        process_sort(local_procs, count, current_mode);
                        if (r_count > 0) {
                           ui_refresh_process_list(remote_procs, r_count, is_first);
                        }
                    }*/
                }

            // for(int i=0; i<config.host_count; i++) { network_collect(&config.hosts[i]); }
            }else{
                //cpu protection : we prevent the loop from running at full throttle 
                nanosleep(&(struct timespec){0,10000000},NULL);
            }
            is_first = 0;
            //sleep(config.collect_local ? 2 : 5); deprecated : we now use a non-blocking stopwatch in order to still catch inputs
        }else{
            char pressed = getchar();
            
            switch(pressed){
                case 'm': {
                    current_mode = SORT_MEM;
                    *last_time = 0;
                    break;
                }
                case 'p':{
                    current_mode = SORT_CPU;
                    *last_time = 0;
                    break;
                }
                case 'r':{
                    display_source++;
                    if(display_source>=config.host_count){
                        display_source = (config.collect_local) ? -1 : 0;
                    }
                    *last_time=0; //réinitialise last_time pour déclencher le rafraîchissement
                    break;
                }
                case 'q': {
                    exit(0); //thanks to atexit(), automatic fallback to canonical mode 
                    break;
                }
                case 'c': {
                    term_toggle(0);
                    char user_command[256];
                    printf(" > ");
                    fgets(user_command,sizeof(user_command),stdin);
                    if (display_source!=-1){ //nous sommes sur une session à distance : l'emission de signal kill() est gérée dans network.c
                        ssh_session target_session = remote_sessions[display_source];
                        remote_command_handling(target_session,user_command);
                    }else{
                        command_handling(user_command);
                    }
                    
                    break;
                }

            }
            if (pressed == 'm') current_mode = SORT_MEM;
            if (pressed == 'p') current_mode = SORT_CPU;

            
            /*else{
                input_handling(pressed);
            } -> unification de la logique dans manager.c 
            
            printf("test : %c",test);
            //key handling
            getchar();*/
        }
    }

}