#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include "parser.h"

#define PATH_LEN 1024
#define MAX_INITIAL_JOBS 1
#define JOBS_RESIZE_FACTOR 2

mode_t mascara;

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
	mode_t modeInt = 0;
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
	mascara = modeInt;
	umask(modeInt);

	return 0;
}

typedef struct Job job_t;

struct Job {
	tline* line;
	
	pid_t pid;
	pid_t childPid;

	int* pipes;
	FILE* stdin_redirect;
	FILE* stdout_redirect;
	FILE* stderr_redirect;

	struct Job* next;
};


job_t* jobs;
job_t* currentJob;

job_t* createJob() {
	job_t* job = malloc(sizeof(job_t));
	
	job->line = NULL;
	job->pipes = NULL;
	job->stdin_redirect = NULL;
	job->stdout_redirect = NULL;
	job->stderr_redirect = NULL;
	
	job->next = NULL;

	if(jobs == NULL) {
		jobs = job;
	}else {
		job_t* lastJob = jobs;
		while(lastJob->next != NULL) {
			lastJob = lastJob->next;
		}

		lastJob->next = job;
	}
	
	return job;
}

void freeJob(job_t* job) {
	if (job->stdin_redirect != NULL) {
		fclose(job->stdin_redirect);
		job->stdin_redirect = NULL;
	}
	if (job->stdout_redirect != NULL) {
		fclose(job->stdout_redirect);
		job->stdout_redirect = NULL;
	}
	if (job->stderr_redirect != NULL) {
		fclose(job->stderr_redirect);
		job->stderr_redirect = NULL;
	}

	if(job->pipes != NULL) {
		free(job->pipes);
	}

	// check job->next to see if its the target of free
	if(job == jobs) { // edge case: its the first one
		jobs = job->next;
	}else { // its somewhere in the list
		job_t* prevJob = jobs;
		while(prevJob->next != job) {
			prevJob = prevJob->next;
		}
		prevJob->next = job->next->next;
	}

	free(job);
}

// frees/cleans up bg job
// outputs "job finished" msg to stdout
// void handleJobClose(int sig) {} // TODO: Implement this for SIGCHLD


int main(void) {
	char buf[1024];
	tline * line;
	int i;

	// Get current active umask value. umask() returns the previous umask value.
	mascara = umask(0);
	umask(mascara);
	
	signal(SIGINT, SIG_IGN);

	printf("msh> ");
	while (fgets(buf, 1024, stdin)) {
		line = tokenize(buf);

		currentJob = createJob();
		if (line == NULL)
			continue;
		if (line->redirect_input != NULL) {
			// TODO: Ensure file exists, error out if not
			currentJob->stdin_redirect = fopen(line->redirect_input, "r");
		}
		if (line->redirect_output != NULL) {
			currentJob->stdout_redirect = fopen(line->redirect_output, "w");
		}
		if (line->redirect_error != NULL) {
			currentJob->stderr_redirect = fopen(line->redirect_error, "w");
		}
		if (line->background) {
		}

		currentJob->line = line;
		// TODO: Report command not found/not recognized
		if (currentJob->line->ncommands == 1 && currentJob->line->commands[0].filename == NULL && !currentJob->line->background) {
			// TODO: Handle cd, exit, jobs, fg and other commands that don't have an executable
			if (strcmp(currentJob->line->commands[0].argv[0], "exit") == 0) {
				exit(0);
			} else if (strcmp(currentJob->line->commands[0].argv[0], "cd") == 0) {
				// cd(currentJob->line->commands[0].argc == 1 ? NULL : currentJob->line->commands[0].argv[1]);
				if (currentJob->line->commands[0].argc == 1) {
					cd(NULL);
				} else {
					cd(currentJob->line->commands[0].argv[1]);
				}
			} else if(strcmp(currentJob->line->commands[0].argv[0], "umask") == 0) {
				if (currentJob->line->commands[0].argc == 2) {
					if(strlen(currentJob->line->commands[0].argv[1])==4){
						applyUmask(currentJob->line->commands[0].argv[1]+1);
					}else if(strlen(currentJob->line->commands[0].argv[1])==3){
						applyUmask(currentJob->line->commands[0].argv[1]);
					}else{
						printf("USO:\numask [mode]\nmode - Valor octal de la máscara a aplicar a los permisos para los nuevos ficheros\n");
					}
				} else if (currentJob->line->commands[0].argc == 1){
					printf("%04o\n", mascara);
				}
			}
		}else {
			currentJob->pipes = (int*)malloc((line->ncommands-1) * sizeof(int)*2);

			currentJob->pid = fork();

			if(currentJob->pid == 0) { // CHILD (job container)
				if(!currentJob->line->background) signal(SIGINT, SIG_DFL);
				
				for (i=0; i<currentJob->line->ncommands; i++) {
					if(i < currentJob->line->ncommands-1) {
						pipe(currentJob->pipes+i*2);
					}

					currentJob->childPid = fork();
					if (currentJob->childPid < 0) {
						break;
					}

					if (currentJob->childPid == 0) { // JOB CHILD (single command execution)
						if (i == 0 && currentJob->line->redirect_input != NULL) {
							close(STDIN_FILENO);
							dup(fileno(currentJob->stdin_redirect));
							fclose(currentJob->stdin_redirect);
						}
						if (i == currentJob->line->ncommands-1 && currentJob->line->redirect_output != NULL) {
							close(STDOUT_FILENO);
							dup(fileno(currentJob->stdout_redirect));
							fclose(currentJob->stdout_redirect);
						}
						if (i == currentJob->line->ncommands-1 && currentJob->line->redirect_error != NULL) {
							close(STDERR_FILENO);
							dup(fileno(currentJob->stderr_redirect));
							fclose(currentJob->stderr_redirect);
						}

						if(i == 0) {
							close(currentJob->pipes[0]);
							dup2(currentJob->pipes[1], STDOUT_FILENO);
						} else if(i == currentJob->line->ncommands-1) {
							close(currentJob->pipes[(i-1)*2+1]);
							dup2(currentJob->pipes[(i-1)*2], STDIN_FILENO);
						} else {
							close(currentJob->pipes[(i-1)*2+1]);
							dup2(currentJob->pipes[(i-1)*2], STDIN_FILENO);

							close(currentJob->pipes[i*2]);
							dup2(currentJob->pipes[i*2+1], STDOUT_FILENO);
						}

						execvp(currentJob->line->commands[i].filename, currentJob->line->commands[i].argv);

						exit(39);
					} else { // JOB PARENT (job container)
						if(i > 0) {
							close(currentJob->pipes[(i-1)*2+1]);
							close(currentJob->pipes[(i-1)*2]);
						}
						waitpid(currentJob->childPid, NULL, 0);
						continue;
					}
				}
				exit(0); // Exit from job container process
			}else { // NOT CHILD (minishell)
				if(!line->background) waitpid(currentJob->pid, NULL, 0);
			}
		}

		// Cleanup
		if(!line->background) {
			freeJob(currentJob);
		}

		printf("msh> ");
	}

	return 0;
}
