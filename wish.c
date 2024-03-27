#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <regex.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

struct structargs
{
    pthread_t tIdentifier;
    char *cmds;
};

#define MAX_PATH_LENGTH 256
#define HISTORY_SIZE 1024
char *history[HISTORY_SIZE];
int history_count = 0;
char *inLine = NULL;
char *binPath[1024] = {"/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin", NULL};
FILE *in = NULL;

void *parsingText(void *arg);

void printError();

int pathSearch(char pathing[], const char *initialArg);

void redirection(FILE *out);

void commandExecution(char *args[], int numberArgs, FILE *out);

char *spacing(char *clean);

void frees(bool closeFile, bool freeLine);

void addHistory(const char *cmd);

void showHistory();

void parseAndExecuteCommand(char *cmds);

void processCommandLine(ssize_t numberRead);

int main(int argc, char *argv[]);

char *parseCommandAndRedirection(char **cmdLine, FILE **output);

char **splitIntoArgs(char *cmds, int *numberArgs);

FILE *handleOutputRedirection(char *cmdLine);

void handleExit(int numberArgs);

void handleChangeDirectory(char *directory, int numberArgs);

void handlePathCommand(char *args[], int numberArgs);

void handleCatCommand(char *files[], int num_files);

void executeExternalCommand(char *args[], FILE *out);

void executePipedCommands(char *args[], int numberArgs, FILE *out);

static void executeCommand(const char *path, char *args[], FILE *out);

static void handleChildProcess(char *pathing, char *args[], FILE *out);

void addHistory(const char *cmd)
{
    if (history_count < HISTORY_SIZE)
    {
        history[history_count++] = strdup(cmd);
    }
    else
    {
        // maybe need more edge cases here
        printError();
    }
}

void showHistory()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("%d %s\n", i + 1, history[i]);
    }
}

void parseAndExecuteCommand(char *cmds)
{
    char *tempArgs[256];
    int numberArgs = 0;

    char *commandCopy = strdup(cmds);
    char *rest = commandCopy;
    char *token;

    while ((token = strsep(&rest, " ")) != NULL)
    {
        if (token[0] != '\0')
        {
            tempArgs[numberArgs++] = token;
        }
    }

    tempArgs[numberArgs] = NULL; 
    commandExecution(tempArgs, numberArgs, stdout);
    free(commandCopy); 
}

void processCommandLine(ssize_t bytesRead)
{
    if (bytesRead > 0 && inLine[bytesRead - 1] == '\n') {
        inLine[--bytesRead] = '\0';
    }
    
    addHistory(inLine);

    struct structargs args[1024];
    size_t argsIndex = 0;
    char *commandSegment = NULL;
    char *cursor = inLine;

    while ((commandSegment = strsep(&cursor, "&")) != NULL && argsIndex < 1024) {
        if (*commandSegment != '\0') {
            args[argsIndex++].cmds = strdup(commandSegment);
        }
    }

    for (size_t i = 0; i < argsIndex; i++) {
        if (pthread_create(&args[i].tIdentifier, NULL, parsingText, &args[i]) != 0) {
            printError();
            break;
        }
    }

    for (size_t i = 0; i < argsIndex; i++) {
        if (pthread_join(args[i].tIdentifier, NULL) != 0) {
            printError();
        }
        free(args[i].cmds);
    }
}

int main(int argc, char *argv[])
{
    size_t numberLines = 0;
    ssize_t numberRead;
    bool isBatchMode = false;

    in = (argc > 1) ? fopen(argv[1], "r") : stdin;
    if (argc > 2 || in == NULL)
    {
        printError();
        exit(EXIT_FAILURE);
    }
    isBatchMode = (argc > 1) ? true : false;

    while (1)
    {
        if (!isBatchMode)
            printf("wish> ");

        numberRead = getline(&inLine, &numberLines, in); // specifically said to use getline
        if (numberRead > 0)
            processCommandLine(numberRead);
        else if (feof(in))
        {
            frees(true, true);
            exit(EXIT_SUCCESS);
        }
    }
    return 0;
}

void printError()
{
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message)); 
}

void frees(bool closeFile, bool freeLine)
{
    if (closeFile && in != NULL)
        fclose(in);
    if (freeLine && inLine != NULL)
        free(inLine);
}

