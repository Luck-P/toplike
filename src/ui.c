#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include "process.h"
#include "ui.h" 

int command_handling(char*);
void trim_newline(char*);

// 1. format_size 
void format_size(unsigned long bytes, char *buf, size_t buf_size) {
    if (bytes >= 1024UL * 1024UL * 1024UL) {
        snprintf(buf, buf_size, "%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024UL * 1024UL) {
        snprintf(buf, buf_size, "%.1fM", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024UL) {
        snprintf(buf, buf_size, "%.1fK", bytes / 1024.0);
    } else {
        snprintf(buf, buf_size, "%luB", bytes);
    }
}

// 2. print_header
void print_header() {
    printf("%-6s %-17s %-4s %-4s %-10s %-10s %-10s %-3s %-6s %-6s %-10s %-20s\n",
           "PID", "USER", "PRI", "NI", "VIRT", "RES", "SHR", "S", "MEM%", "CPU%", "TIME", "CMD");
}

// 3. print_process
void print_process(const ProcessInfo *info, int is_initial_run) {
    char virt_buf[16], res_buf[16], shr_buf[16];
    format_size(info->virt, virt_buf, sizeof(virt_buf));
    format_size(info->res,  res_buf, sizeof(res_buf));
    format_size(info->shr,  shr_buf, sizeof(shr_buf));

    if (is_initial_run) {
        printf("%-6d %-17s %-4ld %-4ld %-10s %-10s %-10s %-3c %-6.2f %-6s %-10lu %-20s\n",
               info->pid, info->user,
               info->priority, info->nice,
               virt_buf, res_buf, shr_buf,
               info->state, info->mem_percent,
               "-", // Remplacement par un tiret
               info->time, info->name);
    } else {
        printf("%-6d %-17s %-4ld %-4ld %-10s %-10s %-10s %-3c %-6.2f %-6.2f %-10lu %-20s\n",
               info->pid, info->user,
               info->priority, info->nice,
               virt_buf, res_buf, shr_buf,
               info->state, info->mem_percent,
               info->cpu_percent, 
               info->time, info->name);
    }
}

void ui_refresh_process_list(ProcessInfo processes[], int count, int is_initial_run) {
    system("clear"); 
    print_header();
    for (int i = 0; i < count; i++) {
        print_process(&processes[i], is_initial_run);
    }
    fflush(stdout);
}

// ----------- keyboard grabbing -----------------
static struct termios legacy_termios;
static int term_initialized = 0 ;

//back-to-canonical-mode function 
static void fallback_term(void){
    if(term_initialized){
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &legacy_termios);
    }
}

//initializer in order to ensure safe fallback
void term_init(void){
    if(!term_initialized){
        tcgetattr(STDIN_FILENO, &legacy_termios);  //we save the keyboard canonical layout
        term_initialized = 1;
        atexit(fallback_term);
    }
} 

//keyboard mode toggler : (between CANONICAL (0) & RAW (1) modes)
void term_toggle(int mode){
    if(!term_initialized){term_init();} //if not done already, ensure proper fallback option 
    static int rawlready = 0 ; 

    if(mode && !rawlready){ //ask for raw mode + isn't in raw mode -> proceed
        struct termios raw = legacy_termios; 
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        rawlready = 1;
    }else if(!mode && rawlready){ //ask for canonical mode + is in raw mode -> proceed
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &legacy_termios);
        rawlready = 0;
    }

}

int keyhit_check(void){
    struct timeval tv = { 0L, 0L }; //timeout to 0 
    fd_set fds; //dummy list of tracked files
    FD_ZERO(&fds); //reset tracked files 
    FD_SET(STDIN_FILENO, &fds); //set tracked files to the one storing input buffer
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv); //select() function that tells whether or not buffer is empty 
}

//key logic 
int input_handling(char inputed){
    int ch_out;
    switch(inputed){
        case 'q': {
            exit(0); //thanks to atexit(), automatic fallback to canonical mode 
            break;
        }
        case 'c': {
            term_toggle(0);
            char user_command[256];
            printf(" > ");
            fgets(user_command,sizeof(user_command),stdin);
            ch_out = command_handling(user_command);
            break;
        }
    }
    return 0;
}

int command_handling(char *command){
    //underlying logic for a user's written command
    /*printf("the given command was : %s",command);
    sleep(3);*/
    
    char *action = strtok(command," \n\t");
    if(!action){return;}

    //this is were it breaks

    /*printf("action : %s",action);
    getchar();*/
    if (strcmp(action,"help")==0 || strcmp(action, "h") == 0){
        printf("help | h : displays available commands\nkill <pid> : terminate the <pid> process\npause <pid> : freezes the <pid> process\nresume <pid> : unfreezes the <pid> process\nrestart <pid> : reload the <pid> process");
        
    }else if(strcmp(action,"quit")==0 || strcmp(action, "q") ==0){
        exit(0);
        //slight redundancy in exit methods - enhanced user experience
    }else{
        //action checking
        /*printf("action checking");
        getchar();*/
        if(strcomp(action,"sort")==0){
            char *crit = strtok(NULL," \n\t");
            int intcode;
            if(!crit){
                printf("error : give a valid sorting criteria");
                goto out; 
            }else if(strcomp(crit,"ram")){
                intcode = 1;
            } 

            
        }
        int ksignal = 0;
        if(strcmp(action,"kill")==0){
            ksignal = SIGTERM;
        }else if(strcmp(action,"pause")==0){
            ksignal = SIGSTOP;
        }else if(strcmp(action,"resume")==0){
            ksignal = SIGCONT;
        }else if(strcmp(action,"restart")==0){
            ksignal = SIGHUP;
        }else{
            printf("invalid action (%s)",action);
            goto out;
        }
        
        /*printf("pid checking");
        getchar();*/

        //pid checking
        char *pid_c = strtok(NULL," \n\t");
        char *endptr;
        if(!pid_c){
            printf("error : no pid given");
            goto out;
        }else{
            long pid = strtol(pid_c,&endptr,10);
            if (*endptr != '\0' || pid<=0) {
                printf("error '%s %s' : invalid <pid> format \n",action,pid_c);
            }else{
                //matching table for action
                if(ksignal && kill(pid,ksignal) == 0){
                    printf("process %ld %sed successfully",pid,action);
                }else{
                    printf("%s" ,(errno == ESRCH) ? "error : invalid pid" : "error : permission denied"); //it should be either PID issue or perm issue
                }
            }
        }
    }
    out:
    printf("\n\t--- press enter to continue ---");
    getchar();
    return 0;
}

// generic 
/*void trim_newline(char *str) {
    size_t len = strlen(str);
    // Check if the last character is a newline
    if (len > 0 && str[len - 1] == '\n') {
        str[len - 1] = '\0'; // Replace the newline with a null terminator
    }
}*/
//deprecated already -> using strtok()