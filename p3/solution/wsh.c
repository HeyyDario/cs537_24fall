#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>  // For open(), O_CREAT, O_WRONLY, O_RDONLY
#include <ctype.h>
#include "wsh.h"

// Error message for any kind of invalid operation
const char *error_message = "An error has occurred\n";

#define MAX_PATH_LENGTH 1024

// Global shell variables array
ShellVar shell_vars[MAX_VARS];
int num_vars = 0;

// Global history object
history_t history = {NULL, DEFAULT_HISTORY_SIZE, 0, 0, 0};

// Initialize last command status
int last_command_status = 0;

// Function to print a specific error message
void print_error(const char *message) {
    fprintf(stderr, "%s\n", message);
}

// Helper function to check if a line is a comment (starts with # or spaces followed by #)
int is_comment(char *line) {
    // Trim leading spaces
    while (*line == ' ') {
        line++;
    }
    // Check if the line starts with '#'
    return (*line == '#');
}

// Function to search for the executable in directories listed in PATH
int find_command_in_path(const char *command, char *full_path) {
    char *path_env = getenv("PATH");
    char *path = strdup(path_env);  // Duplicate the PATH string for manipulation
    char *saveptr; // For strtok_r
    char *dir = strtok_r(path, ":", &saveptr);  // Split PATH by colon

    // Try each directory in PATH
    while (dir != NULL) {
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", dir, command);
        if (access(full_path, X_OK) == 0) {
            free(path);
            return 1;  // Found the command and it's executable
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path);
    return 0;  // Command not found in any PATH directory
}

// Function to run in interactive mode
void interactive_mode() {
    char command[MAX_COMMAND_LENGTH];

    while (1) {
        // Print the prompt
        printf("wsh> ");
        fflush(stdout);

        // Get user input
        if (fgets(command, MAX_COMMAND_LENGTH, stdin) == NULL) {
            // fgets returns NULL on EOF, break the loop to exit
            break;
        }

        // Remove the newline character from the command
        command[strcspn(command, "\n")] = 0;

        // Ignore lines that are comments or empty
        if (is_comment(command) || strlen(command) == 0) {
            continue;
        }

        // Check if the command is 'exit'
        if (strcmp(command, "exit") == 0) {
            break;
        }

        // Execute the command
        execute_command(command);
    }
}

// Function to run in batch mode
void batch_mode(const char *batch_file) {
    FILE *file = fopen(batch_file, "r");
    if (file == NULL) {
        print_error("Error opening batch file");
        exit(1);
    }

    char command[MAX_COMMAND_LENGTH];
    while (fgets(command, MAX_COMMAND_LENGTH, file) != NULL) {
        // Remove the newline character from the command
        command[strcspn(command, "\n")] = 0;

        // Ignore lines that are comments or empty
        if (is_comment(command) || strlen(command) == 0) {
            continue;
        }

        // Check if the command is 'exit'
        if (strcmp(command, "exit") == 0) {
            break;
        }

        // Execute the command
        execute_command(command);
    }

    fclose(file);
}

// Function to handle redirection
void handle_redirection(int redirect_type, char *filename)
{
    int fd;

    if (redirect_type == 1) // Output redirection
    {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout to file
        close(fd);
    }
    else if (redirect_type == 2) // Input redirection
    {
        fd = open(filename, O_RDONLY);
        if (fd == -1)
        {
            perror("Failed to open input file");
            exit(1);
        }
        dup2(fd, STDIN_FILENO); // Redirect stdin to file
        close(fd);
    }
    else if (redirect_type == 3) // Append output redirection
    {
        fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1)
        {
            perror("Failed to open output file");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO); // Redirect stdout to file
        close(fd);
    }
    else if (redirect_type == 4) // Redirect stdout and stderr to the same file
    {
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
    else if (redirect_type == 5) // Append stdout and stderr to the same file
    {
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
    else if (redirect_type == 6) // Redirect stderr only
    {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            perror("Failed to open error file");
            exit(1);
        }
        dup2(fd, STDERR_FILENO); // Redirect stderr to file
        close(fd);
    }
}

// Function to parse redirection in a command string
void parse_redirection(char *command, int *redirect_type, char **filename) {
    // Check for redirection symbols in the command
    char *redir_pos = NULL;

    if (strstr(command, "2>")) {
        *redirect_type = 6; // Redirect stderr only
        *filename = strstr(command, "2>") + 2; // Filename follows 2>
        *strstr(command, "2>") = '\0'; // Truncate command
    } 
    else if ((redir_pos = strstr(command, "&>>"))) {
        *redirect_type = 5; // Append both stdout and stderr
        *filename = redir_pos + 3; // Filename follows &>>
        *redir_pos = '\0'; // Truncate command
    }
    else if ((redir_pos = strstr(command, "&>"))) {
        *redirect_type = 4; // Redirect both stdout and stderr
        *filename = redir_pos + 2; // Filename follows &>
        *redir_pos = '\0'; // Truncate command
    }
    else if ((redir_pos = strstr(command, ">>"))) {
        *redirect_type = 3; // Append output redirection
        *filename = redir_pos + 2; // Filename follows >>
        *redir_pos = '\0'; // Truncate command
    }
    else if ((redir_pos = strstr(command, ">"))) {
        *redirect_type = 1; // Output redirection
        *filename = redir_pos + 1; // Filename follows >
        *redir_pos = '\0'; // Truncate command
    }
    else if ((redir_pos = strstr(command, "<"))) {
        *redirect_type = 2; // Input redirection
        *filename = redir_pos + 1; // Filename follows <
        *redir_pos = '\0'; // Truncate command
    }

    // Trim any leading spaces in the filename
    if (filename && *filename) {
        while (**filename == ' ') {
            (*filename)++;
        }
    }
}

// Function to get a shell variable value
const char* get_shell_var(const char* varname) {
    for (int i = 0; i < num_vars; i++) {
        if (strcmp(shell_vars[i].name, varname) == 0) {
            return shell_vars[i].value;
        }
    }
    return "";  // If variable doesn't exist, return empty string
}

// Function to set a shell variable
void set_shell_var(const char* varname, const char* value) {
    for (int i = 0; i < num_vars; i++) {
        if (strcmp(shell_vars[i].name, varname) == 0) {
            // Update existing variable
            strcpy(shell_vars[i].value, value);
            return;
        }
    }
    // Add new variable
    if (num_vars < MAX_VARS) {
        strcpy(shell_vars[num_vars].name, varname);
        strcpy(shell_vars[num_vars].value, value);
        num_vars++;
    } else {
        fprintf(stderr, "Error: Maximum number of shell variables reached.\n");
    }
}

void expand_variables(char *command) {
    char expanded_command[MAX_COMMAND_LENGTH];
    char *read_ptr = command;
    char *write_ptr = expanded_command;
    while (*read_ptr) {
        if (*read_ptr == '$') {
            read_ptr++;
            char varname[MAX_VAR_LENGTH];
            char *var_ptr = varname;

            // Collect valid variable name characters
            if (isalpha(*read_ptr) || *read_ptr == '_') {
                while (*read_ptr && (isalnum(*read_ptr) || *read_ptr == '_')) {
                    *var_ptr++ = *read_ptr++;
                }
                *var_ptr = '\0';

                // Lookup variable value
                const char* var_value = getenv(varname);
                if (var_value == NULL) {
                    var_value = get_shell_var(varname);
                }

                // Replace with variable value
                if (var_value) {
                    while (*var_value) {
                        *write_ptr++ = *var_value++;
                    }
                }
            } else {
                // If not a valid variable name, treat '$' as a literal
                *write_ptr++ = '$';
            }
        } else {
            *write_ptr++ = *read_ptr++;
        }
    }
    *write_ptr = '\0';
    strcpy(command, expanded_command);  // Replace the original command
}

// Built-in local command for setting shell variables
void handle_local_command(char* command) {
    // Find the '=' character to split the variable name and value
    char *equal_sign = strchr(command, '=');

    if (equal_sign == NULL) {
        // No '=' found, invalid assignment
        fprintf(stderr, "Error: Invalid local variable assignment\n");
        last_command_status = 1;
        return;
    }

    // Temporarily null-terminate at '=' to extract the variable name
    *equal_sign = '\0';
    char *varname = command;
    char *value = equal_sign + 1;

    // Check if the variable name starts with a '$' or contains invalid characters
    if (varname[0] == '$') {
        fprintf(stderr, "Error: Invalid variable name starting with $\n");
        last_command_status = 1;
        return;
    }

    // Check if the variable name contains only alphanumeric characters and underscores
    for (int i = 0; varname[i] != '\0'; i++) {
        if (!isalnum(varname[i]) && varname[i] != '_') {
            fprintf(stderr, "Error: Invalid variable name\n");
            last_command_status = 1;
            return;
        }
    }

    // If variable name is valid, set the shell variable
    if (varname != NULL && value != NULL) {
        // Expand variables in value if necessary (e.g., local a=$b)
        expand_variables(value);
        set_shell_var(varname, value);
        last_command_status = 0;
    } else if (varname != NULL && value == NULL) {
        set_shell_var(varname, "");  // Clear the variable if no value is given
        last_command_status = 1;
    }
}

// Function to handle export command
void handle_export_command(char *command) {
    char *varname = strtok(command, "=");
    char *value = strtok(NULL, "=");

    if (varname != NULL && value != NULL) {
        // Set environment variable
        setenv(varname, value, 1);  // 1 means overwrite existing value
        last_command_status = 0;
    } else if (varname != NULL && value == NULL) {
        // Error: export without a value
        fprintf(stderr, "Error: export without value is not allowed\n");
        last_command_status = 1;
    }
}

// Function to handle the vars command to display shell variables
void handle_vars_command() {
    for (int i = 0; i < num_vars; i++) {
        printf("%s=%s\n", shell_vars[i].name, shell_vars[i].value);
    }
    last_command_status = 0;
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
        history.commands[history.start] = strdup(cmd);
        history.start = (history.start + 1) % history.capacity;
        history.end = (history.end + 1) % history.capacity;
    } else {
        // Add the new command to the end of the circular buffer
        history.commands[history.end] = strdup(cmd);
        history.end = (history.end + 1) % history.capacity;
        history.count++;
    }
}

