#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

typedef int (*builtin_func)(char **args, int arg_count);

#define BUILTIN_COMMAND_SUCCESS 0
#define BUILTIN_COMMAND_ERROR 1

enum built_in_command_type {
    BUILTIN_COMMAND_TYPE_CD = 1,
    BUILTIN_COMMAND_TYPE_EXIT,
    BUILTIN_COMMAND_TYPE_PWD,
    BUILTIN_COMMAND_TYPE_TRUE,
    BUILTIN_COMMAND_TYPE_FALSE,
    BUILTIN_COMMAND_TYPE_ECHO,
    BUILTIN_COMMAND_TYPE_COUNT,
    /*MUST BE THE LAST*/
    BUILTIN_COMMAND_TYPE_NONE = 0,
};

static int
builtin_cd(char **args, int arg_count)
{
    if (arg_count > 1) {
        fprintf(stderr, "cd: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    } 
    if (arg_count < 1) {
        fprintf(stderr, "cd: not enough arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }
    if (chdir(args[1]) != 0) {
        perror("cd");
        return BUILTIN_COMMAND_ERROR;
    }

    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_exit(char **args, int arg_count)
{
    (void)args;
    if (arg_count > 1) {
        fprintf(stderr, "exit: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }

    int exit_code = 0;
    if (arg_count == 0) {
        exit_code = BUILTIN_COMMAND_SUCCESS;
    } else {
        exit_code = atoi(args[1]);
    }

    exit(exit_code);

    assert(false);
}

static int
builtin_pwd(char **args, int arg_count)
{
    (void)args;
    if (arg_count > 1) {
        fprintf(stderr, "pwd: too many arguments\n");
        return BUILTIN_COMMAND_ERROR;
    }
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
        return BUILTIN_COMMAND_ERROR;
    }
    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_true(char **args, int arg_count)
{
    (void)args;
    (void)arg_count;
    return BUILTIN_COMMAND_SUCCESS;
}

static int
builtin_false(char **args, int arg_count)
{
    (void)args;
    (void)arg_count;
    return BUILTIN_COMMAND_ERROR;
}

static int
builtin_echo(char **args, int arg_count)
{
    if (arg_count == 0) {
        printf("\n");
        return BUILTIN_COMMAND_SUCCESS;
    }
    else {
        for (int i = 1; i <= arg_count; i++)
        {
            if (i > 1) {
                printf(" ");
            }
            printf("%s", args[i]);
        }
        printf("\n");
        return BUILTIN_COMMAND_SUCCESS;
    }
    return BUILTIN_COMMAND_ERROR;
}

/*builtin_table[0] = NULL; because enum start from 1*/
builtin_func builtin_table[] = {
    NULL,           
    builtin_cd,     
    builtin_exit,   
    builtin_pwd,    
    builtin_true,   
    builtin_false,  
    builtin_echo    
};

static enum built_in_command_type
is_builtin_command(const char *exe)
{
    if (strcmp(exe, "cd") == 0) {
        return BUILTIN_COMMAND_TYPE_CD;
    } 
    if (strcmp(exe, "exit") == 0) {
        return BUILTIN_COMMAND_TYPE_EXIT;
    } 
    if (strcmp(exe, "pwd") == 0) {
        return BUILTIN_COMMAND_TYPE_PWD;
    } 
    if (strcmp(exe, "true") == 0) {
        return BUILTIN_COMMAND_TYPE_TRUE;
    } 
    if (strcmp(exe, "false") == 0) {
        return BUILTIN_COMMAND_TYPE_FALSE;
    } 
    if (strcmp(exe, "echo") == 0) {
        return BUILTIN_COMMAND_TYPE_ECHO;
    }

    return BUILTIN_COMMAND_TYPE_NONE;
}

static void
execute_command_line(const struct command_line *line)
{
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */
    static int bg_command_count = 0;
    int out_file = STDOUT_FILENO;
    
	assert(line != NULL);

    bg_command_count += (int)line->is_background;

	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		//do nothing
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		out_file = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(out_file, STDOUT_FILENO);
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		out_file = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        dup2(out_file, STDOUT_FILENO);
	} else {
		assert(false);
	}
	//printf("Expressions:\n");
	const struct expr *e = line->head;
    enum built_in_command_type type = BUILTIN_COMMAND_TYPE_NONE;

    int has_prev = 0;
    int will_pipe = 0;

    int prev_pipe[2] = {0};

	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {

            int next_pipe[2] = {0};
            if(e->next){
                if (e->next->type == EXPR_TYPE_PIPE) will_pipe = 1;
            } 
            if (will_pipe) pipe(next_pipe);

            type = is_builtin_command(e->cmd.exe);
            if (type != BUILTIN_COMMAND_TYPE_NONE) {
                if (has_prev)  dup2(prev_pipe[0], STDIN_FILENO);
                if (will_pipe) dup2(next_pipe[1], STDOUT_FILENO);
                
                int res = builtin_table[type](e->cmd.args, e->cmd.arg_count);
                (void)res;
                
                if (will_pipe) close(next_pipe[1]);
                if (will_pipe) close(next_pipe[0]);
                
                if (has_prev) close(prev_pipe[1]);
                if (has_prev) close(prev_pipe[0]);
            } 
            else {
                pid_t pid = fork();
                if (pid == 0) {
                    if (has_prev)  dup2(prev_pipe[0], STDIN_FILENO);
                    if (will_pipe) dup2(next_pipe[1], STDOUT_FILENO);
                    
                    if (has_prev)  close(prev_pipe[1]);
                    if (has_prev)  close(prev_pipe[0]);
                    
                    if (will_pipe) close(next_pipe[1]);
                    if (will_pipe) close(next_pipe[0]);

                    execvp(e->cmd.exe, e->cmd.args);
                    
                }
            }
        if (has_prev) close(prev_pipe[0]);
        if (has_prev) close(prev_pipe[1]);

        if (will_pipe) {                     
            prev_pipe[0] = next_pipe[0];
            prev_pipe[1] = next_pipe[1];
            has_prev = 1;
		}                               

        } else if (e->type == EXPR_TYPE_PIPE) {
			//printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} else if (e->type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} else {
			assert(false);
		}
		e = e->next;
	}
    while(wait(NULL) > 0);
    if (out_file > 0) {
        close(out_file);
        out_file = 0;
    }
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
    

	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
