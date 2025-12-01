#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include "parser.h"

#define PATH_LEN 1024

typedef struct Job job_t;

struct Job {
	tline* line;
	
	pid_t pid;
	pid_t childPid;

	int* pipes;
	FILE* stdin_redirect;
	FILE* stdout_redirect;
	FILE* stderr_redirect;
	
	int background;
	char* originalLine;

	struct Job* next;
};


int cd(char* dir);

int applyUmask(char* mode);

job_t* createJob();

void freeJob(job_t* job);

mode_t mask;

job_t* jobs = NULL;
job_t* currentJob = NULL;

int main(void) {
	char buf[1024];
	tline * line;
	int i;
	pid_t currentJobPid;

	job_t* currentSelectedJob;

	// Used for checking the state of background jobs
	job_t* bgCheckJob;
	job_t* bgCheckJobNext;
	int bgCheckStatus;

	// Get current active umask value. umask() returns the previous umask value.
	mask = umask(0);
	umask(mask);
	
	signal(SIGINT, SIG_IGN);

	printf("msh> ");
	while (fgets(buf, 1024, stdin)) {
		line = tokenize(buf);

		if (line == NULL)
			continue;

		if(line->ncommands != 0) {
			currentJob = createJob();
			
			currentJob->originalLine = (char*)malloc(sizeof(char) * strlen(buf));
			strcpy(currentJob->originalLine, buf);
			
			if (line->redirect_input != NULL) {
				if(access(line->redirect_input, F_OK) == 0) {
					currentJob->stdin_redirect = fopen(line->redirect_input, "r");
				} else {
					// Error out, print prompt and skip
					printf("ERROR: El archivo %s no es accesible\n", line->redirect_input);
					freeJob(currentJob);
					printf("msh> ");
					continue;
				}
			}
			if (line->redirect_output != NULL) {
				currentJob->stdout_redirect = fopen(line->redirect_output, "w");
			}
			if (line->redirect_error != NULL) {
				currentJob->stderr_redirect = fopen(line->redirect_error, "w");
			}
			if (line->background) {
				currentJob->background = 1;
			}

			currentJob->line = line;

			if (line->ncommands == 1 && line->commands[0].filename == NULL && !line->background) {
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
						printf("%04o\n", mask);
					}
				} else if(strcmp(currentJob->line->commands[0].argv[0], "jobs") == 0) {
					currentSelectedJob = jobs;
					while(currentSelectedJob != NULL) {
						if(currentSelectedJob->background) {
							printf("[%d] Running\t\t%s", currentSelectedJob->pid, currentSelectedJob->originalLine);
						}

						currentSelectedJob = currentSelectedJob->next;
					}
				} else if(strcmp(currentJob->line->commands[0].argv[0], "fg") == 0) {
					currentSelectedJob = NULL;
					if (currentJob->line->commands[0].argc == 2) {
						currentSelectedJob = jobs;
						while(currentSelectedJob != NULL) {
							if(currentSelectedJob->background && currentSelectedJob->pid == atoi(currentJob->line->commands[0].argv[1])) {
								break;
							}

							currentSelectedJob = currentSelectedJob->next;
						}
					} else if (currentJob->line->commands[0].argc == 1){
						// Get last job (that is not me)
						currentSelectedJob = jobs;
						while(currentSelectedJob != NULL) {
							if(currentSelectedJob->background && currentSelectedJob->next == currentJob) {
								break;
							}

							currentSelectedJob = currentSelectedJob->next;
						}
					} else {
						printf("USO:\nfg [pid]\npid - PID del job a retomar\n");
					}

					if(currentSelectedJob != NULL) {
						waitpid(currentSelectedJob->pid, NULL, 0);
						freeJob(currentSelectedJob);
					}
				}
			}else {
				currentJob->pipes = (int*)malloc((line->ncommands-1) * sizeof(int)*2);

				currentJobPid = fork();

				if(currentJobPid == 0) { // CHILD (job container)
					if(!currentJob->background) signal(SIGINT, SIG_DFL);
					
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
					currentJob->pid = currentJobPid;
					if(!line->background) waitpid(currentJob->pid, NULL, 0);
				}
			}

			// Cleanup if not background and not custom shell command (fg, jobs...)
			if(!line->background && currentJob->pid != 0) {
				freeJob(currentJob);
			}
		}

		// Check if bg jobs finished
		bgCheckJob = jobs;
		while(bgCheckJob != NULL) {
			bgCheckJobNext = bgCheckJob->next;
			
			if(bgCheckJob->background) {
				if(waitpid(bgCheckJob->pid, &bgCheckStatus, WNOHANG) > 0) {
					if(WIFEXITED(bgCheckStatus)) {
						printf("[%d] Done\t\t%s", bgCheckJob->pid, bgCheckJob->originalLine);
						freeJob(bgCheckJob);
					}
				}
			}

			bgCheckJob = bgCheckJobNext;
		}

		printf("msh> ");
	}

	return 0;
}


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
	mask = modeInt;
	umask(modeInt);

	return 0;
}

job_t* createJob() {
	job_t* job = malloc(sizeof(job_t));
	
	job->line = NULL;
	job->pipes = NULL;
	job->stdin_redirect = NULL;
	job->stdout_redirect = NULL;
	job->stderr_redirect = NULL;
	
	job->next = NULL;
	job->originalLine = NULL;
	
	job->background = 0;

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

	free(job->originalLine);

	// check job->next to see if its the target of free
	if(job == jobs) { // edge case: its the first one
		jobs = job->next;
	}else { // its somewhere in the list
		job_t* prevJob = jobs;
		while(prevJob->next != job) {
			prevJob = prevJob->next;
		}
		prevJob->next = job->next;
	}

	free(job);
}
