#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "defines.h"

/*
 * "Jobber" job spooler.
 */

int BATCHMODE = 0;
int main(int argc, char *argv[])
{
	//dup2(1,2);

	// TO BE IMPLEMENTED (remove comments after finishing
	/* These are the ones you will need to implement */
	//Signal(SIGINT,  sigint_handler);   /* ctrl-c */
	//Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	//Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
	//Signal(SIGTTIN, SIG_IGN);
	//Signal(SIGTTOU, SIG_IGN);

	/* This one provides a clean way to kill the shell */
	//Signal(SIGQUIT, sigquit_handler); 

	// Initialize the job table list
	int val;
	if( (val = jobs_init()) != 0)
		abort();

	signal(SIGCHLD, sigchild_handler);
	// Handling Batchmode by reading input

    	char prompt[] = "jobber> ";
	signal_hook_func_t signal_handler;
	signal_hook_func_t *func = &signal_handler;
	sf_set_readline_signal_hook( func );

	/*
	char* batchfile = NULL;

	if(argc > 2)
	{
		char error_message[30] = "An error has occurred\n";
		write(STDERR_FILENO, error_message, strlen(error_message));
		exit(1);
	}
	else if(argc==2)
	{
	  BATCHMODE = 1;	        
	  batchfile = argv[1];
	}

	FILE *fd;
	if(BATCHMODE == 1)
	{

	}
	else
	*/
	while(1) {
		
		// printing prompt and reading using sf_readline form cmdline
		char* response = sf_readline(prompt);
		if( response == NULL ) {
			jobs_fini();
			free(response);
			exit(EXIT_SUCCESS);
                }
		/*if(strcmp(response, "\n") == 0) {
			debug("new line only in response\n");
                        free(response);
                        exit(EXIT_SUCCESS);
                }
		*/
		response = trim(response);
		if( strcmp(response, "") == 0) {
			free(response);
			continue;
		}
		
  		char *token, *first_word;
		int first_flag = 0, arg_no = 0, length = 0;
		//remove extra spaces at the end use strlen strncpy by finding the length
		token = response;
		while(*token != '\0'){
			length++;
			if(*token == ' '){
				arg_no++;
				if(first_flag == 0){
					first_flag = 1;
					first_word = (char *)malloc(length);
					strncpy ( first_word, response, length-1 );
					*(first_word + length-1) = '\0';
					//printf ("firstword:  %s  %ld\n", first_word, strlen(first_word));
				}
			}
			token++;
		}

		if(arg_no == 0 && first_flag == 0){
			first_word = (char *)malloc(length+1);
                        strncpy ( first_word, response, length );
                        *(first_word + length) = '\0';
                        //printf ("firstword:  %s  %ld\n", first_word, strlen(first_word));
		}

		// Command Switch Started
		if (strcmp(first_word, "help")==0) {
 		     print_help();
		}
		// print all the jobs in the list
		else if (strcmp(first_word, "jobs")==0) {
		     if(arg_no != 0){
			     printf("Wrong number of args (given: %d, required: %d) for command 'jobs'\n", arg_no, 0);
			     free(response);
			     continue;
		     }
		     print_job_list();
		}
		// Quit expunge all jobs 
		else if (strcmp(first_word, "quit")==0) {
		     int ret_quit = quit_expunge();
		     if(ret_quit == 0){
			     free(first_word);
			     free(response);
			     exit(EXIT_SUCCESS);
		     }
		     else{
			     printf("Error Quit\n");
		     }
                }
		// Print the Status of the job
		else if (strcmp(first_word, "status")==0) {
		     if(arg_no != 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'status'\n", arg_no, 1);
			     free(response);
                             continue;
                     }

		     if( digits_only( response + 7 )==0 ){
                             printf("Error: resume");
                             free(response);
                             continue;
                     }

		     int job_id = atoi( response + 7 );
		     if( job_id < 8 ){
		     	int ret = job_get_status(job_id);
		     	if( ret == -1);
                     }
		     else {
			     printf("max jobid exceeded\n");
		     }
		}
		// Enable the runner process if not so that processes in waiting can run
		else if (strcmp(first_word, "enable")==0) {
		     if(arg_no != 0){
                             printf("Wrong number of args (given: %d, required: %d) for command 'enable'\n", arg_no, 0);
			     free(response);
                             continue;
                     }
		     jobs_set_enabled(1);			// returns prev_setting_flag
                }
		// Disable the runner processes
		else if (strcmp(first_word, "disable")==0) {
                     if(arg_no != 0){
                             printf("Wrong number of args (given: %d, required: %d) for command 'disable'\n", arg_no, 0);
			     free(response);
                             continue;
                     }
                     jobs_set_enabled(0);			// returns prev_setting_flag
                }
		// Create a job based on the task given (Handling arguments in different approach)
		else if (strcmp(first_word, "spool")==0) {

                     if(arg_no < 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'spool'\n", arg_no, 1);
			     free(response);
                             continue;
                     }
		     char* command = response + 6;
		     char *task_string;
		     if((char)*command == '\''){
	 	             char* temp = command+1;
	 	             int skip_flag = 0;
        		     while(*temp != '\0'){
		                    if(*temp == '\'')
                	            skip_flag = 1;
		                    temp++;
	 	             }
	        	     if(skip_flag == 0){
                     		task_string = command+1;
	            	     }
           	   	     else {
	                	*(temp-1) = '\0';
		                task_string = command+1;
        	    	     }
    		    }
		    else{
			if(arg_no > 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'spool'\n", arg_no, 1);
                             free(response);
                             continue;
                     	}
		        task_string = command;
		    }
		    char* task_new_string = (char* )malloc(strlen(task_string) + 1);
		    strcpy(task_new_string, task_string);

		     int new_pid = job_create( task_new_string );
		     if( new_pid== -1)
			     printf("Error: spool\n");
                }
		// Pausing a job by sending SIGSTOP to the group of the job Move to state PAUSED Only if RUNNING State
		else if (strcmp(first_word, "pause")==0) {
                     if(arg_no != 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'pause'\n", arg_no, 1);
			     free(response);
                             continue;
                     }

		     if( digits_only( response + 6 ) == 0 ) {
                             printf("Error: resume\n");
                             free(response);
                             continue;
		     }

                     int job_id = atoi( response + 6 );
                     if(job_id < 8){
			     int ret_pause = job_pause(job_id);
			     if( ret_pause != 0 )
			     	printf("Error: pause\n");
		     }
		     else
			     printf("max job_id exeeded\n");
                }
		// Resuming a job by sendin SIGCONT signal to the group job
                else if (strcmp(first_word, "resume")==0) {
                     if(arg_no != 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'resume'\n", arg_no, 1);
			     free(response);
                             continue;
                     }
		     if( digits_only( response + 7 )==0 ){
			     printf("Error: resume");
			     free(response);
			     continue;
		     }

                     int job_id = atoi( response + 7 );
                     if(job_id < 8){
                             if( job_resume(job_id) != 0 )
                                printf("Error: resume\n");
		     }
                     else
                             printf("max job_id exeeded\n");
                }
	        /* The user may choose to cancel a job that is in any state other than
		   CANCELED, COMPLETED, or ABORTED.
		   If the job has not already started, then its status is simply set to ABORTED.
		   If the job has already started, then the job status is set to CANCELED and
		   the runner's process group (if the job has already started) is sent a SIGKILL signal.
		   When the job subsequently terminates, then its state is changed from CANCELED
		   to COMPLETED or ABORTED, according to how termination occured.
		   Note that it is still possible for a job that has been canceled to end up in
		   the COMPLETED state, if the job completed just after the job state was set
		  to CANCELED but before the SIGKILL was delivered to the job runner's process group. */

                else if (strcmp(first_word, "cancel")==0) {
                     if(arg_no != 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'cancel'\n", arg_no, 1);
			     free(response);
                             continue;
                     }
                     if( digits_only( response + 7 )==0 ){
                             printf("Error: cancel");
			     free(response);
                             continue;
                     }

                     int job_id = atoi( response + 7 );
                     if(job_id < 8){
			     int ret_cancel = job_cancel(job_id);
                             if( ret_cancel != 0 )
                                printf("Error: cancel %d", ret_cancel);
		     }
                     else
                             printf("max job_id exeeded\n");
                }
		// a job may only be expunged if it is in the COMPLETED or ABORTED state.
                else if (strcmp(first_word, "expunge")==0) {
                     if(arg_no != 1){
                             printf("Wrong number of args (given: %d, required: %d) for command 'expunge'\n", arg_no, 1);
			     free(response);
                             continue;
                     }
                     if( digits_only( response + 8 )==0 ){
                             printf("Error: expunge");
			     free(response);
                             continue;
                     }

                     int job_id = atoi( response + 8 );
		     if( job_id < 8 ) 
		     {
			     int expunge_ret = job_expunge(job_id);
			     if(expunge_ret != 0)
                             	printf("Error: expunge\n");
		     }
                }
		// Default case with empty string and any other commands
		else
			printf("Unrecognized command: %s\n", first_word);
		
		// Freeing the response given by user
		free(first_word);
		free(response);
	}

	jobs_fini();
	return EXIT_SUCCESS;
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */
