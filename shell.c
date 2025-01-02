#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

char *get_fullpath(char *name);
int exe(unused struct tokens *argvs, bool backPro);
int cmd_wait(struct tokens *argvs);
int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_changedir(struct tokens *argvs);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

struct ch_process {
	int tokens_len;
	int next_token;
	char **args;
	int in_fd;
	int out_fd;
	int out_attr;
};

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_cd, "cd", "change the working directory" },
	  {cmd_pwd, "pwd", "print name of current/working directory" },
    {cmd_wait, "wait", "wait for the child process to terminate!" },
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

#define shell_msg(FORMAT, ...) \
do {\
	if (shell_is_interactive) { \
		fprintf(stdout, FORMAT, ##__VA_ARGS__);\
	} \
} while(0)

/* change working directory */
int cmd_cd(unused struct tokens *tokens)
{
	char *dst = NULL;
	int res = -1;

	switch (tokens_get_length(tokens)) {
	case 1:	/* no directory operand is given, if HOME is given, cd $HOME */
		dst = getenv("HOME");
		break;
	case 2:
		dst = tokens_get_token(tokens, 1);
		break;
	default:
		shell_msg("too many argument\n");
	}
	if (dst == NULL)
		return -1;
	res = chdir(dst);
	if (res == -1)
		shell_msg("No such file or directory\n");
	return res;
}

/* get current full path */
int cmd_pwd(unused struct tokens *tokens)
{
	char *path = getcwd(NULL, 0);
	if (path == NULL) {
		shell_msg("%s\n", strerror(errno));
		return -1;
	}
	printf("%s\n", path);
	free(path);
	return 0;
}

char *get_fullpath(char *name)
{
	char *val = getenv("PATH");
	int i, j, len;
	char *path = (char *)malloc(BUFSIZ);
	/* if name is already full path */
	strcpy(path, name);
	if (access(path, X_OK) == 0)
		return path;
	/* enumerate $PATH and search reachable path */
	len = strlen(val);
	i = 0;
	while (i < len) {
		j = i;
		while (j < len && val[j] != ':')
			j++;
		int k = j - i;
		memset(path, 0, BUFSIZ);
		strncpy(path, val + i, k);
		path[k] = '/';
		strcpy(path + k + 1, name);
		if (access(path, X_OK) == 0)
			return path;
		i = j + 1;
	}
	free(path);
	return NULL;
}


void parse_args(struct ch_process *ch, struct tokens *tokens)
{
	char *token;
	int finish = 0;
	while (ch->next_token < ch->tokens_len && !finish) {
		token = tokens_get_token(tokens, ch->next_token);
		/* if first char of token is < or >, break */
		finish = (token[0] == '<' || token[0] == '>');
		/* if not finish, !finish 1, then args[next_token] = token, then next_token inccrease
		else if finish, args[next_token] = NULL, and next_token refer to the first < or > or >> */
		/* This line may be hard to understand, but it can avoid IF branch */
		ch->args[ch->next_token] = (char *)((!finish) * (int64_t)(void*)(token));
		ch->next_token += !finish;
	}
	ch->args[ch->next_token] = NULL;
}

void parse_redirection(struct ch_process *ch, struct tokens *tokens) {
    char *arrow, *path;

    while (ch->next_token < ch->tokens_len) {
        // Get redirection operator ('<', '>', or '>>')
        arrow = tokens_get_token(tokens, ch->next_token++);
        
        // Ensure there is a file path after the operator
        if (ch->next_token >= ch->tokens_len) {
            fprintf(stderr, "No file specified after '%s'\n", arrow);
            return;
        }
        path = tokens_get_token(tokens, ch->next_token++);

        switch (arrow[0]) {
            case '<':
                // Redirect input
                if (access(path, R_OK) == 0) {
                    if (ch->in_fd != 0) {
                        close(ch->in_fd);
                    }
                    ch->in_fd = open(path, O_RDONLY);
                } else {
                    fprintf(stderr, "%s does not exist or is not readable\n", path);
                    return;
                }
                break;

            case '>':
                // Redirect output
                ch->out_attr = O_WRONLY | O_CREAT;
                if (arrow[1] == '>') {
                    ch->out_attr |= O_APPEND;
                } else {
                    ch->out_attr |= O_TRUNC;
                }

                if (ch->out_fd != 1) {
                    close(ch->out_fd);
                }
                ch->out_fd = open(path, ch->out_attr, 0664);
                if (ch->out_fd == -1) {
                    perror("Failed to open file for output");
                    return;
                }
                break;

            default:
                fprintf(stderr, "Unknown redirection operator: '%s'\n", arrow);
                return;
        }
    }
}

/* start a child process to execute program */
int run_program(struct tokens *tokens) {
    int tokens_len = tokens_get_length(tokens);
    if (tokens_len == 0) {  /* no input */
        exit(0);
    }

    // Initialize child process struct
    char *args[tokens_len + 1];
    struct ch_process child = { 0 };
    child.tokens_len = tokens_len;
    child.next_token = 0;
    child.args = args;
    child.in_fd = 0;   // Default input: stdin
    child.out_fd = 1;  // Default output: stdout

    // Parse arguments (extract command-line args, ignoring redirection symbols)
    parse_args(&child, tokens);

    // Parse redirection symbols (<, >, >>)
    parse_redirection(&child, tokens);

    // Fork the process to execute the command
    pid_t chpid = fork();
    if (chpid < 0) {  /* fork error */
        shell_msg("fork: %s\n", strerror(errno));
        return -1;
    } else if (chpid == 0) {  /* child process */
        // Handle input redirection
        if (child.in_fd != 0) {
            dup2(child.in_fd, 0);  // Redirect stdin
        }

        // Handle output redirection
        if (child.out_fd != 1) {
            dup2(child.out_fd, 1);  // Redirect stdout
        }

        // Close unused file descriptors
        if (child.in_fd != 0) {
            close(child.in_fd);
        }
        if (child.out_fd != 1) {
            close(child.out_fd);
        }

        // Execute the program
        execvp(child.args[0], child.args);

        // If execvp fails
        shell_msg("execvp: %s\n", strerror(errno));
        exit(1);
    }

    // Parent process: Wait for child to complete1
    if (wait(NULL) == -1) {
        shell_msg("wait: %s\n", strerror(errno));
        return -1;
    }

    // Close file descriptors in parent process
    if (child.in_fd != 0) {
        close(child.in_fd);
    }
    if (child.out_fd != 1) {
        close(child.out_fd);
    }

    return 0;
}


/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      fprintf(stdout, "This shell doesn't know how to run programs.\n");
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
