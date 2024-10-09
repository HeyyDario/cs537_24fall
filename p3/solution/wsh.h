#ifndef WSH_H
#define WSH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>  // For directory listing (used in the ls built-in)

#define MAX_VARS 100  // Maximum number of shell variables
#define MAX_VAR_LENGTH 100  // Maximum length of a shell variable
#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64
#define DEFAULT_HISTORY_SIZE 5

// Shell variables
// typedef struct shell_var {
//     char *name;
//     char *value;
//     struct shell_var *next;
// } shell_var;

// extern shell_var *shell_vars;  // Declare external shell_vars

typedef struct {
    char name[MAX_VAR_LENGTH];
    char value[MAX_VAR_LENGTH];
} ShellVar;

// Declare global variables and functions for handling shell variables
extern ShellVar shell_vars[MAX_VARS];
extern int num_vars;

// Command history structure
// typedef struct {
//     char **commands;
//     int capacity;
//     int count;
//     int start;
//     int end;
// } history_t;
// Structure to store history
typedef struct {
    char **commands;
    int capacity;
    int count;
    int start;
    int end;
} history_t;

// Declare global history object
extern history_t history;  

void init_history();
void add_to_history(const char *cmd);
void print_history();  // Print the stored history
void execute_history_command(int n);
void resize_history(int new_size);

//extern history_t history;  // Declare external history
extern int last_command_status;

// Function prototypes

void initialize_path();
// Shell modes
void interactive_mode();
void batch_mode(const char* batch_file);

// Command execution
void execute_command(char* command);
void print_error(const char* message);

void clean_command(char *command);
int is_builtin_command(const char *cmd);
//

const char* get_shell_var(const char* varname);
void set_shell_var(const char* varname, const char* value);
void handle_local_command(char* command);
void expand_variables(char *command);
void handle_export_command(char *command);
void handle_vars_command();

// Built-in command handlers
void handle_cd_command(char *args[]);
void handle_ls_command();
void handle_redirection(int redirect_type, char *filename);
void substitute_variables(char *cmd);

// History functions
void init_history();
void add_to_history(const char *cmd);
void print_history();
void execute_history_command(int n);
void resize_history(int new_size);

// Helper functions
int is_comment(char *line);
const char *get_local_variable(const char *name);
void free_shell_variables();
char* find_command_path(const char* command, char* full_path);

#endif  // WSH_H
