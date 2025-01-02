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
#include <signal.h>
#include "tokenizer.h"
#include <sys/stat.h>

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))
typedef enum IORe
{
  in,
  out,
  other
} IORe_t;

int Oid;
int Aid;

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

pid_t Cpid;

char *path_res(char *file);
int exe(unused struct tokens *argvs, bool backPro);
int mine_wait(struct tokens *argvs);
int mine_exit(struct tokens *argvs);
int mine_help(struct tokens *argvs);
int mine_pwd(struct tokens *argvs);
int mine_changedir(struct tokens *argvs);


/* Built-in command functions take token array (see parse.h) and return int */
typedef int mine_fun_t(struct tokens *argvs);

/* Built-in command struct and lookup table */
typedef struct fun_desc
{
  mine_fun_t *fun;
  char *mine;
  char *doc;
} fun_desc_t;

fun_desc_t mine_table[] = {
{mine_help, "?", "show this help menu"},
    {mine_exit, "exit", "exit the command shell"},
    {mine_pwd, "pwd", "show  current working directory path"},
    {mine_changedir, "cd", "changes  directory to destination directory"},
    {mine_wait, "wait", "wait for the child process to terminate!" },
    };

// Qhu

char *path_res(char *file)
{
  char *PATH = getenv("PATH");
  char *path = (char *)calloc(1024, sizeof(char));
  char *Cpath = PATH;

  while(true) {
    char *next_path = strchr(Cpath, ':');
    if(!next_path) {
      next_path = strchr(Cpath, '0');
    }
    size_t len = next_path - Cpath;
    strncpy(path, Cpath, len);
    path[len] = 0;

    strcat(path, "/");
    strcat(path, file);
    if(!access(path, F_OK)) {
      return path;
    }
    
    if(*next_path) {
      Cpath = next_path + 1;
    } else {
      break;
    }
  }

  free(path);
  return NULL;
}

void IORe(char *FileName, IORe_t QPro)
{
  if (FileName == NULL)
    return;
  int current_id;
  switch (QPro)
  {
  case in:
    current_id = STDIN_FILENO;
    Aid = open(FileName, O_RDONLY);
    if (-1 == Aid)
      perror("open error!");
    Oid = dup(STDIN_FILENO);
    if (-1 == Oid)
      perror("dup error!");
    break;
  case out:
    current_id = STDOUT_FILENO;
    Aid = creat(FileName,  S_IRUSR | S_IWUSR |
                                            S_IRGRP | S_IWGRP |
                                            S_IROTH | S_IWOTH);;
    if (-1 == Aid)
      perror("open error!");
    Oid = dup(STDOUT_FILENO);
    if (-1 == Oid)
      perror("dup error!");
    break;

  default:
    break;
  }
  dup2(Aid, current_id);
  close(Aid);
}


int exe(unused struct tokens *argvs, bool backPro)
{
  int length = tokens_get_length(argvs);
  if (length <= 0)
  {
    printf("no command!\n ");
    return -1;
  }
  char **argv = (char **)calloc(length + 1, sizeof(char *));
  int argv_index = 0;
  char *FileName;
  IORe_t QPro;
  for (int i = 0; i < length; ++i)
  {
    char *current_token = tokens_get_token(argvs, i);
    if (!strcmp(current_token, "<"))
      QPro = in;
    else if (!strcmp(current_token, ">"))
      QPro = out;
    else
      QPro = other;
    if (QPro == other)
    {
      argv[argv_index++] = current_token;
    }
    else
    {
      FileName = tokens_get_token(argvs, ++i);
      IORe(FileName, QPro);
      break;
    }
  }
  char *ProName = argv[0];
  ProName = path_res(ProName);
  char **args = (char **)malloc(length * sizeof(char *) + 1);
  args[0] = ProName;
  // puts(args[0]);
  if (QPro != other)
    length -= 2;
  if (backPro)
    length -= 1;
  for (int i = 1; i < length; i++)
  {
    args[i] = argv[i];
  }
  int res = execv(ProName, args);
  if (-1 == res)
  {
    perror("execv error!");
    free(args);
    return -1;
  }
  free(args);
  free(argv);
  return 1;
}