char *spacing(char *clean)
{
    if (clean == NULL)
    {
        return NULL;
    }

    char *start = clean;
    while (isspace((unsigned char)*start))
    {
        start++;
    }

    if (*start == '\0')
    {
        *clean = '\0';
        return clean;
    }

    char *end =  strlen(start) - 1 + start;
    while (end > start && isspace((unsigned char)*end))
    {
        end--;
    }

    if (start > clean)
    {
        memmove(clean, start, end - start + 1);
    }

    clean[end - start + 1] = '\0';

    return clean;
}

void *parsingText(void *arg)
{
    struct structargs *arguments = (struct structargs *)arg;
    char *cmdLine = arguments -> cmds;
    FILE *output = stdout;
    int numberArgs = 0;

    cmdLine = parseCommandAndRedirection(&cmdLine, &output);
    if (cmdLine == NULL)
        return NULL;

    char **args = splitIntoArgs(cmdLine, &numberArgs);
    if (numberArgs != 0)
    {
        commandExecution(args, numberArgs, output);
    }

    if (output != stdout)
    {
        fclose(output);
    }

    return NULL;
}

char *parseCommandAndRedirection(char **cmdLine, FILE **output)
{
    char *cmds = strsep(cmdLine, ">"); // use strsep
    if (*cmds == '\0' || cmds == NULL)
    {
        printError();
    }
    cmds = spacing(cmds);
    if (*cmdLine != NULL)
    {
        *output = handleOutputRedirection(*cmdLine);
        if (*output == NULL)
        {
            return NULL;
            printError();
        }
    }
    return cmds;
}

FILE *handleOutputRedirection(char *cmdLine)
{
    char *fileName = spacing(cmdLine);

    for (char *p = fileName; *p; p++)
    {
        if (isspace((unsigned char)*p))
        {
            printError();
            return NULL;
        }
    }
    FILE *file = fopen(fileName, "w"); 
    if (file == NULL)
    {
        printError();
        return NULL;
    }

    return file;
}

char **splitIntoArgs(char *cmds, int *numberArgs)
{
    static char *args[256];
    char **argsPointer = args;
    *numberArgs = 0;

    while ((*argsPointer = strsep(&cmds, " \t")) != NULL)
    {
        if (**argsPointer != '\0')
        {
            *argsPointer = spacing(*argsPointer);
            argsPointer++;
            (*numberArgs)++;
            if (*numberArgs >= 256)
            {
                printError();
                //printf("test");
            }
        }
    }
    return args;
}

int pathSearch(char pathing[], const char *initialArg)
{
    DIR *dir;
    struct dirent *entry;
    int found = -1;
    char fullPath[MAX_PATH_LENGTH];
    int i = 0;
    while (binPath[i] != NULL)
    {
        dir = opendir(binPath[i]);
        if (dir == NULL)
        {
            i++;
            continue;
        }
        while ((entry = readdir(dir)) != NULL)
        {
            int length = snprintf(fullPath, MAX_PATH_LENGTH, "%s/%s", binPath[i], entry->d_name); //snprintf over sprintf
            if (length > 0 && length < MAX_PATH_LENGTH)
            {
                if (strcmp(entry->d_name, initialArg) == 0 && access(fullPath, X_OK) == 0) //permissions & dir exists
                {
                    strncpy(pathing, fullPath, MAX_PATH_LENGTH);
                    found = 0;
                    break;
                }
            }
        }
        closedir(dir);

        if (found == 0)
        {
            break;
        }
        i++;
    }
    return found;
}

void redirection(FILE *out)
{
    int fileout = fileno(out);
    if (fileout == -1)
    {
        printError();
        return;
    }

    if (fileout != STDOUT_FILENO)
    {
        if (dup2(fileout, STDOUT_FILENO) == -1 || dup2(fileout, STDERR_FILENO) == -1)
        {
            printError();
            return;
        }
        fclose(out);
    }
}

void handleExit(int numberArgs)
{
    if (numberArgs > 1)
    {
        printError();
    }
    else
    {
        frees(true, true);
        exit(EXIT_SUCCESS);
    }
}

void handleChangeDirectory(char *directory, int numberArgs)
{
    if (chdir(directory) == -1 || numberArgs > 2 || numberArgs == 1)
        printError();
}

void handlePathCommand(char *args[], int numberArgs)
{
    for (size_t i = 0; i < numberArgs - 1; ++i)
    {
        binPath[i] = strdup(args[1 + i]);
    }
    binPath[numberArgs - 1] = NULL;
}

