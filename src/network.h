#ifndef NETWORK_H
#define NETWORK_H

#include <libssh/libssh.h>
#include "manager.h" // for RemoteHost definition
#include "process.h" // for ProcessInfo definition

// Function to establish connection (called once)
int network_connect(RemoteHost *host, ssh_session *session_out);

// Function to collect data (called every refresh)
int network_collect(ssh_session session, ProcessInfo processes[], int max_count);

void network_disconnect(ssh_session session);

// Sends a signal to a remote PID. Returns 0 on success.
int network_send_signal(ssh_session session, int pid, int signal_code);

int remote_command_handling(ssh_session session, char *command);
#endif