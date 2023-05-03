#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>


#define MAX_ARGS 10   // Maximum number of arguments for a command
#define MAX_CMD 510   // Maximum length of a command
#define MAX_PATH 4096 // Maximum length of a file path
#define MAX_ENV_VARS 5000 // Maximum number of environment variables

char* environment[MAX_ENV_VARS]; // Array to store environment variables and their values
int envVarCounter = 0; // Counter for the number of environment variables



void printPrompt(int cmdNum, int argNum);
void customEcho(char **args);
char* getEnvVar(char *var);
void setEnvVar(char *var, char *val);




void printPrompt(int cmdNum, int argNum) {
    char cwd[MAX_PATH];
    char *cd = getcwd(cwd, sizeof(cwd));
    printf("#cmd:%d|#args:%d@%s\t", cmdNum, argNum, cd);
}

void customEcho(char **args) {
    if (args[0] == NULL) {
        return;
    }
    for (int i = 1; args[i] != NULL; i++) {
        char *arg = args[i];
        int len = strlen(arg);
        if (len > 1 && arg[0] == '"' && arg[len - 1] == '"') {
            arg[len - 1] = '\0';
            arg++;
            len -= 2;
        }
        for (int j = 0; j < len; j++) {
            if (arg[j] == '\\' && j < len - 1) {
                j++;
            }
            if (arg[j] == '"' || arg[j] == '\\') {
                continue;
            } else {
                printf("%c", arg[j]);
            }
        }
        if (args[i + 1] != NULL) {
            printf(" ");
        }
    }
    printf("\n");
}


int contains_pipe(char* cmd) {
    for (int i = 0; i < strlen(cmd); i++) {
        if (cmd[i] == '|') {
            return 1;
        }
    }
    return 0;
}

void split_piped_commands(char* cmd, char** left_cmd, char** right_cmd) {
    char* token;
    char* saveptr;

    // Split the command into two parts using the "|" character as a delimiter
    token = strtok_r(cmd, "|", &saveptr);
    *left_cmd = token;
    token = strtok_r(NULL, "|", &saveptr);
    *right_cmd = token;
}

void execute_with_pipe(char* left_cmd[], char* right_cmd[]) {
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process: redirect standard output to the write end of the pipe
        close(pipefd[0]); // close the read end of the pipe
        dup2(pipefd[1], STDOUT_FILENO); // redirect standard output to the write end of the pipe
        close(pipefd[1]); // close the write end of the pipe

        // Tokenize the left command
        int num_args = 0;
        char *arg = strtok(left_cmd[0], " \n");
        while (arg != NULL && num_args < MAX_ARGS - 1) {
            left_cmd[num_args++] = arg;
            arg = strtok(NULL, " \n");
        }
        left_cmd[num_args] = NULL;

        execvp(left_cmd[0], left_cmd); // execute the left command
        perror(left_cmd[0]); // print an error message if execvp fails
        exit(1);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process: redirect standard input to the read end of the pipe
        close(pipefd[1]); // close the write end of the pipe
        dup2(pipefd[0], STDIN_FILENO); // redirect standard input to the read end of the pipe
        close(pipefd[0]); // close the read end of the pipe

        // Tokenize the right command
        int num_args = 0;
        char *arg = strtok(right_cmd[0], " \n");
        while (arg != NULL && num_args < MAX_ARGS - 1) {
            right_cmd[num_args++] = arg;
            arg = strtok(NULL, " \n");
        }
        right_cmd[num_args] = NULL;

        execvp(right_cmd[0], right_cmd); // execute the right command
        perror(right_cmd[0]); // print an error message if execvp fails
        exit(1);
    }

    // Parent process: close both ends of the pipe and wait for both child processes to terminate
    close(pipefd[0]);
    close(pipefd[1]);
    wait(NULL);
    wait(NULL);
}



void setEnvVar(char* name, char* value) {
    // Search for the environment variable in the array
    int i;
    for (i = 0; i < envVarCounter; i++) {
        if (strcmp(environment[i], name) == 0) {
            break;
        }
    }
    if (i < envVarCounter) {
        // If the environment variable already exists, update its value
        free(environment[i]);
        environment[i] = strdup(value);
    } else if (envVarCounter < MAX_ENV_VARS) {
        // If the environment variable doesn't exist and the array is not full, add it to the array
        environment[envVarCounter++] = strdup(name);
        environment[envVarCounter++] = strdup(value);
    } else {
        printf("Error: Maximum number of environment variables reached\n");
    }
}

char* getEnvVar(char* name) {
    // Search for the environment variable in the array
    for (int i = 0; i < envVarCounter; i++) {
        if (strcmp(environment[i], name) == 0) {
            return environment[i + 1]; // Return the value of the environment variable
        }
    }
    return NULL; // Return NULL if the environment variable is not found
}

