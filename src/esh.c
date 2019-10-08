/*
 * esh - the 'pluggable' shell.
 *
 * Developed by Godmar Back for CS 3214 Fall 2009
 * Virginia Tech.
 *
 * Matthew Fishman <feesh96> and Michael Friend <mrf7>
 * Feburary
 */
#include <stdio.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "esh.h"

static void runJob(struct esh_pipeline * pipe);
static void builtin_fg(struct esh_command * pipe);
static void builtin_stop(struct esh_command * stopCommand);
static void builtin_kill(struct esh_command * killCommand);
static void builtin_bg(struct esh_command * bgCommand);
static void closeSafe(int fd);

static void usage(char *progname) {
    printf("Usage: %s -h\n"
        " -h            print this help\n"
        " -p  plugindir directory from which to load plug-ins\n",
        progname);

    exit(EXIT_SUCCESS);
}

/*
 * prints a background jobs jid and pids
 */
 static void printBackgroundJob(struct esh_pipeline * job) {
   printf("[%d]", job->jid);
   struct list_elem * currElem = list_begin(&job->commands);
   for (; currElem != list_end(&job->commands); currElem = list_next(currElem)) {
     struct esh_command * command = list_entry(currElem, struct esh_command, elem);
     printf(" %d", command->pid);
   }
   printf("\n");
 }
/* Build a prompt by assembling fragments from loaded plugins that
 * implement 'make_prompt.'
 *
 * This function demonstrates how to iterate over all loaded plugins.
 */
static char * build_prompt_from_plugins(void) {
    char *prompt = NULL;
    struct list_elem * e = list_begin(&esh_plugin_list);

    for (; e != list_end(&esh_plugin_list); e = list_next(e)) {
        struct esh_plugin *plugin = list_entry(e, struct esh_plugin, elem);

        if (plugin->make_prompt == NULL)
            continue;

        /* append prompt fragment created by plug-in */
        char * p = plugin->make_prompt();
        if (prompt == NULL) {
            prompt = p;
        } else {
            prompt = realloc(prompt, strlen(prompt) + strlen(p) + 1);
            strcat(prompt, p);
            free(p);
        }
    }

    /* default prompt */
    if (prompt == NULL)
        prompt = strdup("esh> ");

    return prompt;
}

/**
 * Assign ownership of ther terminal to process group
 * pgrp, restoring its terminal state if provided.
 *
 * Before printing a new prompt, the shell should
 * invoke this function with its own process group
 * id (obtained on startup via getpgrp()) and a
 * sane terminal state (obtained on startup via
 * esh_sys_tty_init()).
 */
static void give_terminal_to(pid_t pgrp, struct termios *pg_tty_state) {
    esh_signal_block(SIGTTOU);
    int rc = tcsetpgrp(esh_sys_tty_getfd(), pgrp);
    if (rc == -1)
        esh_sys_fatal_error("tcsetpgrp: ");

    if (pg_tty_state)
        esh_sys_tty_restore(pg_tty_state);
    esh_signal_unblock(SIGTTOU);
}

// Update the commands from the jobs_list when their status' change
static void child_status_change(pid_t child, int status) {
  struct esh_command * command; // Get the command
  if ((command = get_cmd_from_pid(child)) == NULL) {
    return;
  }
  struct esh_pipeline * pipe = command->pipeline; // Get the pipeline
  // Process stopped because signal was sent
  if (WIFSTOPPED(status)) {
    pipe->status = STOPPED;
	print_job(pipe);
    if (WSTOPSIG(status) != 22) {
      //print_job(pipe);
    }
  }
  // Exited normally
  if (WIFEXITED(status)) {
    list_remove(&command->elem);
  }
  // Terminated by signal
  else if (WIFSIGNALED(status)) {
    list_remove(&command->elem);
  }
  // Process resumed
  else if (WIFCONTINUED(status)) {
    //list_remove(&command->elem);
  }

  if (WIFSIGNALED(status)) {
    if (WTERMSIG(status) == 9){
      list_remove(&command->elem);
    }
    else if (WTERMSIG(status) == 2){
      list_remove(&command->elem);
    }
  }

  return;
}


