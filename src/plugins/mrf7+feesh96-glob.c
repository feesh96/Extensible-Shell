/** A glob expansion plugin that modifies the raw command line**/
#include <stdbool.h>
#include <stdio.h>
#include <glob.h>
#include <linux/limits.h>
#include <string.h>
#include "../esh.h"

static bool init_plugin(struct esh_shell * shell) {
  printf("Plugin 'glob' initialized...\n");
  return true;
}
// Function given to glob() to output errors
int globerr(const char * path, int errno) {
  fprintf(stderr, "Error expanding glob: %s:%s", path, strerror(errno));
  return 0;
}
// Takes the raw command line and
static bool process_raw_cmdline(char ** cmdline) {
  // If no star in cmdline, nothing to do
  if (!strstr(*cmdline, "*")) {
    return false;
  }
  char cmdlineCpy[PATH_MAX+1];
  strcpy(cmdlineCpy, *cmdline);
  //free(*cmdline);
  // If there is a glob to expand, create a new cmdline to start building
  char * newCmdLine = calloc(PATH_MAX+1, sizeof(char));
  // Get the first part of the command
  char * currEntry = strtok(cmdlineCpy, " \t");
  // Add each command to the newCmdLine, expanding if necessary

  while (currEntry != NULL) {
    // CHeck if need to expand
    if (!strstr(currEntry, "*")) {
      sprintf(newCmdLine +strlen(newCmdLine), " %s", currEntry);
      currEntry = strtok(NULL, " \t");
      continue;
    }
    glob_t results;
    int flags = 0;
    if (glob(currEntry, flags, globerr, &results) != 0) {
      fprintf(stderr, "Glob failed: %s\n",currEntry);
      break;
    }
    // print out each result of the glob
    int i = 0;
    for (; i < results.gl_pathc; i++) {
      sprintf(newCmdLine + strlen(newCmdLine), " %s", results.gl_pathv[i]);
    }
    globfree(&results);
    currEntry = strtok(NULL, " \t");
  }
  *cmdline = newCmdLine;
  return false;
}
// int main(int argc, char ** argv) {
//   char * cmd = "*.c";
//   process_raw_cmdline(&cmd);
//   printf("%s\n",cmd);
//   cmd = "ls *.h";
//   process_raw_cmdline(&cmd);
//   printf("%s\n",cmd);
// }
struct esh_plugin esh_module = {
  .rank = 1,
  .init = init_plugin,
  .process_raw_cmdline = process_raw_cmdline
};
