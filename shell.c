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

/* 用于消除编译器关于未使用的函数参数的警告的便捷宏 */
#define unused __attribute__((unused))

typedef enum IORe
{
  in,    // 输入重定向
  out,   // 输出重定向
  other  // 其他情况
} IORe_t;

int Oid;  // 保存标准输入/输出文件描述符
int Aid;  // 文件打开/创建时的文件描述符

/* 判断当前 shell 是否与一个实际的终端连接 */
bool shell_is_interactive;

/* shell 输入的文件描述符 */
int shell_terminal;

/* shell 的终端模式设置 */
struct termios shell_tmodes;

/* shell 的进程组 ID */
pid_t shell_pgid;

/* 当前子进程的 ID */
pid_t Cpid;

/* 查找文件路径 */
char *path_res(char *file);

/* 执行命令 */
int exe(unused struct tokens *argvs, bool backPro);

/* 等待子进程 */
int mine_wait(struct tokens *argvs);

/* 退出命令 */
int mine_exit(struct tokens *argvs);

/* 帮助命令 */
int mine_help(struct tokens *argvs);

/* 显示当前工作目录 */
int mine_pwd(struct tokens *argvs);

/* 更改目录命令 */
int mine_changedir(struct tokens *argvs);

/* 内建命令的函数类型 */
typedef int mine_fun_t(struct tokens *argvs);

/* 内建命令结构体和查找表 */
typedef struct fun_desc
{
  mine_fun_t *fun;   // 内建命令函数指针
  char *mine;         // 命令名称
  char *doc;          // 命令描述
} fun_desc_t;

/* 内建命令的查找表 */
fun_desc_t mine_table[] = {
    {mine_help, "?", "显示帮助菜单"},
    {mine_exit, "exit", "退出命令 shell"},
    {mine_pwd, "pwd", "显示当前工作目录路径"},
    {mine_changedir, "cd", "切换到目标目录"},
    {mine_wait, "wait", "等待子进程终止"},
};

/* 根据给定的文件路径查找并返回文件路径 */
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

/* 处理输入输出重定向 */
void IORe(char *FileName, IORe_t QPro)
{
  if (FileName == NULL)
    return;
  int current_id;
  switch (QPro)
  {
  case in:  // 输入重定向
    current_id = STDIN_FILENO;
    Aid = open(FileName, O_RDONLY);
    if (-1 == Aid)
      perror("open error!");
    Oid = dup(STDIN_FILENO);
    if (-1 == Oid)
      perror("dup error!");
    break;
  case out:  // 输出重定向
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
  dup2(Aid, current_id);  // 重定向
  close(Aid);  // 关闭文件描述符
}

/* 执行命令 */
int exe(unused struct tokens *argvs, bool backPro)
{
  int length = tokens_get_length(argvs);
  if (length <= 0)
  {
    printf("没有命令!\n ");
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
      QPro = in;  // 输入重定向
    else if (!strcmp(current_token, ">"))
      QPro = out;  // 输出重定向
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
  ProName = path_res(ProName);  // 查找可执行文件路径
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
  int res = execv(ProName, args);  // 执行命令
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

/* 等待子进程 */
int mine_wait(struct tokens *argvs)
{
  int sta;
  int pid = waitpid(Oid,&sta,WUNTRACED);
  while (pid)
  {
    if (pid == -1)
    {
      printf("等待出错!\n");
      break;
    }
    else
    {
      printf("子进程 #%d 已终止。\n", pid);
      break;
    }
  }
  return 0;
}

/* 打印命令的帮助信息 */
int mine_help(unused struct tokens *argvs)
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", mine_table[i].mine, mine_table[i].doc);
  return 1;
}

/* 打印当前工作目录 */
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

/* 改变目录 */
int mine_changedir(unused struct tokens *argvs)
{
  char *argument = tokens_get_token(argvs, 1);
  if (!argument){
    char *home_dir = getenv("HOME");
    if (home_dir)
      chdir(home_dir);  // 切换到 home 目录
    else
    {
      fprintf(stderr, "cd: HOME 没有设置\n");
      return -1;
    }
  }
  else
  {
    chdir(argument);  // 切换到指定目录
  }
  return 1;
}

/* 退出 shell */
int mine_exit(unused struct tokens *argvs) { exit(0); }

/* 查找内建命令 */
int lookup(char mine[])
{
  for (unsigned int i = 0; i < sizeof(mine_table) / sizeof(fun_desc_t); i++)
    if (mine && (strcmp(mine_table[i].mine, mine) == 0))
      return i;
  return -1;
}

/* 初始化 shell */
void init_shell()
{
  /* shell 连接到标准输入 */
  shell_terminal = STDIN_FILENO;

  /* 检查是否以交互方式运行 */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive)
  {
    /* 如果 shell 当前不是在前台运行，我们必须暂停 shell 直到它成为
     * 前台进程。使用 SIGTTIN 暂停 shell。当 shell 被移动到前台时，我们会收到 SIGCONT 信号 */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* 保存 shell 的进程 ID */
    shell_pgid = getpid();

    /* 控制终端 */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* 保存当前终端设置，以便以后恢复 */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[])
{
  init_shell();
  int sta;
  static char line[4096];
  int Lindex = 0;

  /* 仅当标准输入不是终端时才打印 shell 提示符 */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", Lindex);

  while (fgets(line, 4096, stdin))
  {
    /* 将输入行分割成单词 */
    struct tokens *argvs = tokenize(line);

    /* 查找要执行的内建函数 */
    int Findex = lookup(tokens_get_token(argvs, 0));
    size_t token_length = tokens_get_length(argvs);
    bool backPro = false;
    if (Findex >= 0)
    {
      mine_table[Findex].fun(argvs);  // 执行内建命令
    }
    else
    {
      if (!strcmp(tokens_get_token(argvs, token_length - 1), "&"))
        backPro = true;  // 检查是否后台执行
      /* 使用 fork 创建子进程执行外部命令 */
      pid_t childPid = fork();
      switch (childPid)
      {
      case -1:
        perror("fork 错误");
        exit(EXIT_FAILURE);
      case 0:
        exe(argvs, backPro);  // 子进程执行命令
        _exit(0);
      default:
      {
        Cpid = childPid;
        int status;
        setpgid(childPid, childPid);
        if(tcsetpgrp(shell_terminal, Cpid) == 0) {
            if ((waitpid(Cpid, &status, WUNTRACED)) < 0) {
              perror("wait 失败");
              _exit(2);
            }
            printf( "\n");
            signal(SIGTTOU, SIG_IGN);
            if(tcsetpgrp(shell_terminal, shell_pgid) != 0)
              printf("切换回 shell 发生错误!\n");
            signal(SIGTTOU, SIG_DFL);

          } else {
            printf("tcsetpgrp 错误发生\n");
          }
        break;
      }
        
      }

      signal(SIGTTOU, SIG_IGN);
      if (!backPro)
      {
       unused int ret = waitpid(-1,&sta,0);  // 等待子进程
        signal(SIGTTOU, SIG_DFL);
      }
    }

    if (shell_is_interactive)
      /* 仅当标准输入不是终端时才打印 shell 提示符 */
      fprintf(stdout, "%d: ", ++Lindex);

    /* 清理内存 */
    tokens_destroy(argvs);
  }

  return 0;
}
