/*
 * Job manager for "jobber".
 */

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


/* Global Variables */
volatile sig_atomic_t ENABLED = 0;
volatile int RUNNERS = 0;
volatile int RUNNER_PROCESS_RUNNING = 0;
volatile int cmd_count = 0;


/* Some Helper Func
 * #include <string.h>tions */

static void print_job(job_table* job, int jobid) {
	printf("job %d [%s (canceled: %d)]: %s", jobid, job_status_names[job->state], job->canceled, job->cmd);
	printf("\n");
	return;
}


static void clear_list(job_table* job){
	job->filled = 0;
	job->pid = 0;
	job->pgid = 0;
	job->state = NEW;
	job->canceled = 0;
	job->exit_status = 0;
	job->cmd = NULL;
}

static void add_job(job_table* job, char* command) {
        job->filled = 1;
        job->pid = 0;
        job->pgid = 0;
        job->state = NEW;
	job->canceled = 0;
	job->exit_status = 0;
	job->cmd = command;
}


static int push_job(char* command) {
	int asigned_flag = -1;
	for (int i = 0; i < MAX_JOBS; i++) {
                if( job_list[i].filled == 0 ) {
			add_job(&job_list[i], command);
			asigned_flag = i;
			debug("job %d created\n", asigned_flag);
			break;
		}
	}
	return asigned_flag;
}


static int change_status(int new_jobid, JOB_STATUS prev_state, JOB_STATUS new_state) {
	if(job_list[new_jobid].state == prev_state) {
		job_list[new_jobid].state = new_state;

		debug("job %d status change: %s -> %s\n", new_jobid, job_status_names[prev_state], job_status_names[new_state]);

		// Calling sf
		sf_job_status_change(new_jobid, prev_state, new_state);

		return 0;
	}
	return -1;
}


static int set_canceled(int jobid) {
	if( job_list[jobid].canceled == 0 ) {
		job_list[jobid].canceled = 1;
		return 1;
	}
	return 0;
}



/* Given functions to be implemented */
int jobs_init(void) {

	for (int i = 0; i < MAX_JOBS; i++)
		clear_list(&job_list[i]);
	return 0;
}

void jobs_fini(void) {
	quit_expunge();
}

int jobs_set_enabled(int val) {
    // Enabling if 1
    int prev_flag = jobs_get_enabled();
    if(val == 0){
	    if( prev_flag!=0 )
                ENABLED = 0;
    }
    else{
	    if( prev_flag==0 )
                ENABLED = 1;
	    RUNNER_PROCESS_RUNNING = 1;
	    runner_process();
	    RUNNER_PROCESS_RUNNING = 0;
    }
    return prev_flag;
}

int jobs_get_enabled() {
	return ENABLED;   
}


int job_create(char *command) {
    char* command_start = command;
    TASK *task_generated = parse_task(&command);
    if( task_generated == NULL )
	return -1;
    else{
	    //debug("Task: ");
	    //unparse_task(task_generated, stdout);
	    //debug("\n");
	    free_task(task_generated);
    }
    int new_jobid = push_job(command_start);
    if(new_jobid == -1)
	    return -1;

    // Calling sf
    sf_job_create(new_jobid);

    int waiting_ret = change_status(new_jobid, NEW, WAITING);

    if(waiting_ret == -1)
	    return -1;


    RUNNER_PROCESS_RUNNING = 1;
    runner_process();
    RUNNER_PROCESS_RUNNING = 0;
    /*
    // Related to runner process when state changed from Waiting to running
    PIPELINE_LIST* tasks = task_generated->pipelines;
    while( tasks != NULL ){
	    debug("input_path:  %s %s \n", tasks->first->input_path, tasks->first->output_path );
	    COMMAND_LIST* command_list = tasks->first->commands;
	    debug("Pipeline\n");
	    while(command_list != NULL){
		    WORD_LIST* words = command_list->first->words;
		    debug("Command\n");
		    while( words != NULL){
			    debug("--> %s ", words->first);
			    words = words->rest;
		    }
		    debug("\n");
		    command_list = command_list->rest;
	    }
	    debug("\n");
	    tasks = tasks->rest;
    }
    */

    return new_jobid;
}

