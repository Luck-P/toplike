
// Implémentations futures...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/libssh.h>
#include "network.h"
#include "process.h"

// 1. Establish the SSH Connection
int network_connect(RemoteHost *host, ssh_session *session_out) {
    ssh_session session = ssh_new();
    if (session == NULL) return -1;

    // Set options
    ssh_options_set(session, SSH_OPTIONS_HOST, host->address);
    ssh_options_set(session, SSH_OPTIONS_PORT, &host->port);
    ssh_options_set(session, SSH_OPTIONS_USER, host->username);

    // Connect
    if (ssh_connect(session) != SSH_OK) {
        fprintf(stderr, "Error connecting: %s\n", ssh_get_error(session));
        ssh_free(session);
        return -1;
    }

    // Authenticate (Password)
    // In a real app, you would try public key first, then password
    if (ssh_userauth_password(session, NULL, host->password) != SSH_AUTH_SUCCESS) {
        fprintf(stderr, "Auth error: %s\n", ssh_get_error(session));
        ssh_disconnect(session);
        ssh_free(session);
        return -1;
    }

    *session_out = session;
    return 0; // Success
}

// 2. Run 'ps' and Parse Output
int network_collect(ssh_session session, ProcessInfo processes[], int max_count) {
    ssh_channel channel;
    int rc;
    char buffer[4096];
    int nbytes;
    int count = 0;

    channel = ssh_channel_new(session);
    if (channel == NULL) return 0;

    if (ssh_channel_open_session(channel) != SSH_OK) {
        ssh_channel_free(channel);
        return 0;
    }

    // The command: match the columns to your struct
    // pid, user, state, priority, nice, virt(kb), res(kb), mem%, cpu%, time(sec), command
    //const char *cmd = "ps -Ao pid,user,state,pri,ni,vsz,rss,pmem,pcpu,times,comm --no-headers --sort=-pcpu | head -n 50";
    const char *cmd = "ps -A -o pid,user,state,pri,ni,vsz,rss,pmem,pcpu,times,comm";
    rc = ssh_channel_request_exec(channel, cmd);
    if (rc != SSH_OK) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        return 0;
    }

    // Buffer to accumulate output
    char output_acc[16384] = ""; 
    
    // Simple read loop (in production, handle large outputs more robustly)
    while ((nbytes = ssh_channel_read(channel, buffer, sizeof(buffer), 0)) > 0) {
        if (strlen(output_acc) + nbytes < sizeof(output_acc) - 1) {
            strncat(output_acc, buffer, nbytes);
        }
    }

    // Parse the accumulated string line by line
    char *line = strtok(output_acc, "\n");
    int cur_line=0;
    while (line != NULL && count < max_count) {
        if (cur_line==0 && strstr(line, "PID")){
            line = strtok(NULL, "\n");
            cur_line++;
            continue;
        }

        ProcessInfo *p = &processes[count];
        
        // sscanf format matching 'ps' columns
        // Note: VSZ/RSS from ps are in KB, your struct might expect Bytes. 
        // We multiply by 1024 to match local units.
        unsigned long vsz_kb = 0, rss_kb = 0;
        
        // ps format: PID USER S PRI NI VSZ RSS %MEM %CPU TIME COMMAND
        int fields = sscanf(line, "%d %63s %c %ld %ld %lu %lu %lf %lf %lu %255s",
            &p->pid,
            p->user,
            &p->state,
            &p->priority,
            &p->nice,
            &vsz_kb,
            &rss_kb,
            &p->mem_percent,
            &p->cpu_percent,
            &p->time,
            p->name
        );

        if (fields >= 10) { // If we parsed enough fields
            p->virt = vsz_kb * 1024;
            p->res  = rss_kb * 1024;
            p->shr  = 0; // ps doesn't give shared mem easily
            count++;
        }

        line = strtok(NULL, "\n");
    }

    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);

    return count;
}

int network_send_signal(ssh_session session, int pid, int signal_code) {
    ssh_channel channel = ssh_channel_new(session);
    if (!channel) return -1;

    if (ssh_channel_open_session(channel) != SSH_OK) {
        ssh_channel_free(channel);
        return -1;
    }

    char cmd[64];
    // Construct command: "kill -15 1234"
    // We use numeric signals because they are portable (Android/Linux)
    snprintf(cmd, sizeof(cmd), "kill -%d %d", signal_code, pid);

    int rc = ssh_channel_request_exec(channel, cmd);
    
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    
    return (rc == SSH_OK) ? 0 : -1;
}

int remote_command_handling(ssh_session session, char *command) {
    if (session == NULL) {
        printf("Error: No active remote session.\n");
        goto out;
    }

    // 1. Parse the Action
    // Note: strtok modifies the original command string
    char *action = strtok(command, " \n\t");
    if (!action) return 0;

    // Check for help/quit first
    if (strcmp(action, "help") == 0 || strcmp(action, "h") == 0) {
        printf("REMOTE COMMANDS:\n");
        printf("  kill <pid>    : Terminate process (SIGTERM/15)\n");
        printf("  pause <pid>   : Suspend process (SIGSTOP/19)\n");
        printf("  resume <pid>  : Unfreeze process (SIGCONT/18)\n");
        printf("  restart <pid> : Reload process (SIGHUP/1)\n");
        goto out;
    } 
    else if (strcmp(action, "quit") == 0 || strcmp(action, "q") == 0) {
        exit(0);
    }

    // 2. Determine Signal Code
    // We use integer literals because macros like SIGTERM might differ 
    // slightly across architectures, but these 4 are standard on Linux/Android.
    int signal_code = 0;
    
    if (strcmp(action, "kill") == 0)        signal_code = 15; // SIGTERM
    else if (strcmp(action, "pause") == 0)  signal_code = 19; // SIGSTOP
    else if (strcmp(action, "resume") == 0) signal_code = 18; // SIGCONT
    else if (strcmp(action, "restart") == 0) signal_code = 1;  // SIGHUP
    else {
        printf("Invalid remote action: '%s' (try 'help')\n", action);
        goto out;
    }

    // 3. Parse the PID
    char *pid_str = strtok(NULL, " \n\t");
    if (!pid_str) {
        printf("Error: Missing <pid> for action '%s'\n", action);
        goto out;
    }
    
    int pid = atoi(pid_str);
    if (pid <= 0) {
        printf("Error: Invalid PID format '%s'\n", pid_str);
        goto out;
    }

    // 4. Execute Command via SSH
    printf("Sending signal %d to remote PID %d...\n", signal_code, pid);

    ssh_channel channel = ssh_channel_new(session);
    if (!channel) {
        printf("Error: Could not create SSH channel.\n");
        goto out;
    }

    if (ssh_channel_open_session(channel) != SSH_OK) {
        printf("Error: Could not open SSH session.\n");
        ssh_channel_free(channel);
        goto out;
    }

    // Construct the command string (e.g., "kill -15 1234")
    char shell_cmd[64];
    snprintf(shell_cmd, sizeof(shell_cmd), "kill -%d %d", signal_code, pid);

    // Send it
    int rc = ssh_channel_request_exec(channel, shell_cmd);
    
    if (rc == SSH_OK) {
        printf("Command '%s' sent successfully.\n", shell_cmd);
    } else {
        printf("Failed to execute command on remote host.\n");
    }

    // Cleanup
    ssh_channel_close(channel);
    ssh_channel_free(channel);

out:
    // Wait for user confirmation so they can read the status
    printf("\n\t--- press enter to continue ---");
    getchar();
    return 0;
}


void network_disconnect(ssh_session session) {
    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
    }
}