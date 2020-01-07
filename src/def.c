#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <ctype.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "defines.h"


/* Some Helper Func  */
volatile int SIGCHILD_FLAG = 0;

int digits_only(const char* s)
{
	if( *s>='0' && *s<='9' ) return 1;
	return 0;
}

char* trim(char* resp) {
        int length = strlen(resp) - 1;
        if( *(resp + length) == ' '){
                while( *(resp + length ) == ' ') {
                        length--;
                }
        }
        else
                return resp;

        char *new = (char*)malloc(length+2);
        strncpy(new, resp, length+1);
        *(new + length + 1) = '\0';
        free(resp);
        return new;
}


void print_help() {

        printf("Available commands:\n");
        printf("help (0 args) Print this help message\n");
        printf("quit (0 args) Quit the program\n");
        printf("enable (0 args) Allow jobs to start\n");
        printf("disable (0 args) Prevent jobs from starting\n");
        printf("spool (1 args) Spool a new job\n");
        printf("pause (1 args) Pause a running job\n");
        printf("resume (1 args) Resume a paused job\n");
        printf("cancel (1 args) Cancel an unfinished job\n");
        printf("expunge (1 args) Expunge a finished job\n");
        printf("status (1 args) Print the status of a job\n");
        printf("jobs (0 args) Print the status of all jobs\n");
        return;
}


static void print_job_main(job_table* job, int jobid) {
        printf("job %d [%s (canceled: %d)]: %s", jobid, job_status_names[job->state], job->canceled, job->cmd);
        printf("\n");
        return;
}


void print_job_list() {
        if(jobs_get_enabled()!=0)
                printf("Starting jobs is enabled\n");
        else
                printf("Starting jobs is disabled\n");
        for (int i = 0; i < MAX_JOBS; i++){
                if(job_list[i].filled != 0)
                        print_job_main(&job_list[i], i);
        }
        return;
}

int quit_expunge() {
        // cancel the job if running by sending SIGKILL after subsequntly terminates then state change from Cancelled to Completed or aborted,
        // abort the process if job not started
        for (int i = 0; i < MAX_JOBS; i++){
		sigset_t master_mask_all, master_prev_one;
        	sigfillset(&master_mask_all);
	        sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */
                if(job_list[i].filled != 0){
			JOB_STATUS status = job_list[i].state;
			if( (status != COMPLETED) &&(status != ABORTED) )
			{
			int ret_cancel = job_cancel(i);
                        if( ret_cancel != 0 ){
				printf("-----------\n");
			}
			}
                }
		sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
        }
        // waiting for all jobs to become canceled or abort or complete
        int all_jobs_cancel_count = 0;
    while(1){
        all_jobs_cancel_count = 0;

	sigset_t master_mask_all, master_prev_one;
        sigfillset(&master_mask_all);
        sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */

        for (int i = 0; i < MAX_JOBS; i++){
		

                if(job_list[i].filled != 0){
			//sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */
			JOB_STATUS status = job_list[i].state;
			//printf("jobid - %d  status - %s\n", i, job_status_names[status]);
			//if( (status == RUNNING) || (status == WAITING) || (status == NEW) || (status == PAUSED) || (status == CANCELED)){
			if( (status != COMPLETED) && (status != ABORTED) ){
                                all_jobs_cancel_count++;
                        }
			//sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
                }
        }
	sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */


	if( all_jobs_cancel_count != 0)
	{
		sigset_t mask, omask;
        	sigfillset(&mask);
	        sigprocmask(SIG_BLOCK, &mask, &omask);

		signal_handler();

		sigprocmask(SIG_SETMASK, &omask, NULL);

	}
	else
	{
		break;
	}
    }

        for (int i = 0; i < MAX_JOBS; i++){
                if(job_list[i].filled != 0){
                        int expunge_ret = job_expunge(i);
                        if(expunge_ret != 0){
			    return -1;
                        }
                }
        }

        return 0;
}


static void parseline(char *cmdline, char *argv[]) {
  int i = 0;
  char *cp = cmdline;
  argv[0] = cp;
  while(*cp != '\0' && i < MAXARGS-1) {
    if(isspace(*cp)) {
      *cp++ = '\0';
      argv[++i] = cp;
      //printf("argv  %d  %s\n", i-1, argv[i-1]);
    } else {
      cp++;
    }
  }
  argv[++i] = NULL;
}


static int valueinarray(int val, int arr[], int size)
{
    int i;
    for(i = 0; i < size; i++)
    {
        if(arr[i] == val)
            return 1;
    }
    return 0;
}


static int update_pid_pgid(int pid, job_table *job) {
	job->pid = pid;
        job->pgid = pid;
        return 1;
}