/* SIGCHLD handler.
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD
 * signal may be delivered for multiple children that have
 * exited.
 */
static void sigchld_handler(int sig, siginfo_t *info, void *_ctxt) {
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        child_status_change(child, status);
    }
}

/* Wait for all processes in this pipeline to complete, or for
 * the pipeline's process group to no longer be the foreground
 * process group.
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 *
 * Implement child_status_change such that it records the
 * information obtained from waitpid() for pid 'child.'
 * If a child has exited or terminated (but not stopped!)
 * it should be removed from the list of commands of its
 * pipeline data structure so that an empty list is obtained
 * if all processes that are part of a pipeline have
 * terminated.  If you use a different approach to keep
 * track of commands, adjust the code accordingly.
 */
static void wait_for_job(struct esh_pipeline *pipeline) {
    assert(esh_signal_is_blocked(SIGCHLD));

    while (pipeline->status == FOREGROUND && !list_empty(&pipeline->commands)) {
        int status;
        //Waitpid for the second command doesn't return the child's pid...
        pid_t child = waitpid(-1, &status, WUNTRACED);
        if (child != -1)
            child_status_change(child, status);
    }
}

/*
 * Checks jobs list for finished jobs and removes/dispalys them
 */
static void cleanJobsList() {
  // Go throught each job in jobs list
  for (struct list_elem * currElem = list_begin(&jobs_list); currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
    // Get the pipeline struct
    struct esh_pipeline * pipeline = list_entry(currElem, struct esh_pipeline, elem);
    // Check if job is DONE
    if (list_empty(&pipeline->commands)) {
      // Remove job from the jobs list_end
      list_remove(currElem);
      // Dispaly job status if job wasnt in the foreground
      if (pipeline->status != FOREGROUND) {
        printf("[%d]\t", pipeline->jid);
        printf("Done\n");
      }
    }
  }
}

/* The shell object plugins use.
 * Some methods are set to defaults.
 */
struct esh_shell shell =
{
    .build_prompt = build_prompt_from_plugins,
    .readline = readline,       /* GNU readline(3) */
    .parse_command_line = esh_parse_command_line, /* Default parser */
    .get_cmd_from_pid = get_cmd_from_pid,
	  .get_job_from_jid = get_job_from_jid,
    .get_job_from_pgrp = get_job_from_pgrp,
    .get_jobs = get_jobs
};

int main(int ac, char *av[]) {
    int opt;
    list_init(&esh_plugin_list);
    list_init(&jobs_list);

    job_id = 0;
    terminal = esh_sys_tty_init();

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "hp:")) > 0) {         //Get command line options, only -h and -p are allowed
        switch (opt) {
        case 'h':                                       // Display Help
            usage(av[0]);
            break;

        case 'p':
            esh_plugin_load_from_directory(optarg);     // Load plugins. Opt arg is a variable in unistd.h where
            break;                                      // Get opts stores the arguments of the options
        }
    }

    esh_plugin_initialize(&shell);

    //Set sigchld handler
    esh_signal_sethandler(SIGCHLD, sigchld_handler);

    /* Read/eval loop. */
    for (;;) {
        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? shell.build_prompt() : NULL;
        // Before reading a line, clean the jobs list and display finished jobs, if shell on terminal
        if (isatty(0)) {
          cleanJobsList();
        }
        char * cmdline = shell.readline(prompt);
        free (prompt);
        // Give the raw command line to the plugins before parsing,
        // If one returns true, dont process this command line
        if (checkRawPlugin(&cmdline)) {
          continue;
        }
        if (cmdline == NULL)  /* User typed EOF */                      // Control-D
            break;

        struct esh_command_line * cline = shell.parse_command_line(cmdline);
        //esh_command_line is a list of esh_pipelines
        //esh_pipeline has a list of esh_commands and other fields
        //esh_command has information about a single command
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes)) {    /* User hit enter */
            esh_command_line_free(cline);
            continue;
        }

        while (!list_empty(&cline->pipes)) {
          struct list_elem * currElem = list_pop_front(&cline->pipes);
          struct esh_pipeline * current_pipeline = list_entry(currElem, struct esh_pipeline, elem);
          // If command isn't built in, run it normally
          if (!checkBuiltIn(current_pipeline) && !checkPlugin(current_pipeline)) {
            runJob(current_pipeline);
          }
        }
        //Free command line
        esh_command_line_free(cline);
    }
    return 0;
}