// Print the command history
void print_history() {
    int index = history.start;
    for (int i = 0; i < history.count; i++) {
        printf("%d) %s\n", i + 1, history.commands[index]);
        index = (index + 1) % history.capacity;
    }
    last_command_status = 0;
}

// Execute the nth command from history
void execute_history_command(int n) {
    if (n <= 0 || n > history.count) {
        last_command_status = 1;
        return;  // Invalid command number
    }

    int index = (history.start + n - 1) % history.capacity;

    // Execute the command without adding it to history
    execute_command(history.commands[index]);
}

// Check if a command is a built-in command
int is_builtin_command(const char *cmd) {
    // Extract the first token to compare
    char cmd_copy[MAX_COMMAND_LENGTH];
    // Use snprintf instead of strncpy
    snprintf(cmd_copy, sizeof(cmd_copy), "%s", cmd);
    char *token = strtok(cmd_copy, " ");

    if (token == NULL) {
        return 0;
    }

    return strcmp(token, "exit") == 0 || strcmp(token, "cd") == 0 || strcmp(token, "ls") == 0 ||
           strcmp(token, "local") == 0 || strcmp(token, "export") == 0 || strcmp(token, "vars") == 0 ||
           strcmp(token, "history") == 0;
}

// Resize the history capacity
void resize_history(int new_size) {
    if (new_size <= 0) {
        last_command_status = 1;
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

    // Free the remaining commands that don't fit
    for (int i = new_count; i < history.count; i++) {
        free(history.commands[index]);
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
    last_command_status = 0;
}

// Implementation of built-in cd
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

// Compare function for qsort (alphabetical order)
int compare_filenames(const void *a, const void *b) {
    const char *file_a = *(const char **)a;
    const char *file_b = *(const char **)b;
    return strcmp(file_a, file_b);
}

void handle_ls_command() {
    DIR *dir;
    struct dirent *entry;
    char *filenames[1024]; // Array to store file names (adjust size as needed)
    int count = 0;

    dir = opendir(".");
    if (!dir) {
        perror("ls");
        last_command_status = 1;
        return;
    }

    // Collect filenames from the directory
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') { // Ignore hidden files (starting with .)
            filenames[count] = strdup(entry->d_name); // Duplicate the name for sorting
            count++;
        }
    }
    closedir(dir);

    // Sort the filenames alphabetically
    qsort(filenames, count, sizeof(char *), compare_filenames);

    // Print the sorted filenames
    for (int i = 0; i < count; i++) {
        printf("%s\n", filenames[i]);
        free(filenames[i]); // Free the duplicated name after printing
    }
    last_command_status = 0;
}

