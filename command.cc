
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <glob.h>
#include <vector>
#include <string>
#include <iostream>
#include "command.h"
#include "tokenizer.h"

static int log_fd = -1;


static std::string strip_quotes(const std::string &s)
{
    std::string r = s;
    if (!r.empty() && r.front() == '"') r.erase(0, 1);
    if (!r.empty() && r.back()  == '"') r.erase(r.size() - 1);
    return r;
}


static void log_child(int pid)
{
    if (log_fd < 0) return;
    char buf[64];
    int n = sprintf(buf, "child %d finished\n", pid);
    write(log_fd, buf, n);
}

void sigchld_handler(int sig)
{     (void)sig;
    int saved = errno;
    int status;
    int pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        log_child(pid);  
    }
    errno = saved;
}

void parse(std::vector<Token> &tokens)
{
    Command cmd;
    SimpleCommand *simple = new SimpleCommand();
    int n = tokens.size();

    for (int i = 0; i < n; i++)
    {
        Token t = tokens[i];

        if (t.type == TOKEN_COMMAND || t.type == TOKEN_ARGUMENT)
        {
           
            std::string val = strip_quotes(t.value);

            if (val.find('*') != std::string::npos ||
                val.find('?') != std::string::npos)
            {
                glob_t g;
                int r = glob(val.c_str(), 0, NULL, &g);
                if (r == 0 && g.gl_pathc > 0)
                {
                    for (int j = 0; j < (int)g.gl_pathc; j++)
                        simple->insertArgument(strdup(g.gl_pathv[j]));
                    globfree(&g);
                    continue;
                }
                globfree(&g);
            }
            simple->insertArgument(strdup(val.c_str()));
        }
        else if (t.type == TOKEN_PIPE)
        {
            cmd.insertSimpleCommand(simple);
            simple = new SimpleCommand();
        }
        else if (t.type == TOKEN_REDIRECT || t.type == TOKEN_APPEND ||
                 t.type == TOKEN_INPUT || t.type == TOKEN_ERROR ||
                 t.type == TOKEN_REDIRECT_AND_ERROR)
        {
            if (i + 1 >= n ||
                (tokens[i+1].type != TOKEN_COMMAND &&
                 tokens[i+1].type != TOKEN_ARGUMENT))
            {
                fprintf(stderr, "syntax error: redirection needs filename\n");

                free(simple->_arguments);
                delete simple;
                cmd.clear();
                return;
            }

            i++;
            char *fname = strdup(strip_quotes(tokens[i].value).c_str());

            if (t.type == TOKEN_REDIRECT)
            {
                if (cmd._outFile) free(cmd._outFile);
                cmd._outFile = fname;
                cmd._append = 0;  
            }
            else if (t.type == TOKEN_APPEND)
            {
                if (cmd._outFile) free(cmd._outFile);
                cmd._outFile = fname;
                cmd._append = 1;
            }
            else if (t.type == TOKEN_INPUT)
            {
                if (cmd._inputFile) free(cmd._inputFile);
                cmd._inputFile = fname;
            }
            else if (t.type == TOKEN_ERROR)
            {
                if (cmd._errFile) free(cmd._errFile);
                cmd._errFile = fname;
            }
            else  
            {
                if (cmd._errFile) free(cmd._errFile);
                cmd._errFile = fname;
                cmd._out_error = 1;
                cmd._append = 1;  
            }
        }
        else if (t.type == TOKEN_BACKGROUND)
        {
            cmd._background = 1;
        }
    }

    cmd.insertSimpleCommand(simple);
    cmd.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
    _arguments[0] = NULL;  
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }
    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;
    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }
    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
            free(_simpleCommands[i]->_arguments[j]);
        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];  
    }

    
    free(_simpleCommands);
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    if (_outFile)   { free(_outFile);   _outFile = 0; }
    if (_inputFile) { free(_inputFile); _inputFile = 0; }
    if (_errFile)   { free(_errFile);   _errFile = 0; }

    _numberOfSimpleCommands = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n");
    printf("Simple Commands:\n");
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  [%d] ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
            printf("\"%s\" ", _simpleCommands[i]->_arguments[j]);
        printf("\n");
    }
    printf("out=%s in=%s err=%s bg=%d\n",
           _outFile   ? _outFile   : "none",
           _inputFile ? _inputFile : "none",
           _errFile   ? _errFile   : "none",
           _background);
    printf("\n");
}

void Command::execute()
{
    int n = _numberOfSimpleCommands;

    if (n == 0)
    {
        clear();
        return;
    }

    
    print();

    
    for (int i = 0; i < n; i++)
    {
        if (_simpleCommands[i]->_numberOfArguments == 0)
        {
            fprintf(stderr, "syntax error: empty command in pipeline\n");
            clear();
            return;
        }
    }

   
    if (n == 1)
    {
        char *cmdname = _simpleCommands[0]->_arguments[0];

        if (strcmp(cmdname, "exit") == 0)
        {
            printf("Good bye!!\n");
            exit(0);
        }

        if (strcmp(cmdname, "cd") == 0)
        {
            int argc = _simpleCommands[0]->_numberOfArguments;
            if (argc == 1)
            {
                char *home = getenv("HOME");
                if (home == NULL)
                    fprintf(stderr, "cd: HOME not set\n");
                else if (chdir(home) != 0)
                    perror("cd");
            }
            else if (argc == 2)
            {
                if (chdir(_simpleCommands[0]->_arguments[1]) != 0)
                    perror("cd");
            }
            else
                fprintf(stderr, "cd: too many arguments\n");
            clear();
            return;
        }
    }

    int npipes = n - 1;
    int *pipes = NULL;

    if (npipes > 0)
    {
        pipes = (int *)malloc(2 * npipes * sizeof(int));
        for (int i = 0; i < npipes; i++)
        {
            if (pipe(&pipes[2*i]) < 0)
            {
                perror("pipe");
                free(pipes);
                clear();
                return;
            }
        }
    }

    pid_t *pids = (pid_t *)malloc(n * sizeof(pid_t));

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            pids[i] = -1;
            continue;
        }

        if (pid == 0)
        {
            if (i > 0)        dup2(pipes[2*(i-1)], 0);
            if (i < n - 1)    dup2(pipes[2*i + 1], 1);

            if (i == 0 && _inputFile != NULL)
            {
                int fd = open(_inputFile, O_RDONLY);
                if (fd < 0) { perror(_inputFile); _exit(1); }
                dup2(fd, 0);
                close(fd);
            }

            if (i == n - 1 && _outFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;
                if (_append) flags = flags | O_APPEND;
                else         flags = flags | O_TRUNC;
                int fd = open(_outFile, flags, 0644);
                if (fd < 0) { perror(_outFile); _exit(1); }
                dup2(fd, 1);
                close(fd);
            }

            if (i == n - 1 && _errFile != NULL)
            {
               
                int flags = O_WRONLY | O_CREAT;
                if (_append) flags = flags | O_APPEND;
                else         flags = flags | O_TRUNC;
                int fd = open(_errFile, flags, 0644);
                if (fd < 0) { perror(_errFile); _exit(1); }
                dup2(fd, 2);
                if (_out_error) dup2(fd, 1);
                close(fd);
            }

            for (int j = 0; j < 2 * npipes; j++) close(pipes[j]);
            signal(SIGINT, SIG_DFL);

            execvp(_simpleCommands[i]->_arguments[0],
                   _simpleCommands[i]->_arguments);
            perror(_simpleCommands[i]->_arguments[0]);
            _exit(127);
        }

        pids[i] = pid;
    }

    for (int j = 0; j < 2 * npipes; j++) close(pipes[j]);

    if (!_background)
    {
        for (int i = 0; i < n; i++)
        {
            if (pids[i] > 0)
            {
                waitpid(pids[i], NULL, 0);
                log_child(pids[i]);  
            }
        }
    }

    free(pipes);
    free(pids);
    clear();
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (true)
    {
        if (!std::getline(std::cin, input))
        {
            printf("\n");
            exit(0);
        }
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
        printf("myshell>");
        fflush(stdout);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    log_fd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    Command::_currentCommand.prompt();
    return 0;
}
