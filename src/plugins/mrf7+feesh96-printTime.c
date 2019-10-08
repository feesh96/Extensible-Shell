/*
 * An plug-in, which prints the current date, hour, and minute.
 * Matthew Fishman <feesh96> and Michael Friend <mrf7>
 */
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include "../esh.h"
#include <signal.h>
#include "../esh-sys-utils.h"

static bool
init_plugin(struct esh_shell *shell)
{
    printf("Plugin 'printTime' initialized...\n");
    return true;
}

/* Implement editEnv built-in. Sets an environment variable.
 * Returns true if handled, false otherwise. */
static bool
print_time_builtin(struct esh_command *cmd)
{
    if (strcmp(cmd->argv[0], "printTime"))
        return false;

    time_t rawtime;
    struct tm * timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    printf("Time/date:\n");
    printf("%s", asctime(timeinfo));

    return true;
}

struct esh_plugin esh_module = {
  .rank = 1,
  .init = init_plugin,
  .process_builtin = print_time_builtin
};