int mine_wait(struct tokens *argvs)
{
  int sta;
  int pid = waitpid(Oid,&sta,WUNTRACED);
  while (pid)
  {
    if (pid == -1)
    {
      printf("wait error!\n");
      break;
    }
    else
    {
      printf("child #%d terminated.\n", pid);
      break;
    }
  }
  return 0;
}

/* Prints a helpful description for the given command */
int mine_help(unused struct tokens *argvs)
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", mine_table[i].mine, mine_table[i].doc);
  return 1;
}

int mine_pwd(unused struct tokens *argvs)
{
  char buffer[1024];
  getcwd(buffer, 1024);
  if (buffer == NULL)
  {
    perror("getcwd error!");
    return -1;
  }

  else
  {
    printf("%s\n", buffer);
    return 1;
  }
}

int mine_changedir(unused struct tokens *argvs)
{
  char *argument = tokens_get_token(argvs, 1);
  if (!argument){
    char *home_dir = getenv("HOME");
    if (home_dir)
      chdir(home_dir);
    else
    {
      fprintf(stderr, "cd: HOME not set\n");
      return -1;
  }
  }
  else
  {
    chdir(argument);
  }
  return 1;
}

/* Exits this shell */
int mine_exit(unused struct tokens *argvs) { exit(0); }

/* Looks up the built-in command, if it exists. */
int lookup(char mine[])
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    if (mine && (strcmp(mine_table[i].mine, mine) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell()
{
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive)
  {
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

int main(unused int argc, unused char *argv[])
{
  init_shell();
  int sta;
  static char line[4096];
  int Lindex = 0;

  /* Please only print shell QPros when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", Lindex);

  while (fgets(line, 4096, stdin))
  {
    /* Split our line into words. */
    struct tokens *argvs = tokenize(line);

    /* Find which built-in function to run. */
    int Findex = lookup(tokens_get_token(argvs, 0));
    size_t token_length = tokens_get_length(argvs);
    bool backPro = false;
    if (Findex >= 0)
    {
      mine_table[Findex].fun(argvs);
    }
    else
    {
      if (!strcmp(tokens_get_token(argvs, token_length - 1), "&"))
        backPro = true;
      /* REPLACE this to run commands as programs. */
      pid_t childPid = fork();
      switch (childPid)
      {
      case -1:
        perror("fork error");
        exit(EXIT_FAILURE);
      case 0:
        exe(argvs, backPro);
        _exit(0);
      default:
      {
        Cpid = childPid;
        int status;
        setpgid(childPid, childPid);
        if(tcsetpgrp(shell_terminal, Cpid) == 0) {
            if ((waitpid(Cpid, &status, WUNTRACED)) < 0) {
              perror("wait failed");
              _exit(2);
            }
            printf( "\n");
            signal(SIGTTOU, SIG_IGN);
            if(tcsetpgrp(shell_terminal, shell_pgid) != 0)
              printf("switch to shell error occurred!\n");
            signal(SIGTTOU, SIG_DFL);

          } else {
            printf("tcsetpgrp error occurred\n");
          }
        break;
      }
        
      }

      signal(SIGTTOU, SIG_IGN);
      // Qhu
      if (!backPro)
      {
       unused int ret = waitpid(-1,&sta,0);
        signal(SIGTTOU, SIG_DFL);
        // Qhu
      }
    }

    if (shell_is_interactive)
      /* Please only print shell QPros when standard input is not a tty */
      fprintf(stdout, "%d: ", ++Lindex);

    /* Clean up memory */
    tokens_destroy(argvs);
  }

  return 0;
}