// Handles input output redirection
int create_master_pipeline( COMMAND_LIST* command_list, const char* in, const char* out, int pgid ) {

	int exit_status_last_cmd;
	int pid;
	//sigset_t master_mask_all, master_mask_child, master_prev_one;
        //sigfillset(&master_mask_all);
        //sigemptyset(&master_mask_child);
        //sigaddset(&master_mask_child, SIGCHLD);
        //sigprocmask(SIG_BLOCK, &master_mask_child, &master_prev_one); /* Block SIGCHLD */

	// This is master pipeline process Handle input output redirection
	if ((pid = fork()) == 0) {
	    //sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
	    // Handling process groupid
	    setpgid(0, pgid);
            cmd_count = 0; // Counter for number of commands in this pipeline  (Used for input redirection also)
	    int local_cmd_count = 0;
	    //int last_command_in_list = 0; // Flag for finding the last command in the list to handle output redirection
    	    //signal(SIGCHLD, master_pipeline_child_handler);
	    //sigset_t mask_all, mask_child, prev_one;
	    //sigfillset(&mask_all);
	    //sigemptyset(&mask_child);
	    //sigaddset(&mask_child, SIGCHLD);
	    //signal(SIGCHLD, sigchild_handler_master);

	    COMMAND_LIST* command_list_temp = command_list;    
	    // while loop to find number of commands in the list
	    while(command_list_temp != NULL) {
		cmd_count++;
		command_list_temp = command_list_temp->rest;
	    }
	    int cmd_pid[cmd_count];
	    
	    //sigprocmask(SIG_BLOCK, &mask_child, &prev_one); /* Block SIGCHLD */
	    int new_fds[2];
	    int old_fds[2];

	    while(command_list != NULL){
                    WORD_LIST* words = command_list->first->words;
		    char * new_str ;
		    new_str = (char *)malloc(1);
		    *new_str = '\0';
                    while( words != NULL){
			    int old_length = strlen(new_str);
			    if(old_length == 0){
				    int new_length = strlen(words->first) + old_length + 1;
				    if((new_str = realloc(new_str, new_length)) == NULL){
                                	    printf("malloc failed!\n");
                            	    }
				    strcat(new_str, words->first);
			    }
			    else{
				    int new_length = strlen(words->first) + old_length + 2;
				    if((new_str = realloc(new_str, new_length)) == NULL){
                                    	printf("malloc failed!\n");
                            	    }
				    *(new_str + old_length) = ' ';
	                            *(new_str + old_length + 1) = '\0';
				    strcat(new_str, words->first);
			    }
                            //printf("--> %s ", words->first);
                            words = words->rest;
                    }
		    char *argv[MAXARGS]; /* Argument list execve() */
		    parseline(new_str, argv); /* Initializing argv */
		    local_cmd_count++;
		
		    //printf("argv 0  %s\n", argv[0]);
		    //printf("new_str  %s\n", new_str);
		    // Enabling flag for last command in the list for output redirection
		    if(local_cmd_count != cmd_count) {
			    pipe(new_fds);
		    }

		    if ((cmd_pid[local_cmd_count-1] = fork()) == 0) {			 		// Initializing forks for each commands and running execvp

			//sigprocmask(SIG_SETMASK, &prev_one, NULL); 			/* Unblock SIGCHLD */
			// Handling process groupid
	                setpgid(0, pgid);
			// Handling input and out put redirection
			if(local_cmd_count == 1) {
				//if '<' char was found in string inputted by user
				if(in != NULL) {
						// fdo is file-descriptor
						int fd0;
						if ((fd0 = open(in, O_RDONLY, 0)) < 0) {
								perror("Couldn't open input file");
								exit(1);
						}
						// dup2() copies content of fdo in input of preceeding file
						dup2(fd0, 0); // STDIN_FILENO here can be replaced by 0
						close(fd0); // necessary
				}

			}
			else{
				dup2(old_fds[0], 0);
			        close(old_fds[0]);
			        close(old_fds[1]);
			}
		
			// If last command
			if( local_cmd_count == cmd_count ) {
				//close( pipefds[(local_cmd_count-1)*2] );
				//if '>' char was found in string inputted by user
				if (out != NULL)
				{

					int fd1 ;
					if ((fd1 = open(out , O_CREAT|O_WRONLY ,0644)) < 0) {
						perror("Couldn't open the output file");
						exit(1);
					}
					dup2(fd1, 1); // 1 here can be replaced by STDOUT_FILENO
					close(fd1);
				}	
			}
			else{
				close(new_fds[0]);
				dup2(new_fds[1], 1);
			        close(new_fds[1]);
			}
			
			if( execvp(argv[0], argv) < 0)
			{
				printf("execvp failed: No such file or directory\n");
				abort();
			}
		    }
		    else if(cmd_pid[local_cmd_count-1] < 0){
         		   perror("command error");
			   //abort();
		           exit(EXIT_FAILURE);
	            }
		    if(local_cmd_count != 1){
		         close(old_fds[0]);
           		 close(old_fds[1]);
		    }
	            if(local_cmd_count != cmd_count) {
		         old_fds[0] = new_fds[0];
			 old_fds[1] = new_fds[1];
		    }

		    free(new_str);
                    command_list = command_list->rest;
            }

	    /* parent closes all of its copies at the end */
	    if(cmd_count > 1){
		    close(old_fds[0]);
		    close(old_fds[1]);
	    }

	    //sigprocmask(SIG_SETMASK, &prev_one, NULL); /* Unblock SIGCHLD Parent */
	    // reap all child processes from master and return all commands are reaped
	    int child_status, exitStatus;
	    for (int i = 0; i < cmd_count; i++) { /* Parent */
		pid_t wpid;
		//wpid = waitpid(-1, &child_status, 0);
		wpid = wait(&child_status);
		if(wpid <= -1 && errno != ECHILD){
                    perror("waitpid error: ");
		    exit(EXIT_FAILURE);
		}
		else if( valueinarray(wpid, cmd_pid, cmd_count) == 1 ){
			if (WIFEXITED(child_status)) {
                        //printf("Child %d terminated with exit status %d  numer of process %d\n",
                               //wpid, WEXITSTATUS(child_status), i);
			exitStatus = WEXITSTATUS(child_status);
                    }
                    else{
			    if(WIFSIGNALED(child_status)){
                        	abort();
			    }
                    }
		}
		else if(wpid == 0){
			i++;
		}
	    }
	    //printf("exited for loop\n");
	    /*for(int l = 0; l < cmd_count; l++) {
		    printf("\n------ Child PIDS are %d\n", cmd_pid[l]);
	    }*/
	    exit(exitStatus);
	}
	else if(pid < 0) {
		perror("error at master pipeline fork creation");
                exit(EXIT_FAILURE);
	}
	else{

	// reap master and return
	//sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
	int child_status;
	pid_t wpid;
waitcall:
	wpid = waitpid(pid, &child_status, 0);
	if(wpid == -1){
		perror("waitpiderror master");
		exit(EXIT_FAILURE);
	}
	if(wpid == 0){
		goto waitcall;
	}
	if (WIFEXITED(child_status)){
		//printf("master pipeline process %d terminated with exit status %d\n",
		//wpid, WEXITSTATUS(child_status));
		exit_status_last_cmd = WEXITSTATUS(child_status);
	}
	else{
		if(WIFSIGNALED(child_status)){
                        //printf("Child %d terminated abnormally\n", wpid);
                        return -1;
                }

	}
	}
	return exit_status_last_cmd;

}


