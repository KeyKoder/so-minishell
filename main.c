#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"

// TODO: create job struct

int main(void) {
	char buf[1024];
	tline * line;
	int i,j;
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
				}
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
