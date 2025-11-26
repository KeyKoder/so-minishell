#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include "parser.h"

#define PATH_LEN 1024

// TODO: create job struct

int cd(char* dir) {
    if(dir == NULL) {
        dir = getenv("HOME");
    }

    if(chdir(dir) != 0) {
        fprintf(stderr, "Error al cambiar. %s\n", strerror(errno));
        return errno;
    }

    char cwd[PATH_LEN];
    getcwd(cwd, PATH_LEN);
    printf("%s\n", cwd);

    return 0;
}


// TODO MEMORIA: Explain string to octal algorithm

int applyUmask(char* mode) {
	int modeInt = 0;
	int i;
	for(i=0; i<strlen(mode); i++){
		if(mode[i] >= '0' && mode[i] <= '9'){
			modeInt = modeInt | mode[i] -'0' << ((2-i)*3);
		}
		else{
			printf("Numero fuera del rango 0-9.");
        	return 1;
		}
	}
	umask(modeInt);

	return 0;
}

int main(void) {
	char buf[1024];
	tline * line;
	int i;
	pid_t childPid; // TOOO: Swap with a dynamic size array for handling multiple commands

	printf("==> ");
	while (fgets(buf, 1024, stdin)) {
		line = tokenize(buf);
		if (line == NULL)
			continue;
		if (line->redirect_input != NULL) {
			// TODO: Handle stdin redirection
			printf("redirección de entrada: %s\n", line->redirect_input);
		}
		if (line->redirect_output != NULL) {
			// TODO: Handle stdout redirection
			printf("redirección de salida: %s\n", line->redirect_output);
		}
		if (line->redirect_error != NULL) {
			// TODO: Handle stderr redirection
			printf("redirección de error: %s\n", line->redirect_error);
		}
		if (line->background) {
			// TODO: Handle background execution with jobs
			printf("comando a ejecutarse en background\n");
		}
		for (i=0; i<line->ncommands; i++) {
			if (line->commands[i].filename == NULL) {
				// TODO: Handle cd, exit, jobs, fg and other commands that don't have an executable
				if (strcmp(line->commands[i].argv[0], "exit") == 0) {
					exit(0);
				} else if (strcmp(line->commands[i].argv[0], "cd") == 0) {
					// cd(line->commands[i].argc == 1 ? NULL : line->commands[i].argv[1]);
					if (line->commands[i].argc == 1) {
						cd(NULL);
					} else {
						cd(line->commands[i].argv[1]);
					}
				} else if (strcmp(line->commands[i].argv[0], "umask") == 0) {
					if (line->commands[i].argc == 2) {
						applyUmask(line->commands[i].argv[1]);
					} else {
						// TODO: Replace this with code to get the value of umask when called with no arguments (argc == 1)
						printf("USO:\numask [mode]\nmode - Valor octal de la máscara a aplicar a los permisos para los nuevos ficheros");
					}
				}
				continue;
			} else {
				// TODO: Fork and exec command

				childPid = fork();
				if (childPid < 0) {
					// TODO: Handle fork error somehow
					continue;
				}

				if (childPid == 0) { // CHILD
					execvp(line->commands[i].filename, line->commands[i].argv);

					exit(39); // TODO: execvp failed, think of what to return here later
				} else { // NOT CHILD
					waitpid(childPid, NULL, 0);
					continue;
				}
			}
		}
		printf("==> ");
	}
	return 0;
}
