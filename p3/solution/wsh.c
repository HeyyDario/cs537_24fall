/* ************************************************************************
> File Name:     wsh.c
> Author:        Chengtao Dai
> cs login:         chengtao
> Created Time:  Fri  9/27 21:21:19 2024
> Description:  See README.md
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_C_PER_LINE 1000
#define MAX_ARGS 1000

void parse(char *line, char **args);
void do_cmd(char **args);

int main(int argc, char *argv[])
{
    FILE *file = stdin;
    char line[MAX_C_PER_LINE];
    char *args[MAX_ARGS];

    // batch mode
    if (argc == 2)
    {
        file = fopen(argv[1], "r");
        if (!file)
        {
            perror("Failed to open file in batch mode.");
            exit(1);
        }

        while (fgets(line, MAX_C_PER_LINE, file))
        {
            parse(line, args);
            //printf("lines are: %s\n", line);

            if (args[0] != NULL) // Skip empty lines
            {
                if (strcmp(args[0], "exit") == 0) // Handle exit command
                {
                    break;
                }

                do_cmd(args);
            }
        }
        fclose(file);
    }
    else if (argc > 2)
    {
        printf("Usage: %s [batch_file]\n", argv[0]);
        exit(1);
    }
    else
    { // interactive mode
        while (1)
        {
            printf("wsh> ");

            // if no new line
            if (!fgets(line, MAX_C_PER_LINE, file))
            {
                break;
            }

            // replace new line with null terminator
            parse(line, args);

            if (args[0] == NULL)
            {
                continue;
            }

            // if exit
            if (strcmp(line, "exit") == 0)
            {
                break;
            }

            do_cmd(args);
        }
    }
    return 0;
}

void parse(char *line, char **args)
{
    line[strcspn(line, "\n")] = '\0';

    char *token = strtok(line, " ");
    int i = 0;
    
    for(i = 0; token != NULL; i++)
    {
        args[i] = token;
        token = strtok(NULL, " ");
    }

    args[i] = NULL;
}

void do_cmd(char **args)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        // Fork failed
        perror("Fork failed");
        exit(1);
    }
    else if (pid == 0)
    {
        // Child process
        char path[MAX_C_PER_LINE] = "/bin/";
        strcat(path, args[0]); // Assuming command is located in /bin/
        
        // Execute the command using execv
        if (execv(path, args) == -1)
        {
            perror("Execution failed");
        }
        exit(1);
    }
    else
    {
        // Parent process waits for the child to finish
        wait(NULL);
    }
}
