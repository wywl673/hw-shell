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
#include <sys/stat.h>
#include "tokenizer.h"

/* 定义一个宏，用于消除未使用参数的编译器警告 */
#define unused __attribute__((unused))

/* 枚举类型，用于区分输入、输出和其他操作 */
typedef enum IORe
{
  in,    // 输入重定向
  out,   // 输出重定向
  other  // 其他操作
} IORe_t;

/* 全局变量，用于存储文件描述符 */
int Oid;
int Aid;

/* 判断shell是否是交互模式 */
bool shell_is_interactive;

/* shell的文件描述符 */
int shell_terminal;

/* shell的终端模式 */
struct termios shell_tmodes;

/* shell的进程组ID */
pid_t shell_pgid;

/* 当前子进程ID */
pid_t Cpid;

/* 函数声明 */
char *path_res(char *file);
int exe(unused struct tokens *argvs, bool backPro);
int mine_wait(struct tokens *argvs);
int mine_exit(struct tokens *argvs);
int mine_help(struct tokens *argvs);
int mine_pwd(struct tokens *argvs);
int mine_changedir(struct tokens *argvs);

/* 内建命令的函数类型 */
typedef int mine_fun_t(struct tokens *argvs);

/* 内建命令结构和查找表 */
typedef struct fun_desc
{
  mine_fun_t *fun;   // 函数指针
  char *mine;        // 命令名称
  char *doc;         // 命令说明
} fun_desc_t;

/* 定义内建命令表 */
fun_desc_t mine_table[] = {
    {mine_help, "?", "显示帮助菜单"},
    {mine_exit, "exit", "退出命令行shell"},
    {mine_pwd, "pwd", "显示当前工作目录"},
    {mine_changedir, "cd", "切换到指定目录"},
    {mine_wait, "wait", "等待子进程结束"}
};

/* 路径解析函数，查找可执行文件路径 */
char *path_res(char *file)
{
  char *PATH = getenv("PATH");
  char *path = (char *)calloc(1024, sizeof(char));
  char *Cpath = PATH;

  while (true) {
    char *next_path = strchr(Cpath, ':');
    if (!next_path) {
      next_path = strchr(Cpath, '0');
    }
    size_t len = next_path - Cpath;
    strncpy(path, Cpath, len);
    path[len] = 0;

    strcat(path, "/");
    strcat(path, file);
    if (!access(path, F_OK)) {
      return path;
    }

    if (*next_path) {
      Cpath = next_path + 1;
    } else {
      break;
    }
  }

  free(path);
  return NULL;
}

/* 输入/输出重定向函数 */
void IORe(char *FileName, IORe_t QPro)
{
  if (FileName == NULL)
    return;

  int current_id;
  switch (QPro)
  {
  case in: // 输入重定向
    current_id = STDIN_FILENO;
    Aid = open(FileName, O_RDONLY);
    if (-1 == Aid)
      perror("打开文件出错！");
    Oid = dup(STDIN_FILENO);
    if (-1 == Oid)
      perror("复制文件描述符出错！");
    break;

  case out: // 输出重定向
    current_id = STDOUT_FILENO;
    Aid = creat(FileName, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (-1 == Aid)
      perror("创建文件出错！");
    Oid = dup(STDOUT_FILENO);
    if (-1 == Oid)
      perror("复制文件描述符出错！");
    break;

  default:
    break;
  }
  dup2(Aid, current_id);
  close(Aid);
}

/* 执行命令函数 */
int exe(unused struct tokens *argvs, bool backPro)
{
  int length = tokens_get_length(argvs);
  if (length <= 0)
  {
    printf("没有命令！\n");
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
    perror("执行命令失败！");
    free(args);
    return -1;
  }

  free(args);
  free(argv);
  return 1;
}

/* 等待子进程结束 */
int mine_wait(struct tokens *argvs)
{
  int sta;
  int pid = waitpid(Oid, &sta, WUNTRACED);
  while (pid)
  {
    if (pid == -1)
    {
      printf("等待出错！\n");
      break;
    }
    else
    {
      printf("子进程 #%d 已结束。\n", pid);
      break;
    }
  }
  return 0;
}

/* 帮助命令 */
int mine_help(unused struct tokens *argvs)
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", mine_table[i].mine, mine_table[i].doc);
  return 1;
}

/* 显示当前目录 */
int mine_pwd(unused struct tokens *argvs)
{
  char buffer[1024];
  getcwd(buffer, 1024);
  if (buffer == NULL)
  {
    perror("获取当前目录出错！");
    return -1;
  }
  else
  {
    printf("%s\n", buffer);
    return 1;
  }
}

/* 切换目录 */
int mine_changedir(unused struct tokens *argvs)
{
  char *argument = tokens_get_token(argvs, 1);
  if (!argument)
  {
    char *home_dir = getenv("HOME");
    if (home_dir)
      chdir(home_dir);
    else
    {
      fprintf(stderr, "cd: HOME 未设置\n");
      return -1;
    }
  }
  else
  {
    chdir(argument);
  }
  return 1;
}

/* 退出命令 */
int mine_exit(unused struct tokens *argvs) { exit(0); }

/* 查找内建命令 */
int lookup(char mine[])
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    if (mine && (strcmp(mine_table[i].mine, mine) == 0))
      return i;
  return -1;
}

/* 初始化shell */
void init_shell()
{
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive)
  {
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    shell_pgid = getpid();
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

/* 主函数 */
int main(unused int argc, unused char *argv[])
{
  init_shell();
  int sta;
  static char line[4096];
  int Lindex = 0;

  if (shell_is_interactive)
    fprintf(stdout, "%d: ", Lindex);

  while (fgets(line, 4096, stdin))
  {
    struct tokens *argvs = tokenize(line);
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

      pid_t childPid = fork();
      switch (childPid)
      {
      case -1:
        perror("创建子进程失败");
        exit(EXIT_FAILURE);
      case 0:
        exe(argvs, backPro);
        _exit(0);
      default:
      {
        Cpid = childPid;
        int status;
        setpgid(childPid, childPid);
        if (tcsetpgrp(shell_terminal, Cpid) == 0)
        {
          if ((waitpid(Cpid, &status, WUNTRACED)) < 0)
          {
            perror("等待子进程失败");
            _exit(2);
          }
          printf("\n");
          signal(SIGTTOU, SIG_IGN);
          if (tcsetpgrp(shell_terminal, shell_pgid) != 0)
            printf("切换回shell时出错！\n");
          signal(SIGTTOU, SIG_DFL);
        }
        else
        {
          printf("tcsetpgrp 出错\n");
        }
        break;
      }
      }

      signal(SIGTTOU, SIG_IGN);
      if (!backPro)
      {
        unused int ret = waitpid(-1, &sta, 0);
        signal(SIGTTOU, SIG_DFL);
      }
    }

    if (shell_is_interactive)
      fprintf(stdout, "%d: ", ++Lindex);

    tokens_destroy(argvs);
  }

  return 0;
}