/* Checks if the command is a built in command
 * If it is a built in command runs the command and returns true,
 * otherwise returns false
 */
bool checkBuiltIn(struct esh_pipeline * pipeline) {
    // Get the first command of the pipeline
    struct esh_command * firstCommand = list_entry(list_begin(&pipeline->commands), struct esh_command, elem);
    char * firstCommandString = firstCommand->argv[0];

    if (strcmp(firstCommandString, "jobs") == 0) {
      	struct list_elem *  currElem = list_begin(&jobs_list);       //Get the list of pipes
      	for (; currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
        	struct esh_pipeline * current_pipeline = list_entry(currElem, struct esh_pipeline, elem);
        	print_job(current_pipeline);
    	}
    	return true;
    } else if (strcmp(firstCommandString, "kill") == 0) {
    	builtin_kill(firstCommand);
    	return true;
    } else if (strcmp(firstCommandString, "bg") ==0) {
    	builtin_bg(firstCommand);
    	return true;
    } else if (strcmp(firstCommandString, "fg") == 0) {
    	builtin_fg(firstCommand);
    	return true;
    } else if (strcmp(firstCommandString, "stop") == 0) {
    	builtin_stop(firstCommand);
    	return true;
    }

    return false;
}

/* returns true is processBuiltIn or process_pipeline returns true */
bool checkPlugin(struct esh_pipeline * pipeline) {
  struct list_elem * currElem = list_begin(&esh_plugin_list);
  for (; currElem != list_end(&esh_plugin_list); currElem = list_next(currElem)) {
    struct esh_plugin * plugin = list_entry(currElem, struct esh_plugin, elem);
    // Give each pipeline to the current plugin and return true if the plugin processed it
    if (plugin->process_pipeline != NULL && plugin->process_pipeline(pipeline)) {
      return true;
    }
    struct list_elem * currCommand = list_begin(&pipeline->commands);
    for (; currCommand != list_end(&pipeline->commands); currCommand = list_next(currCommand)) {
      struct esh_command * command = list_entry(currCommand, struct esh_command, elem);
      // Give each command to the current plugin and return true if the plugin processed it
      if (plugin->process_builtin != NULL && plugin->process_builtin(command)) {
        return true;
      }
    }
  }
  return false;
}
// Returns true if process_raw_cmdline returns true for some plugin
bool checkRawPlugin(char ** cmdline) {
  struct list_elem * currElem = list_begin(&esh_plugin_list);
  for (; currElem != list_end(&esh_plugin_list); currElem = list_next(currElem)) {
    struct esh_plugin * plugin = list_entry(currElem, struct esh_plugin, elem);
    // Give the command line to the plugin, if process_raw_cmdline is defined
    if (plugin->process_raw_cmdline != NULL && plugin->process_raw_cmdline(cmdline)) {
      return true;
    }
  }
  return false;
}
/* Runs a job descriped by pipe. Creates a new process for each
 * Command in the pipe and creates pipes to connect them.
 * If pipe->bg_job is false it runs in the foreground and waits for
 * the job to finish, if pipe->bg_job is true it runs job in the Background
 * and continues
*/
static void runJob(struct esh_pipeline * pipe) {
  struct list * commands = &(pipe->commands);
  struct list_elem * currElem = list_begin(commands);

  pipe->pgrp = -1;   // Flag for the first command

  // Create the pipes for before and after a given command
  int beforePipe[2], afterPipe[2];
  beforePipe[0] = afterPipe[0] = 1;
  beforePipe[1] = afterPipe[1] = 2;

  //Run through/execute commands
  for (; currElem != list_end(commands); currElem = list_next(currElem)) {
    // If >1 command and currElem is not the last command, create a new pipe
    if (list_size(commands) != 1 && list_next(currElem) != list_end(commands)) {
      createPipe(afterPipe);
    }

    struct esh_command * command = list_entry(currElem, struct esh_command, elem);
    int childPID = fork();
    if (childPID == 0) {  //Child process
      // If the first command, set the entire pipe's group id to its pid
      int pgid = pipe->pgrp;
      if (pgid == -1) {
        pgid = getpid();
      }

      // Set the current process' group id, output error on failure
      if (setpgid(getpid(), pgid) < 0) {
         esh_sys_fatal_error("Error Setting Process Group for pid: %d", getpid);
      }

      //If there is piping, connect them
      if (list_size(commands) != 1) {
        //Don't connect beforePipe/STDIN on first command
        if (currElem != list_begin(commands)) {
          dup2(beforePipe[0], STDIN_FILENO);
          closeSafe(beforePipe[0]);
          closeSafe(beforePipe[1]);
        }
        //Don't connect afterPipe/STDOUT on last command
        if (list_next(currElem) != list_end(commands)) {
          dup2(afterPipe[1], STDOUT_FILENO);
          closeSafe(afterPipe[0]);
          closeSafe(afterPipe[1]);
        }
      }

      // Redirect input if needed
      if (command->iored_input != NULL) {
        int input_fd = open(command->iored_input, O_RDONLY);  // Open readonly input file

        // Duplicate file into stdin, checking for failure
        if (dup2(input_fd, STDIN_FILENO) < 0) {
          esh_sys_fatal_error("dup2 error for input redir: %s", command->iored_input);
        }

        closeSafe(input_fd);  //Close input file
      }

      //Redirect output if needed
      if (command->iored_output != NULL) {
        int output_fd;

        //Opens the output file for appening if necessary (>>)
        if (command->append_to_output) {
          output_fd = open(command->iored_output, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
        } else {
          output_fd = open(command->iored_output, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
        }

        // Duplicate file into stdin, checking for failure
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
          esh_sys_fatal_error("dup2 error for output redir: %s", command->iored_output);
        }

        closeSafe(output_fd);
      }

      // execute the command
      if (execvp(command->argv[0], command->argv) < 0) {
        esh_sys_fatal_error("Exec Error: %s", command->argv[0]);
      }
    }

    else if (childPID < 0) { // Check for fork error
      esh_sys_fatal_error("Fork Error: %s", command->argv[0]);
    }

    else {  //Parent shell

      // If on first command, save pid as group id
      if (pipe->pgrp == -1) {
        pipe->pgrp = childPID;
      }

      // Set PID in commands list
      command->pid = childPID;

      // Redundant set the process group to avoid race conditionals
      if (setpgid(childPID, pipe->pgrp) < 0) {
         esh_sys_fatal_error("Error Setting Process Group for pid: %d", childPID);
      }

      // If we're piping...
      if (list_size(commands) != 1) {
        // If not first command, close before pipe
        if (currElem != list_begin(commands)) {
          closeSafe(beforePipe[0]);
          closeSafe(beforePipe[1]);
        }

        // If last command, close after pipe
        if (list_next(currElem) == list_end(commands)) {
          //closeSafe(afterPipe[0]);
          //closeSafe(afterPipe[1]);
        } else { // move afterPipe to beforePipe
            beforePipe[0] = afterPipe[0];
            beforePipe[1] = afterPipe[1];
        }
      }
    }

    //If a foreground job, give the terminal to the job... IDK if we need this
    if (!pipe->bg_job) {
      esh_sys_tty_save(&pipe->saved_tty_state);
      give_terminal_to(pipe->pgrp, &pipe->saved_tty_state);
    }
  }

  pipe->jid = findLowestFreeJobID();

  // block SIGCHLD for adding the pipeline to the jobs list
  esh_signal_block(SIGCHLD);
  list_push_back(&jobs_list, &pipe->elem);

  if (!pipe->bg_job) {
    pipe->status = FOREGROUND;
    wait_for_job(pipe);
    give_terminal_to(getpid(), terminal); //Give terminal back to shell
  } else {
    pipe->status = BACKGROUND;
    // Print the background jobs jid and pid
    printBackgroundJob(pipe);
  }

  //When the job finishes, unblock SIGCHLD
  esh_signal_unblock(SIGCHLD);
}

// Creates a pipe and does error handling
int createPipe(int pipeEnds[2]) {
    int pipeReturn = pipe(pipeEnds);
    if (pipeReturn == -1) {
        perror("Pipe Creation Failed"), exit(-1);
    } else {
        return pipeReturn;
    }
}
// Closes a file descriptor and checks for failure
static void closeSafe(int fd) {
	extern int errno;
	if (close(fd) != 0) {
		esh_sys_error("Error closing fd: %d. Error number: %d", fd, errno);
	}
}
static void printCommands(struct esh_pipeline * job) {
	  //Print each command
	  struct list_elem * currElem = list_begin(&job->commands);
	  for (; currElem != list_end(&job->commands); currElem = list_next(currElem)) {
	    struct esh_command * currCommand = list_entry(currElem, struct esh_command, elem);
	    for (int i = 0; currCommand->argv[i] != NULL; i++) {
	      printf("%s ", currCommand->argv[i]);
	    }
	    if (currElem->next != list_end(&job->commands)) {
	      printf("| ");
	    }
	  }
}
//Prints the jobs from the job list
void print_job(struct esh_pipeline * current_pipeline) {
  printf("[%d]\t", current_pipeline->jid);
  switch (current_pipeline->status) {
    case 0 :
      printf("Running\t\t");
      break;
    case 1 :
      printf("Running\t\t");
      break;
    case 2 :
      printf("Stopped\t\t");
      break;
    case 3 :
      printf("Stopped\t\t");
      break;
    default : //DONE or NULL
      printf("Done\t\t");
      break;
  }

  printCommands(current_pipeline);
  printf("\n");
}

int findLowestFreeJobID(void) {
  int lowest = 1;
  struct list_elem * currElem = list_begin(&jobs_list);
  for (; currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
    if (list_entry(currElem, struct esh_pipeline, elem)->jid == lowest) {
      currElem = list_head(&jobs_list);
      lowest++;
    }
  }
  return lowest;
}

/*
 * Executes the fg builtin command.
 * Argv[1] should contain the jid of the jobs
 * Gets the job from the jobs list with jid and puts the job in the FOREGROUND
 * Sends the sigcont signal to the pgrp to continue the job if its stopped
 */
static void builtin_fg(struct esh_command * fgCommand) {
	// Try to get jid from argv, making sure its entered and valid
	int jid;
	if (fgCommand->argv[1] == NULL) {
		printf("fg: job id missing or invalid\n");
		return;
	}
	if (sscanf(fgCommand->argv[1], "%d", &jid) != 1) {
		printf("fg: usage fg <job>\n");
		return;
	}

	// Get the job matching the jid, if no job output Error
	struct esh_pipeline * job = get_job_from_jid(jid);
	if (job == NULL || list_empty(&job->commands)) {
		printf("fg %d: No such job\n", jid);
    return;
	}
	// Print out the commands
	printCommands(job);
	printf("\n");
	fflush(stdout);
	// If job was stoppped, send the contiue signal
	if (job->status == STOPPED || job->status == NEEDSTERMINAL) {
		if (killpg(job->pgrp, SIGCONT) < 0) {
			esh_sys_fatal_error("Sending SIGCONT to %d failed", job->pgrp);
		}
	}

  // Move the job into the foreground and wait for it to finsh
	job->status = FOREGROUND;

	esh_signal_block(SIGCHLD);
  	give_terminal_to(job->pgrp, terminal);
	wait_for_job(job);
	esh_signal_unblock(SIGCHLD);
	give_terminal_to(getpid(), terminal);
}

/*
 * Executes the bg builtin command.
 * Argv[1] should contain the jid of the jobs
 * Gets the job from the jobs list with jid and puts the job in the background
 * Sends the sigcont signal to the pgrp to continue the job if its stopped
 */
static void builtin_bg(struct esh_command * bgCommand) {
		// Try to get jid from argv, making sure its entered and valid
		int jid;
		if (bgCommand->argv[1] == NULL) {
			printf("bg: job id missing or invalid\n");
			return;
		}
		if (sscanf(bgCommand->argv[1], "%d", &jid) != 1) {
			printf("bg: usage fg <job>\n");
			return;
		}

		// Get the job matching the jid, if no job output Error
		struct esh_pipeline * job = get_job_from_jid(jid);
		if (job == NULL) {
			printf("bg %d: No such job\n", jid);
      return;
		}

		// If job was stoppped, send the contiue signal
		if (job->status == STOPPED || job->status == NEEDSTERMINAL) {
			if (killpg(job->pgrp, SIGCONT) < 0) {
				esh_sys_fatal_error("Sending SIGCONT to %d failed", job->pgrp);
			}
		}
		job->status = BACKGROUND;

}

/*
 * Executes the stop command, sends the stop signal to a given process group
 */
static void builtin_stop(struct esh_command * stopCommand) {
	// Try to get jid from argv, making sure its entered and valid
	int jid;
	if (stopCommand->argv[1] == NULL) {
		printf("bg: job id missing or invalid\n");
		return;
	}
	if (sscanf(stopCommand->argv[1], "%d", &jid) != 1) {
		printf("bg: usage fg <job>\n");
		return;
	}

	// Get the job matching the jid, if no job output Error
	struct esh_pipeline * job = get_job_from_jid(jid);
	if (job == NULL) {
		printf("bg %d: No such job\n", jid);
    return;
	}

	// If job isn't stopped, send stop signal
	if (job->status != STOPPED && job->status != NEEDSTERMINAL) {
		if (killpg(job->pgrp, SIGSTOP) < 0) {
			esh_sys_fatal_error("Sending SIGSTOP to %d failed", job->pgrp);
		}
	}
}

/*
 * Executes built in kill function
 */
static void builtin_kill(struct esh_command * killCommand) {
	// Try to get jid from argv, making sure its entered and valid
	int jid;
	if (killCommand->argv[1] == NULL) {
		printf("bg: job id missing or invalid\n");
		return;
	}
	if (sscanf(killCommand->argv[1], "%d", &jid) != 1) {
		printf("bg: usage fg <job>\n");
		return;
	}

	// Get the job matching the jid, if no job output Error
	struct esh_pipeline * job = get_job_from_jid(jid);
	if (job == NULL) {
		printf("bg %d: No such job\n", jid);
    return;
	}

	// Send the kill signal
	if (killpg(job->pgrp, SIGKILL) < 0) {
		esh_sys_fatal_error("Sending SIGKILL to %d failed", job->pgrp);
	}
}


// Esh_shell functions -------------------------------------------------------

/*
 * Finds the job with the given job id - return NULL if not found
 */
struct esh_pipeline * get_job_from_jid(int jid) {
	//Search all jobs
	struct list_elem * currElem = list_begin(&jobs_list);
	for (; currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
		struct esh_pipeline * pipeline = list_entry(currElem, struct esh_pipeline, elem);
		if (pipeline->jid == jid) {
			return pipeline;
		}
	}
	return NULL;
}

/*
 * Finds the command with the given pid - return NULL if not found
 */
struct esh_command * get_cmd_from_pid(pid_t cmdPID) {
  //Search all jobs
  struct list_elem * currElem = list_begin(&jobs_list);
  for (; currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
    struct esh_pipeline * pipeline = list_entry(currElem, struct esh_pipeline, elem);
    struct list_elem * currCommand = list_begin(&pipeline->commands);

    //Search all commands within the job
    for (; currCommand != list_end(&pipeline->commands); currCommand = list_next(currCommand)) {
      struct esh_command * command = list_entry(currCommand, struct esh_command, elem);
      if (cmdPID == command->pid) {
        return command;
      }
    }
  }
  return NULL;
}

/*
 * Finds the job with the given pgid - return NULL if not found
 */
struct esh_pipeline * get_job_from_pgrp(pid_t pgrp) {
  //Search all jobs
	struct list_elem * currElem = list_begin(&jobs_list);
	for (; currElem != list_end(&jobs_list); currElem = list_next(currElem)) {
		struct esh_pipeline * pipeline = list_entry(currElem, struct esh_pipeline, elem);
		if (pipeline->pgrp == pgrp) {
			return pipeline;
		}
	}
	return NULL;
}

/* Return the list of current jobs */
struct list * get_jobs(void) {
  return &jobs_list;
}