int job_expunge(int jobid) {
        if( job_list[jobid].filled != 0 ){
                if( (job_list[jobid].state == COMPLETED) || (job_list[jobid].state == ABORTED) ) {
			// Freeing commands
			free(job_list[jobid].cmd);
                	clear_list(&job_list[jobid]);

			// Calling sf
			sf_job_expunge(jobid);
			return 0;
		}
        }
	// Calling sf
	sf_job_expunge(jobid);
        return -1;
}

int job_cancel(int jobid) {
	// Race condition handling
	if( job_list[jobid].filled != 0 ){

		sigset_t master_mask_all, master_prev_one;
                sigfillset(&master_mask_all);
                sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */

		JOB_STATUS status = job_list[jobid].state;
                if( (status == PAUSED) || (status == WAITING) || (status == RUNNING) ) {
			if( (status == PAUSED) || (status == RUNNING) ){
				if( status == PAUSED  ){
					change_status(jobid, PAUSED, CANCELED);
				}
				else{
					change_status(jobid, RUNNING, CANCELED);
				}

				set_canceled(jobid);
				sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */

				// Handle race condition killpg()
				if( killpg(job_list[jobid].pgid, SIGKILL) < 0 ){
					return -1;
				}
			}
			else{
				change_status(jobid, WAITING, ABORTED);
				sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
			}

                        return 0;
                }
		sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
        }
        return -1;
}

int job_pause(int jobid) {
    // TO BE IMPLEMENTED Race condition handling
    	if( job_list[jobid].filled != 0 ){

                sigset_t master_mask_all, master_prev_one;
                sigfillset(&master_mask_all);
                sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */

                if( job_list[jobid].state == RUNNING ) {
                        change_status(jobid, RUNNING, PAUSED);

                        // Handle race condition killpg()
			sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
			if( killpg(job_list[jobid].pgid, SIGSTOP) < 0 ){
                                        return -1;
                        }
			return 0;
                }
		sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
        }
        return -1;

}

int job_resume(int jobid) {
    // TO BE IMPLEMENTED Race condition handling
        if( job_list[jobid].filled != 0 ){

                sigset_t master_mask_all, master_prev_one;
                sigfillset(&master_mask_all);
                sigprocmask(SIG_BLOCK, &master_mask_all, &master_prev_one); /* Block SIGCHLD */

                if( job_list[jobid].state ==  PAUSED) {
                        change_status(jobid, PAUSED, RUNNING);


                        // Handle race condition killpg()
			sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
			if( killpg(job_list[jobid].pgid, SIGCONT) < 0 ){
                                        return -1;
                        }
			return 0;
                }
		sigprocmask(SIG_SETMASK, &master_prev_one, NULL);       /* Unblock SIGCHLD */
        }
        return -1;
}

int job_get_pgid(int jobid) {
	if(jobid < 8){
                if( job_list[jobid].filled != 0 ){
                        if( (job_list[jobid].state == RUNNING) || (job_list[jobid].state == CANCELED) || (job_list[jobid].state == PAUSED) )
                                return job_list[jobid].pgid;
                }
        }

        return -1;
}

JOB_STATUS job_get_status(int jobid) {
	if(jobid < 8){
		if( job_list[jobid].filled != 0 ){
			debug("jobid %d, filled %d\n", jobid, job_list[jobid].filled);
			print_job(&job_list[jobid], jobid);
			return job_list[jobid].state;
		}
	}
	return -1;
}

int job_get_result(int jobid) {
	if(jobid < 8){
                if( job_list[jobid].filled != 0 ){
			if( job_list[jobid].state == COMPLETED )
                       		return job_list[jobid].exit_status;
		}
	}
      
	return -1;
}

int job_was_canceled(int jobid) {
	if(jobid < 8){
                if( job_list[jobid].filled != 0 ){
			return job_list[jobid].canceled;
		}
	}
	return 0;
}

char *job_get_taskspec(int jobid) {
	if(jobid < 8){
                if( job_list[jobid].filled != 0 ){
                        return job_list[jobid].cmd;
                }
        }
        return 0;
}
