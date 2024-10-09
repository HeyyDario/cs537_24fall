#include "wsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>

#define MAX_LINE 1024 // Maximum length of the input line
#define MAX_ARGS 100  // Maximum number of arguments
#define DEFAULT_HISTORY_SIZE 5

// Struct Definitions
typedef struct shell_var
{
    char *name;
    char *value;
    struct shell_var *next;
} shell_var;

shell_var *shell_vars = NULL; // Head of the shell variables list

typedef struct {
    char **commands;
    int capacity;
    int count;
    int start;
    int end;
} history_t;

history_t history = {NULL, DEFAULT_HISTORY_SIZE, 0, 0, 0}; // Command history

int last_command_status = 0;  // Store the exit status of the last command

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
void exec_command_with_path(char *full_path, char *args[]);
void free_shell_variables();
void init_history();
void add_to_history(const char *cmd);
void execute_history_command(int n);
void resize_history(int new_size);
int is_builtin_command(const char *cmd);
void handle_cd_command(char *args[]);
void handle_ls_command();


int main(int argc, char *argv[])
{
    // Initialize the shell's PATH to contain only "/bin"
    setenv("PATH", "/bin", 1);
    init_history();

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

    return last_command_status;
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

void exec_command_with_path(char *full_path, char *args[]) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Fork failed");
        return;
    }

    if (pid == 0) {
        // Handle redirection here if necessary
        if (execv(full_path, args) == -1) {
            perror("Execution failed");
            exit(1);
        }
    } else {
        wait(NULL);  // Wait for the child process to complete
    }
}

// Initialize history with default size
void init_history() {
    history.commands = (char **)malloc(DEFAULT_HISTORY_SIZE * sizeof(char *));
    history.capacity = DEFAULT_HISTORY_SIZE;
}

// Add a command to the history
void add_to_history(const char *cmd) {
    if (is_builtin_command(cmd)) {
        return;  // Don't store built-in commands in history
    }

    // Prevent consecutive duplicate commands
    if (history.count > 0) {
        int last_index = (history.end - 1 + history.capacity) % history.capacity;
        if (strcmp(history.commands[last_index], cmd) == 0) {
            return;
        }
    }

    // If history is full, overwrite the oldest command
    if (history.count == history.capacity) {
        free(history.commands[history.start]);
        history.start = (history.start + 1) % history.capacity;
    } else {
        history.count++;
    }

    // Add the new command to the end of the circular buffer
    history.commands[history.end] = strdup(cmd);
    history.end = (history.end + 1) % history.capacity;
}

// Print the command history
void print_history() {
    int index = history.start;
    for (int i = 0; i < history.count; i++) {
        printf("%d) %s\n", i + 1, history.commands[index]);
        index = (index + 1) % history.capacity;
    }
}

// Execute the nth command from history
void execute_history_command(int n) {
    if (n <= 0 || n > history.count) {
        return;  // Invalid command number
    }

    int index = (history.start + n - 1) % history.capacity;
    printf("Executing: %s\n", history.commands[index]);

    // Simulate executing the command (this would normally call execute_command)
    execute_command(history.commands[index]);
}

// Check if a command is a built-in command
int is_builtin_command(const char *cmd) {
    return strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || strcmp(cmd, "ls") == 0 ||
           strcmp(cmd, "local") == 0 || strcmp(cmd, "export") == 0 || strcmp(cmd, "vars") == 0 ||
           strcmp(cmd, "history") == 0;
}

// Resize the history capacity
void resize_history(int new_size) {
    if (new_size <= 0) {
        return;  // Invalid size
    }

    // Allocate new history buffer
    char **new_commands = (char **)malloc(new_size * sizeof(char *));
    int new_count = 0;
    int new_end = 0;

    // Copy over as many commands as will fit into the new buffer
    int index = history.start;
    for (int i = 0; i < history.count && new_count < new_size; i++) {
        new_commands[new_end++] = history.commands[index];
        new_count++;
        index = (index + 1) % history.capacity;
    }

    // Free the old history
    free(history.commands);

    // Update history struct with new buffer
    history.commands = new_commands;
    history.capacity = new_size;
    history.count = new_count;
    history.start = 0;
    history.end = new_end;
}

// Implementation of built-in `cd`
void handle_cd_command(char *args[]) {
    if (args[1] == NULL || args[2] != NULL) {
        fprintf(stderr, "cd: wrong number of arguments\n");
        last_command_status = 1;
        return;
    }
    if (chdir(args[1]) != 0) {
        perror("cd failed");
        last_command_status = 1;
    } else {
        last_command_status = 0;
    }
}

// Implementation of built-in `ls`
void handle_ls_command() {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(".");
    if (!dir) {
        perror("ls");
        last_command_status = 1;
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') { // Ignore hidden files
            printf("%s\n", entry->d_name);
        }
    }

    closedir(dir);
    last_command_status = 0;
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

    // Add command to history (except for built-in commands)
    if (!is_builtin_command(trimmed_cmd)) {
        add_to_history(trimmed_cmd);
    }

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

    if (strcmp(args[0], "cd") == 0) 
    {
        handle_cd_command(args); // Built-in cd command
        return;
    }

    if (strcmp(args[0], "ls") == 0) 
    {
        handle_ls_command(); // Built-in ls command
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

    if (strcmp(args[0], "history") == 0) {
        if (args[1] == NULL) {
            print_history();
        } else if (strcmp(args[1], "set") == 0 && args[2] != NULL) {
            resize_history(atoi(args[2]));
        } else {
            execute_history_command(atoi(args[1]));
        }
        return;
    }

    // Fork a child process
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("Fork failed");
        last_command_status = 1;
        return;
    }

    if (pid == 0)
    {
        // Child process: handle redirection if necessary
        if (redirect_type > 0 && filename != NULL)
        {
            handle_redirection(redirect_type, filename);
        }

        if (strchr(args[0], '/') != NULL) 
        {
            exec_command_with_path(args[0], args);
        } else {
            char *path_env = getenv("PATH");
            char *path_copy = strdup(path_env);  // Duplicate the PATH string
            char *path_token = strtok(path_copy, ":");

            while (path_token != NULL) {
                char full_path[MAX_LINE];
                snprintf(full_path, sizeof(full_path), "%s/%s", path_token, args[0]);

                if (access(full_path, X_OK) == 0) {
                    exec_command_with_path(full_path, args);  // Found the executable, run it
                    free(path_copy);  // Free memory before returning
                    exit(0);  // Child process exits after successful execution
                }

                path_token = strtok(NULL, ":");
            }

            // If the command wasn't found in $PATH
            fprintf(stderr, "%s: command not found\n", args[0]);
            free(path_copy);
            exit(127);  // Exit child process
        }

        // Execute the command using execv (requires full path)
        // if (execv(args[0], args) == -1)
        // {
        //     perror("Execution failed");
        //     exit(1); // Exit child process if execution fails
        // }
    }
    else
    {
        // Parent process: wait for the child to finish and capture the exit status
        int status;
        wait(&status);

        if (WIFEXITED(status)) {
            last_command_status = WEXITSTATUS(status);
        } else {
            last_command_status = 1;  // Non-zero on error
        }
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
