#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"
#include "ui.h" 

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
               info->cpu_percent, // Affichage de la valeur
               info->time, info->name);
    }
}

void ui_refresh_process_list(ProcessInfo processes[], int count, int is_initial_run) {
    system("clear"); // Sera remplacé par ncurses plus tard
    print_header();
    for (int i = 0; i < count; i++) {
        print_process(&processes[i], is_initial_run);
    }
    fflush(stdout);
}

