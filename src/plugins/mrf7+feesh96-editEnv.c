/*
 * An plug-in, which edits environment variables.
 * Matthew Fishman <feesh96> and Michael Friend <mrf7>
 */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "../esh.h"
#include <signal.h>
#include "../esh-sys-utils.h"

static bool
init_plugin(struct esh_shell *shell)
{
    printf("Plugin 'editEnv' initialized...\n");
    return true;
}

/* Implement editEnv built-in. Sets an environment variable.
 * Returns true if handled, false otherwise. */
static bool
edit_env_builtin(struct esh_command *cmd)
{
    if (strcmp(cmd->argv[0], "editEnv"))
        return false;

    char *variable = cmd->argv[1];
    char *newVal = cmd->argv[2];

    if (variable != NULL && newVal != NULL) {
      setenv(variable, newVal, 1);
    }
    else if (variable == NULL) {
        esh_sys_error("No environment variable given.\n");
    }
    else if (newVal == NULL) {
        esh_sys_error("No new value given.\n");
    }

    printf("Environemnt variable %s is set to %s\n", variable, getenv(variable));

    return true;
}

struct esh_plugin esh_module = {
  .rank = 1,
  .init = init_plugin,
  .process_builtin = edit_env_builtin
};