int main() {
    char cmd[MAX_CMD];
    char *args[MAX_ARGS];
    pid_t pid[MAX_ARGS];
    int argsCounter = 0, cmdCounter = 0, exitCounter = 0;
    char *left_cmd[MAX_ARGS], *right_cmd[MAX_ARGS];

    while (1) { // Start an infinite loop
        printPrompt(cmdCounter, argsCounter); // Print the prompt
        fgets(cmd, sizeof(cmd), stdin); // Read user input
        if (strchr(cmd, '\n') == NULL && !feof(stdin)) {
            printf("\n Error: more than 510 chars\n");
            continue;
        }
        char *cmdToken;
        char *savePtr;

        // Check if user has pressed "Enter" 3 times in a row to exit
        if (strcmp(cmd, "\n") == 0) {
            exitCounter++;
        } else {
            exitCounter = 0;
        }
        if (exitCounter == 3) {
            break;
        }

        if (contains_pipe(cmd)) { // Check if command contains pipe symbol
            split_piped_commands(cmd, left_cmd, right_cmd); // Split the command into two parts

            // Tokenize the left command
            int num_left_args = 0;
            char *left_arg = strtok(left_cmd[0], " \n"); // Tokenize the command and arguments
            while (left_arg != NULL && num_left_args < MAX_ARGS - 1) {
                left_cmd[num_left_args++] = left_arg;
                left_arg = strtok(NULL, " \n");
            }
            left_cmd[num_left_args] = NULL;

            // Tokenize the right command
            int num_right_args = 0;
            char *right_arg = strtok(right_cmd[0], " \n"); // Tokenize the command and arguments
            while (right_arg != NULL && num_right_args < MAX_ARGS - 1) {
                right_cmd[num_right_args++] = right_arg;
                right_arg = strtok(NULL, " \n");
            }
            right_cmd[num_right_args] = NULL;

            execute_with_pipe(left_cmd, right_cmd); // Execute the two commands with a pipe
            continue;
        }

        cmdToken = strtok_r(cmd, ";", &savePtr); // Tokenize the input string by semicolon
        while (cmdToken != NULL) { // Loop through each token
            int num_args = 0;
            char *arg = strtok(cmdToken, " \n"); // Tokenize the command and arguments
            while (arg != NULL && num_args < MAX_ARGS - 1) {
                // Check if argument is an environment variable
                if (arg[0] == '$') {
                    char *envVar = getEnvVar(arg + 1);
                    if (envVar != NULL) {
                        args[num_args++] = envVar;
                    }
                }
                    // Check if argument is setting an environment variable
                else if (strchr(arg, '=') != NULL) {
                    char *var = strtok(arg, "=");
                    char *val = strtok(NULL, "=");
                    char *next_token = strtok(val, ";");
                    while (next_token != NULL) {
                        setEnvVar(var, next_token); // Set the environment variable
                        next_token = strtok(NULL, ";");
                    }
                }
                else {
                    args[num_args++] = arg;
                }
                arg = strtok(NULL, " \n");
            }
            if (num_args == MAX_ARGS-1 && arg != NULL) {
                printf("Error: too many arguments (maximum is %d)\n", MAX_ARGS);
                cmdCounter--;
                argsCounter--;
                continue;
            }
            args[num_args] = NULL; // Set the last argument to NULL for execvp()
            if (num_args == 0) {
                break;
            }

            if (strcmp(args[0], "echo") == 0) {
                customEcho(args);
                argsCounter += num_args;
                cmdCounter++;
                break;
            }


            if (strcmp(args[0], "cd") == 0) {
                printf("cd not supported\n");
            }
            if (strcmp(args[num_args - 1], "&") == 0) { // Check for "&" operator
                pid[num_args] = fork(); // Create a child process
                if (pid[num_args] == -1) {
                    printf("ERR\n");
                    exit(1);
                } else if (pid[num_args] == 0) { // Child process
                    args[num_args - 1] = NULL; // Remove "&" from arguments
                    execvp(args[0], args); // Execute the command
                    exit(1);}
            } else { // Run the command in the foreground
                pid[num_args] = fork(); // Create a child process
                if (pid[num_args] == -1) {
                    printf("ERR\n");
                    exit(1);
                } else if (pid[num_args] == 0) { // Child process
                    execvp(args[0], args); // Execute the command
                    exit(1);
                } else { // Parent process
                    int status;
                    waitpid(pid[num_args], &status, 0); // Wait for the child process to complete
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { // Check if command executed successfully
                        cmdCounter++;
                        int i = 0;
                        while (args[i] != NULL) { // Count the number of arguments for prompt line
                            argsCounter++;
                            i++;}}}}
            cmdToken = strtok_r(NULL, ";", &savePtr); // Get the next token
        }
    }
    return 0;
}