void sigchild_handler(int signum) {
	SIGCHILD_FLAG = 1;
}

static int pid_jobid(pid_t pid)
{
	for(int i = 0; i < MAX_JOBS; i++)
        {
		if( job_list[i].filled != 0 )
		{
			if( job_list[i].pid == pid )
			{
				return i;
			}
		}
	}
	return -1;
}


static int change_status(int new_jobid, JOB_STATUS prev_state, JOB_STATUS new_state) {
        if(job_list[new_jobid].state == prev_state) {
                job_list[new_jobid].state = new_state;

                printf("job %d status change: %s -> %s\n", new_jobid, job_status_names[prev_state], job_status_names[new_state]);

		// Calling sf
                sf_job_status_change(new_jobid, prev_state, new_state);

                return 0;
        }
        return -1;
}


int signal_handler() {
        int olderrno = errno;

	//sigset_t mask, prev_mask;
	//sigfillset(&mask);

	if( SIGCHILD_FLAG == 1) {
        pid_t pid;
	int status;

	while( (pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) // reap  all zombies
	{
		//sigprocmask(SIG_BLOCK, &mask, &prev_mask);
	    	if(WIFEXITED(status))
		{
		    int jobid = pid_jobid(pid);
		    if(jobid == -1){
			    printf("no such job\n");
			    continue;
		    }
                    //printf("\nNormal JOBID is:  %d  pid %d\n", jobid, pid);
	                    change_status(jobid, job_list[jobid].state, COMPLETED);
			    job_list[jobid].exit_status = WEXITSTATUS(status);

			    // Calling sf
			    sf_job_end(jobid, job_list[jobid].pgid,  job_list[jobid].exit_status);
		    RUNNERS--;
		    	    
		}

		else if(WIFSIGNALED(status))
		{
		    int jobid = pid_jobid(pid);
                    if(jobid == -1){
                            printf("no such job\n");
			    continue;
                    }
                    //printf("\n ABORTED JOBID is:  %d  %d\n", jobid, pid);

		    change_status(jobid, job_list[jobid].state, ABORTED);

		    job_list[jobid].exit_status = WTERMSIG(status);

		    // Calling sf
                    sf_job_end(jobid, job_list[jobid].pgid,  job_list[jobid].exit_status);

		    RUNNERS--;

			    //job_list[jobid].exit_status = WEXITSTATUS(status);
		}
		if(WIFCONTINUED(status))
		{
		    int jobid = pid_jobid(pid);
                    if(jobid == -1){
                            printf("no such job\n");
			    continue;
                    }
                    
		    //change_status(jobid, job_list[jobid].state, RUNNING);

		    // Calling sf
                    sf_job_resume(jobid, job_list[jobid].pgid );


		}
	    	if(WIFSTOPPED(status))
		{
		    int jobid = pid_jobid(pid);
                    if(jobid == -1){
                            printf("no such job\n");
			    continue;
                    }
		    
                    //change_status(jobid, job_list[jobid].state, PAUSED);

		    // Calling sf
		    sf_job_pause(jobid, job_list[jobid].pgid );


		}
	    //sigprocmask(SIG_SETMASK, &prev_mask, NULL);
	}
    	/*if (errno != ECHILD) {
    		if(write(1, "\nwaitpid error No child process\n", strlen("\nwaitpid error No child process\n")+1) < 0)
			printf("waitpid unix error");
	}*/

	}

	if(RUNNER_PROCESS_RUNNING == 0) {
                RUNNER_PROCESS_RUNNING = 1;
                runner_process();
                RUNNER_PROCESS_RUNNING = 0;
        }
	SIGCHILD_FLAG = 0;
        errno = olderrno;
	//sigprocmask(SIG_SETMASK, &prev_mask, NULL);
	return 0;
}

void runner_process() {
	int pid;
	if(ENABLED){

	sigset_t master_mask_all, master_mask_child, master_prev_one;
        sigfillset(&master_mask_all);
        sigemptyset(&master_mask_child);
        sigaddset(&master_mask_child, SIGCHLD);
        signal(SIGCHLD, sigchild_handler);
	//signal(SIGKILL, sigkill_handler);
        sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */

	for(int i = 0; i < MAX_JOBS; i++)
	{
	if(job_list[i].state == WAITING){

	if(RUNNERS < MAX_RUNNERS) {
		/*
		sigset_t master_mask_all, master_mask_child, master_prev_one;
	        sigfillset(&master_mask_all);
	        sigemptyset(&master_mask_child);
        	sigaddset(&master_mask_child, SIGCHLD);
		signal(SIGCHLD, sigchild_handler);
	        sigprocmask(SIG_BLOCK, &master_mask_child, &master_prev_one); // Block SIGCHLD 
		*/
		//printf("\nMAIN_RUNNER_PROCESS %d Jobid %d\n", (int) getpid(),  i);
		char* temp_ptr = job_list[i].cmd;
		TASK *task_generated = parse_task(&temp_ptr);
                PIPELINE_LIST* tasks = task_generated->pipelines;
	
		if ((pid = fork()) == 0) {
		    sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
		int curr_pid = (int) getpid();
		    // Handling process groupid
	            setpgid(0, curr_pid);
		    int exit_status;
		    //printf("1111  %d    %d", curr_pid, pipeline_count);
		    while( tasks != NULL ){
		            COMMAND_LIST* command_list = tasks->first->commands;
			    exit_status = create_master_pipeline( command_list, tasks->first->input_path, tasks->first->output_path, curr_pid ); // Calling Master_pipeline_process
			    //printf("\n--------- pipelinecount: %d  exit_status: %d -----\n", ++pipeline_count, exit_status);
			    if(exit_status == -1)
				    abort();
			    tasks = tasks->rest;
		    }
         	    //printf("Done %d Jobid %d\n", (int) getpid(), i);
        	    exit(exit_status);  /* runner process exits */
        	}
		else if(pid < 0){
			perror("Fork Error");
			abort();
		}
		RUNNERS++;
		//printf("-\n");
		free_task(task_generated);
		//printf("--\n");
                update_pid_pgid(pid, &job_list[i]);
		//printf("---\n");
                change_status(i, WAITING, RUNNING);
		//printf("----\n");
		sf_job_start(i, job_list[i].pgid);
    	}
	else{
		//printf("Runners are full");
		//RUNNER_PROCESS_RUNNING = 0;
		//sigprocmask(SIG_SETMASK, &master_mask_child, NULL);       /* Unblock SIGCHLD */
		sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
		return;
	}
	}
	}
	sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
	//sigprocmask(SIG_SETMASK, &master_mask_child, NULL);       /* Unblock SIGCHLD */
	}
	return;
}

