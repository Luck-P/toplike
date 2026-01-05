#ifndef UI_H
#define UI_H

#include "process.h" // Nécessaire pour ProcessInfo

// Fonctions d'interface
void ui_init(void);
void ui_cleanup(void);
void ui_refresh_process_list(ProcessInfo processes[], int count, int is_initial_run);

//fonctions de paramètres clavier 
void term_init(void);
void term_toggle(int mode);
int keyhit_check(void);

//gestion inputs 
int command_handling(char*);


#endif