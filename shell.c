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

/* 全局变量，用于保存旧的文件描述符和新打开的文件描述符 */
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

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_wait(struct tokens *argvs);
int cmd_changedir(struct tokens *argvs);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

/* 子进程的参数解析结构体 */
struct ch_process {
    int tokens_len;      // Total number of tokens
    int next_token;      // Current token index
    char **args;         // Arguments list
    int in_fd;           // Input file descriptor (default 0: stdin)
    int out_fd;          // Output file descriptor (default 1: stdout)
    int out_attr;        // File open attributes for output
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

int cmd_wait(struct tokens *argvs) {
  int sta;
  int pid = waitpid(Oid, &sta, WUNTRACED);
  while (pid) {
    if (pid == -1) {
      printf("wait error!\n");
      break;
    } else {
      printf("子进程 #%d 已终止。\n", pid);
      break;
    }
  }
  return 0;
}

int cmd_changedir(unused struct tokens *argvs) {
  char *argument = tokens_get_token(argvs, 1);
  if (!argument) {
    char *home_dir = getenv("HOME");
    if (home_dir) chdir(home_dir);
    else {
      fprintf(stderr, "cd: HOME 未设置\n");
      return -1;
    }
  } else {
    chdir(argument);
  }
  return 1;
}

/* start a child process to execute program */
int run_program(struct tokens *tokens) {
    int tokens_len = tokens_get_length(tokens);
    if (tokens_len == 0) return 0;  // 无输入直接返回

    char *args[tokens_len + 1];  // 参数数组，多预留一个 NULL
    struct ch_process child = { 0 };
    child.tokens_len = tokens_len;
    child.next_token = 0;
    child.args = args;
    child.in_fd = 0;  // 默认 stdin
    child.out_fd = 1; // 默认 stdout
    child.out_attr = O_TRUNC;  // 默认覆盖输出

    // 解析参数
    parse_args(&child, tokens);

    // 解析重定向
    parse_redirection(&child, tokens);

    // 创建子进程
    pid_t chpid = fork();
    if (chpid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return -1;
    } else if (chpid == 0) {
        // 子进程：执行重定向
        if (child.in_fd != 0) dup2(child.in_fd, 0);  // 重定向 stdin
        if (child.out_fd != 1) dup2(child.out_fd, 1);  // 重定向 stdout

        // 执行命令
        execvp(args[0], args);
        fprintf(stderr, "execvp: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 父进程：等待子进程完成
    if (wait(NULL) == -1) {
        fprintf(stderr, "wait: %s\n", strerror(errno));
        return -1;
    }

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

void parse_args(struct ch_process *ch, struct tokens *tokens) {
    char *token;
    int finish = 0;

    while (ch->next_token < ch->tokens_len && !finish) {
        token = tokens_get_token(tokens, ch->next_token);
        finish = (token[0] == '<' || token[0] == '>');  // 遇到 < 或 > 停止解析
        ch->args[ch->next_token] = (char *)((!finish) * (int64_t)(void *)(token));
        ch->next_token += !finish;
    }
    ch->args[ch->next_token] = NULL;  // 参数数组以 NULL 结尾
}

void parse_redirection(struct ch_process *ch, struct tokens *tokens) {
    char *arrow, *path;

    while (ch->next_token < ch->tokens_len) {
        arrow = tokens_get_token(tokens, ch->next_token++);  // 获取 <, >, 或 >>
        if (ch->next_token >= ch->tokens_len) {
            fprintf(stderr, "No file next to '%s'\n", arrow);
            return;
        }
        path = tokens_get_token(tokens, ch->next_token++);  // 获取文件路径

        switch (arrow[0]) {
            case '<':  // 输入重定向
                if (access(path, R_OK) == 0) {
                    if (ch->in_fd != 0) close(ch->in_fd);
                    ch->in_fd = open(path, O_RDONLY);
                } else {
                    fprintf(stderr, "%s is not exist or readable\n", path);
                    return;
                }
                break;

            case '>':  // 输出重定向
                ch->out_attr = O_WRONLY | O_CREAT;
                if (arrow[1] == '>') {
                    ch->out_attr |= O_APPEND;  // 追加模式
                } else {
                    ch->out_attr |= O_TRUNC;  // 覆盖模式
                }
                if (ch->out_fd != 1) close(ch->out_fd);
                ch->out_fd = open(path, ch->out_attr, 0664);  // -rw-rw-r--
                break;
        }
    }
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
    // 初始化 shell 设置
    init_shell();

    static char line[4096];  // 存储输入的命令行
    int line_num = 0;        // 命令计数器
    int sta;

    // 仅在交互模式下打印 shell 提示符
    if (shell_is_interactive) {
        fprintf(stdout, "%d: ", line_num);
    }

    // 主循环：读取命令并执行
    while (fgets(line, sizeof(line), stdin)) {
        // 将输入行进行分词
        struct tokens* argvs = tokenize(line);

        // 查找并执行对应的内置命令
        int Findex = lookup(tokens_get_token(argvs, 0));
        size_t token_length = tokens_get_length(argvs);
        bool backPro = false;  // 是否是后台进程

        if (Findex >= 0) {
            // 如果是内建命令，则执行相应函数
            cmd_table[Findex].fun(argvs);
        } else {
            // 判断是否是后台命令（以 & 结尾）
            if (!strcmp(tokens_get_token(argvs, token_length - 1), "&"))
                backPro = true;

            // 执行外部命令
            pid_t childPid = fork();
            switch (childPid) {
                case -1:  // fork 错误
                    perror("fork error");
                    exit(EXIT_FAILURE);
                case 0:  // 子进程
                    exe(argvs, backPro);
                    _exit(0);  // 如果 exec 出错，退出子进程
                default: {  // 父进程
                    Cpid = childPid;  // 保存子进程 ID
                    int status;
                    setpgid(childPid, childPid);  // 设置子进程的进程组 ID
                    if (tcsetpgrp(shell_terminal, Cpid) == 0) {  // 将子进程设置为前台进程
                        if ((waitpid(Cpid, &status, WUNTRACED)) < 0) {
                            perror("wait failed");
                            _exit(2);
                        }
                        printf("\n");
                        // 恢复 shell 的控制权
                        signal(SIGTTOU, SIG_IGN);  // 忽略 SIGTTOU 信号
                        if (tcsetpgrp(shell_terminal, shell_pgid) != 0)
                            printf("切换到 shell 发生错误！\n");
                        signal(SIGTTOU, SIG_DFL);  // 恢复默认的 SIGTTOU 处理
                    } else {
                        printf("tcsetpgrp 错误！\n");
                    }
                    break;
                }
            }

            // 如果不是后台进程，等待子进程完成
            if (!backPro) {
                unused int ret = waitpid(-1, &sta, 0);
            }
        }

        // 显示 shell 提示符
        if (shell_is_interactive)
            fprintf(stdout, "%d: ", ++line_num);

        // 清理内存
        tokens_destroy(argvs);
    }

    return 0;  // 退出 shell
}

