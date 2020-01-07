#ifndef DEFINES_H
#define DEFINES_H
#include "jobber.h"
#include <unistd.h>
#include "task.h"

#define MAXARGS 64

typedef struct job_table {
	int filled;
	pid_t pid;
	int pgid;
	JOB_STATUS state;
	int canceled;
	int exit_status;
	char* cmd;
} job_table;

job_table job_list[MAX_JOBS];  // Job table list

/* Global Variables */
extern volatile sig_atomic_t ENABLED;
extern volatile int RUNNERS;
extern volatile int RUNNER_PROCESS_RUNNING;
extern volatile int cmd_count;

extern volatile int SIGCHILD_FLAG;


/* Helper functions */
extern int digits_only(const char *s);
extern char* trim(char* resp);
extern void runner_process();
extern int signal_handler();
extern void sigchild_handler(int signum);


/* Direct Functions */
extern void print_help();
extern void print_job_list();		// Parse list and print all the contents of the list utilize unparse_task() func to get the combined command task
extern int quit_expunge();  		// Call each job in the list and force kill each along with expunge job_expunge with job_id



















#endif