void handleCatCommand(char *files[], int num_files)
{
    for (int i = 0; i < num_files; ++i)
    {
        FILE *file = fopen(files[i], "r");
        if (file == NULL)
        {
            printError();
            continue;
        }

        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), file) != NULL)
        {
            if (fputs(buffer, stdout) == EOF)
            {
                printError();
                break;
            }
        }
        if (ferror(file))
        {
            printError();
            clearerr(file);
        }
        fclose(file);
    }
}

void executeExternalCommand(char *args[], FILE *out)
{
    char pathing[256];
    if (pathSearch(pathing, args[0]) == 0)
    {
        pid_t pid = fork();
        switch (pid)
        {
            case -1:
                printError();
                break;
            case 0:
                handleChildProcess(pathing, args, out);
                break;
            default:
                waitpid(pid, NULL, 0);
                break;
        }
    }
    else
    {
        printError();
    }
}

static void handleChildProcess(char *pathing, char *args[], FILE *out)
{
    redirection(out);
    executeCommand(pathing, args, out);
}

static void executeCommand(const char *path, char *args[], FILE *out)
{
    if (execv(path, args) == -1)
    {
        printError();
        exit(EXIT_FAILURE);
    }
}

void executePipedCommands(char *args[], int numberArgs, FILE *out) {
    int numPipes = 0;
    for (int i = 0; i < numberArgs; ++i) {
        if (strcmp(args[i], "|") == 0) {
            numPipes++;
        }
    }

    int pipefds[2 * numPipes];
    for (int i = 0; i < numPipes; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            printError();
            exit(EXIT_FAILURE);
        }
    }

    int commandIndex = 0;
    for (int i = 0; i <= numPipes; i++) {
        char *currentArgs[256];
        int j = 0;
        while (commandIndex < numberArgs && strcmp(args[commandIndex], "|") != 0) {
            currentArgs[j++] = args[commandIndex++];
        }
        currentArgs[j] = NULL;
        commandIndex++;

        pid_t pid = fork();
        if (pid == 0) {
            if (i > 0) {
                if (dup2(pipefds[(i - 1) * 2], 0) < 0) {
                    printError();
                    exit(EXIT_FAILURE);
                }
            }
            if (i < numPipes) {
                if (dup2(pipefds[i * 2 + 1], 1) < 0) {
                    printError();
                    exit(EXIT_FAILURE);
                }
            }
            for (int k = 0; k < 2 * numPipes; k++) {
                close(pipefds[k]);
            }
            if (execvp(currentArgs[0], currentArgs) < 0) {
                printError();
                exit(EXIT_FAILURE);
            }
        } else if (pid < 0) {
            printError();
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < 2 * numPipes; i++) {
        close(pipefds[i]);
    }

    for (int i = 0; i <= numPipes; i++) {
        wait(NULL);
    }
}

void commandExecution(char *args[], int numberArgs, FILE *out)
{
    int hasPipe = 0;
    for (int i = 0; i < numberArgs; i++)
    {
        if (strcmp(args[i], " | ") == 0)
        {
            hasPipe = 1;
            break;
        }
    }

    if (hasPipe)
    {
        executePipedCommands(args, numberArgs, out);
    }
    else
    {
        if (args == NULL || args[0] == NULL)
        {
            printError();
            return;
        }
        if (strcmp(args[0], "exit") == 0)
        {
            handleExit(numberArgs);
            return;
        }
        else if (strcmp(args[0], "cd") == 0)
        {
            handleChangeDirectory(args[1], numberArgs);
        }
        else if (strcmp(args[0], "path") == 0)
        {
            handlePathCommand(args, numberArgs);
        }
        else if (strcmp(args[0], "cat") == 0)
        {
            handleCatCommand(args + 1, numberArgs - 1);
        }
        else if (strcmp(args[0], "history") == 0)
        {
            showHistory();
        }
        else if (args[0][0] == '!')
        {
            if (numberArgs == 1)
            {
                int historyIndex = atoi(args[0] + 1) - 1;
                if (historyIndex >= 0 && historyIndex < history_count)
                {
                    parseAndExecuteCommand(history[historyIndex]);
                }
                else
                {
                    printError();
                }
            }
            else
            {
                printError();
            }
        }
        else
        {
            executeExternalCommand(args, out);
        }
    }
}
