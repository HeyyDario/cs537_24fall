#include "wsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>

#define MAX_LINE 1024 // Maximum length of the input line
#define MAX_ARGS 100  // Maximum number of arguments

// Struct Definitions
typedef struct shell_var
{
    char *name;
    char *value;
    struct shell_var *next;
} shell_var;

shell_var *shell_vars = NULL; // Head of the shell variables list

// Function prototypes
void interactive_mode();
void batch_mode(char *filename);
void execute_command(char *cmd);
int is_comment(char *line);
void handle_redirection(int redirect_type, char *filename);
void set_local_variable(const char *name, const char *value);
const char *get_local_variable(const char *name);
void handle_vars_command();
void handle_local_command(char *args[]);
void handle_export_command(char *args[]);
void substitute_variables(char *cmd);
void free_shell_variables();

int main(int argc, char *argv[])
{
    // Check arguments
    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(1);
    }

    if (argc == 1)
    {
        // Interactive mode
        interactive_mode();
    }
    else
    {
        // Batch mode
        batch_mode(argv[1]);
    }

    free_shell_variables();

    return 0;
}

void interactive_mode()
{
    char line[MAX_LINE];

    while (1)
    {
        printf("wsh> ");
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            break; // End of input (Ctrl+D)
        }

        // Remove newline character
        line[strcspn(line, "\n")] = 0;

        // Exit if the user types "exit"
        if (strcmp(line, "exit") == 0)
        {
            break;
        }

        // Ignore comments
        if (is_comment(line))
        {
            continue;
        }

        // Execute the command
        execute_command(line);
    }
}

void batch_mode(char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening batch file");
        exit(1);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        // Remove newline character
        line[strcspn(line, "\n")] = 0;

        // Ignore comments
        if (is_comment(line))
        {
            continue;
        }

        // Execute the command
        execute_command(line);
    }

    fclose(file);
}

// Check if the line is a comment
int is_comment(char *line)
{
    // Find the first occurrence of '#' in the line
    char *comment_pos = strchr(line, '#');

    // If '#' is found, make sure all characters before it are spaces
    if (comment_pos != NULL)
    {
        for (char *p = line; p < comment_pos; p++)
        {
            if (!isspace(*p))
            {
                return 0; // Not a comment, some non-space characters found before '#'
            }
        }
        return 1; // All characters before '#' are spaces, it's a comment
    }

    return 0; // No '#' found, not a comment
}

// Function to handle redirection
void handle_redirection(int redirect_type, char *filename)
{
    int fd;

    if (redirect_type == 1)
    {
        // Output redirection
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout to file
        close(fd);
    }
    else if (redirect_type == 2)
    {
        // Input redirection
        fd = open(filename, O_RDONLY);
        if (fd == -1)
        {
            perror("Failed to open input file");
            exit(1);
        }
        dup2(fd, STDIN_FILENO); // Redirect stdin to file
        close(fd);
    }
    else if (redirect_type == 3)
    {
        // Append output redirection
        fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout to file
        close(fd);
    }
    else if (redirect_type == 4)
    {
        // Redirect stdout and stderr to the same file
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout
        dup2(fd, STDERR_FILENO); // Redirect stderr
        close(fd);
    }
    else if (redirect_type == 5)
    {
        // Append stdout and stderr to the same file
        fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout
        dup2(fd, STDERR_FILENO); // Redirect stderr
        close(fd);
    }
}

// Function to set or update a local shell variable
void set_local_variable(const char *name, const char *value)
{
    shell_var *current = shell_vars;

    // Check if the variable already exists
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            // Update the value of the existing variable
            free(current->value);
            current->value = strdup(value);
            return;
        }
        current = current->next;
    }

    // Otherwise, add a new variable to the list
    shell_var *new_var = (shell_var *)malloc(sizeof(shell_var));
    new_var->name = strdup(name);
    new_var->value = strdup(value);
    new_var->next = shell_vars;
    shell_vars = new_var;
}

// Function to get the value of a local shell variable
const char *get_local_variable(const char *name)
{
    shell_var *current = shell_vars;
    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            return current->value;
        }
        current = current->next;
    }
    return NULL; // Variable not found
}

// Function to display all local shell variables
void handle_vars_command()
{
    shell_var *current = shell_vars;
    while (current != NULL)
    {
        printf("%s=%s\n", current->name, current->value);
        current = current->next;
    }
}

// Function to handle "local" command
void handle_local_command(char *args[])
{
    if (args[1] == NULL || strchr(args[1], '=') == NULL)
    {
        fprintf(stderr, "Error: Invalid local variable assignment.\n");
        return;
    }

    // Extract the variable name and value from args[1]
    char *equal_sign = strchr(args[1], '=');
    *equal_sign = '\0'; // Terminate the name string
    const char *varname = args[1];
    const char *value = equal_sign + 1;

    if (varname[0] == '$')
    {
        fprintf(stderr, "Error: Variable name cannot start with $.\n");
        return;
    }

    // Set the local variable using the extracted name and value
    set_local_variable(varname, value);
}