// Function to execute a command using execv and handle redirection
void execute_command(char *command) {
    // Add command to history (except for built-in commands)
    if (!is_builtin_command(command)) {
        add_to_history(command);
    }

    char *args[MAX_ARGS];
    int i = 0;
    char *token;
    int redirect_type = 0;
    char *filename = NULL;

    // Find the comment delimiter '#' and truncate the command at that point
    char *comment_pos = strchr(command, '#');
    if (comment_pos != NULL) {
        *comment_pos = '\0';  // Replace '#' with null terminator to truncate
    }

    // Parse redirection operators and filenames
    parse_redirection(command, &redirect_type, &filename);

    // Parse and expand variables
    expand_variables(command);

    // Tokenize the command into arguments
    token = strtok(command, " ");
    while (token != NULL && i < MAX_ARGS - 1)
    {
        args[i++] = token; // Regular command/argument
        token = strtok(NULL, " ");
    }
    args[i] = NULL; // Null-terminate the argument list

    if (i == 0)
    {
        // No command, return without doing anything
        last_command_status = 0;
        return;
    }

    // Check if this is the exit command
    if (strcmp(args[0], "exit") == 0) {
        if (args[1] != NULL) {
            // Error: exit should not take any arguments
            fprintf(stderr, "Error: exit does not take any arguments\n");
            last_command_status = 255; // Set to 255 as per test expectation
        } else {
            // Exit with the status of the last command
            exit(last_command_status);
        }
        return;
    }

    // Check if this is a local shell variable assignment
    if (strcmp(args[0], "local") == 0 && args[1] != NULL) {
        handle_local_command(args[1]);  // Call the local handler
        return;
    }

    // Check if this is an export command
    if (strcmp(args[0], "export") == 0 && args[1] != NULL) {
        handle_export_command(args[1]);
        return;
    }

    // Check if this is the vars command
    if (strcmp(args[0], "vars") == 0) {
        handle_vars_command();
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

    // Full or relative path (contains /)
    if (strchr(args[0], '/') != NULL) {
        if (access(args[0], X_OK) == 0) {
            // Fork to execute the command
            pid_t pid = fork();
            if (pid < 0) {
                print_error("Fork failed");
                last_command_status = 1;
                return;
            } else if (pid == 0) {
                // Child process: execute the command
                if (redirect_type > 0 && filename != NULL)
                {
                    handle_redirection(redirect_type, filename);
                }
                execv(args[0], args);
                // If execv returns, there was an error
                fprintf(stderr, "Command execution failed\n");
                exit(1);
            } else {
                // Parent process: wait for the child to finish
                int status;
                waitpid(pid, &status, 0);

                if (WIFEXITED(status)) {
                    last_command_status = WEXITSTATUS(status);
                } else {
                    last_command_status = 1;
                }
            }
        } else {
            last_command_status = 255; // Command not found
        }
        return;
    }

    // Construct the full path for the command
    char full_path[MAX_PATH_LENGTH];
    if (!find_command_in_path(args[0], full_path)) {
        last_command_status = 255; // Command not found
        return;
    }

    // Fork and execute the command
    pid_t pid = fork();
    if (pid < 0) {
        print_error("Fork failed");
        last_command_status = 1;
        return;
    } else if (pid == 0) {
        // Child process: execute the command
        if (redirect_type > 0 && filename != NULL)
        {
            handle_redirection(redirect_type, filename);
        }
        execv(full_path, args);
        // If execv returns, there was an error
        fprintf(stderr, "Command execution failed\n");
        exit(1);
    } else {
        // Parent process: wait for the child to finish
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            last_command_status = WEXITSTATUS(status);
        } else {
            last_command_status = 1;
        }
    }
}

// Main function to start the shell
int main(int argc, char *argv[]) {
    // Set PATH to /bin at the start of the program
    setenv("PATH", "/bin", 1);
    init_history();

    if (argc > 2) {
        print_error("Too many arguments. Usage: ./wsh [batch_file]");
        exit(1);
    } else if (argc == 2) {
        // Batch mode
        batch_mode(argv[1]);
    } else {
        // Interactive mode
        interactive_mode();
    }

    return last_command_status;
}