// Function to handle "export" command
void handle_export_command(char *args[])
{
    if (args[1] == NULL || strchr(args[1], '=') == NULL)
    {
        fprintf(stderr, "Error: Invalid environment variable assignment.\n");
        return;
    }

    // Extract the variable name and value from args[1]
    char *equal_sign = strchr(args[1], '=');
    *equal_sign = '\0'; // Terminate the name string
    const char *varname = args[1];
    const char *value = equal_sign + 1;

    // Set the environment variable using the extracted name and value
    if (setenv(varname, value, 1) == -1)
    {
        perror("setenv");
    }
}

// Function to substitute variables in a command
void substitute_variables(char *cmd)
{
    char buffer[MAX_LINE] = {0}; // Temporary buffer for the modified command
    char *output = buffer;
    char *input = cmd;

    while (*input)
    {
        if (*input == '$')
        {
            input++; // Skip the '$'

            // Dynamically allocate memory for the variable name
            size_t varname_len = 0;
            const char *start = input;

            // Calculate the length of the variable name
            while (*input && isalnum(*input))
            {
                varname_len++;
                input++;
            }

            // Allocate memory dynamically for the variable name
            char *varname = malloc(varname_len + 1); // +1 for null terminator
            if (!varname)
            {
                perror("malloc");
                exit(1);
            }

            strncpy(varname, start, varname_len); // Copy the variable name
            varname[varname_len] = '\0';          // Null-terminate the string

            // Check environment variables first
            const char *value = getenv(varname);
            if (!value)
            {
                // Check local shell variables
                value = get_local_variable(varname);
            }

            if (value)
            {
                // Append the variable's value to the output
                strcpy(output, value);
                output += strlen(value);
            }

            free(varname); // Free dynamically allocated memory
        }
        else
        {
            // Copy regular characters
            *output++ = *input++;
        }
    }

    // Copy the modified command back into the original buffer
    strcpy(cmd, buffer);
}

void execute_command(char *cmd)
{
    char *args[MAX_ARGS];
    int i = 0;
    int redirect_type = 0;
    char *filename = NULL;

    // Tokenize the command string, considering '#' as a delimiter
    char *token = strtok(cmd, "#"); // Split at the comment
    if (token != NULL)
    {
        cmd = token; // Use the part before the '#'
    }

    substitute_variables(cmd);

    // Trim leading and trailing whitespace after substitution
    char *trimmed_cmd = cmd;
    while (isspace((unsigned char)*trimmed_cmd))
        trimmed_cmd++;
    if (*trimmed_cmd == '\0')
        return; // Ignore empty commands

    // Tokenize the command into arguments
    token = strtok(trimmed_cmd, " ");
    while (token != NULL && i < MAX_ARGS - 1)
    {
        // Check for redirection operators
        if (strcmp(token, ">") == 0)
        {
            redirect_type = 1; // Output redirection
            token = strtok(NULL, " ");
            filename = token; // The next token is the filename
            break;
        }
        else if (strcmp(token, ">>") == 0)
        {
            redirect_type = 3; // Append redirection
            token = strtok(NULL, " ");
            filename = token; // The next token is the filename
            break;
        }
        else if (strcmp(token, "<") == 0)
        {
            redirect_type = 2; // Input redirection
            token = strtok(NULL, " ");
            filename = token; // The next token is the filename
            break;
        }
        else if (strcmp(token, "&>") == 0)
        {
            redirect_type = 4; // Redirect both stdout and stderr
            token = strtok(NULL, " ");
            filename = token; // The next token is the filename
            break;
        }
        else if (strcmp(token, "&>>") == 0)
        {
            redirect_type = 5; // Append both stdout and stderr
            token = strtok(NULL, " ");
            filename = token; // The next token is the filename
            break;
        }
        else
        {
            args[i++] = token; // Regular command/argument
        }
        token = strtok(NULL, " ");
    }
    args[i] = NULL; // Null-terminate the argument list

    if (i == 0)
    {
        // No command, return without doing anything
        return;
    }

    if (strcmp(args[0], "local") == 0)
    {
        handle_local_command(args); // Pass args array to local handler
        return;
    }

    if (strcmp(args[0], "export") == 0)
    {
        handle_export_command(args); // Pass args array to export handler
        return;
    }

    if (strcmp(args[0], "vars") == 0)
    {
        handle_vars_command(); // vars has no arguments, call directly
        return;
    }

    // Fork a child process
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("Fork failed");
        return;
    }

    if (pid == 0)
    {
        // Child process: handle redirection if necessary
        if (redirect_type > 0 && filename != NULL)
        {
            handle_redirection(redirect_type, filename);
        }

        // Execute the command using execv (requires full path)
        if (execv(args[0], args) == -1)
        {
            perror("Execution failed");
            exit(1); // Exit child process if execution fails
        }
    }
    else
    {
        // Parent process: wait for the child to finish
        wait(NULL);
    }
}

// Free all shell variables
void free_shell_variables()
{
    shell_var *current = shell_vars;
    while (current != NULL)
    {
        shell_var *next = current->next;
        free(current->name);
        free(current->value);
        free(current);
        current = next;
    }
}
