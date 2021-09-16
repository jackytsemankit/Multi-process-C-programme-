///////////////////////////////////////////////////////////////////////////////////////////
/*
- File name: part1b-mandelbrot.c
- Student's name: Tse Man Kit
- Student Number: 3035477757
- Date: Oct 30, 2019
- Version: 1.0
- Development platform: Ubuntu 18.04 (downloaded from class moodle page)
- Compilation:
	use the command
	gcc part1b-mandelbrot.c -o 1bmandel -l SDL2 -l m
	to generate the executable 
	then use the following command to run:
	./1bmandel [no. of child processes] [no. of rows in a task]
*/
///////////////////////////////////////////////////////////////////////////////////////////

//Using SDL2 and standard IO
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include "Mandel.h"
#include "draw.h"

//initialize task pipe as global variables
int task_pfd[2];

//initialize data pipe as global variables
int data_pfd[2];

int doneTaskCount = 0;

//data structure for pipes
typedef struct task {
	int start_row;
	int num_of_rows;
} TASK;

typedef struct message {
	int row_index;
	pid_t child_pid;
	float rowdata[IMAGE_WIDTH];
} MSG;

void SIGUSR1_Handler(int signum){
	if (signum==SIGUSR1){
		//data structure to store the start and end times of the computation
		struct timespec start_compute, end_compute;
		
		//allocate memory to store the data structures
		TASK* task = (TASK*) malloc(sizeof(TASK));
		MSG* msg = (MSG*) malloc(sizeof(MSG));

		//allocate memory to store the pixels
		float* rowOfPixel = (float*) malloc(sizeof(float) * IMAGE_WIDTH);

		//keep track of the execution time
		printf("Child(%d) : Start the computation ...\n", getpid());
		clock_gettime(CLOCK_MONOTONIC, &start_compute);	

		//child performs a pipe read to get the tasks they are assigned to perform
		read(task_pfd[0], task, sizeof(TASK));
		//children processes calculate the rows that they are assigned to work on
		for (int y=task->start_row; y<task->start_row + task->num_of_rows && y<IMAGE_HEIGHT; y++) {
			for (int x=0; x<IMAGE_WIDTH; x++) {
				//compute a value for each point c (x, y) in the complex plane
				rowOfPixel[x] = Mandelbrot(x, y);
			}

			//write the process data to the data pipe
			msg->row_index = y;
			for (int x=0; x<IMAGE_WIDTH; x++)
				msg->rowdata[x] = rowOfPixel[x];
				//if the child finishes the rows that they are assigned to work on,msg->child_pid =getpid(), else it is 0
				if (y == task->start_row + task->num_of_rows-1 || y == IMAGE_HEIGHT-1){
					msg->child_pid =getpid();
				}
				else{
					msg->child_pid = 0;
				}
			write(data_pfd[1], msg, sizeof(MSG));
	   	}

		//Report timing
		clock_gettime(CLOCK_MONOTONIC, &end_compute);
		float difftime = (end_compute.tv_nsec - start_compute.tv_nsec)/1000000.0 + (end_compute.tv_sec - start_compute.tv_sec)*1000.0;
		printf("Child(%d) : ... completed. Elapse time = %.3f ms\n", getpid(), difftime);
		
		doneTaskCount++;
	}
}

void SIGINT_Handler(int signum){
	if (signum==SIGINT){
		//terminate process
		printf("Process %d is interrupted by ^C. Bye Bye\n", getpid());	
		exit(doneTaskCount);
	}
}

int main( int argc, char* args[] )
{	
	//false if no data read is performed by the parent yet
	bool firstRead=false;
	//data structure to store the start and end times of the whole program
	struct timespec start_time, end_time;
	//get the start time
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	//generate mandelbrot image and store each pixel for later display
	//each pixel is represented as a value in the range of [0,1]
	
	//store the 2D image as a linear array of pixels (in row-major format)
	float * pixels;
	
	//allocate memory to store the pixels
	pixels = (float *) malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT);
	if (pixels == NULL) {
		printf("Out of memory!!\n");
		exit(1);
	}
	

	//get command line arguments
	int numberOfChildren = atoi(args[1]);
	int rowsPerTask = atoi(args[2]);
	
	//pipe
	pipe(task_pfd);
	pipe(data_pfd);

	//assign signal handlers to signals
	signal(SIGUSR1, SIGUSR1_Handler);
	signal(SIGINT, SIGINT_Handler);

	//store pids of workers in a dynamic array for later usage related to their pids
	pid_t* pid = (pid_t*) malloc(sizeof(pid_t) * numberOfChildren);
	for (int i=0; i<numberOfChildren; i++)
		pid[i] = 0;

	//create worker processes one after another
	pid[0] = fork();
	for (int i=0; i<numberOfChildren-1; i++){
		if (pid[i] > 0) {
			pid[i+1] = fork();
		}
	}

	//in child
	if (pid[numberOfChildren-1]== 0){
		printf("Child(%d) : Start up. Wait for task!\n", getpid());
		while (1){
			//wait for SIGUSR1
			pause();
		}
	}

	//in parent
	if (pid[numberOfChildren-1] > 0){		

		TASK* task = (TASK*) malloc(sizeof(TASK));
		MSG* msg = (MSG*) malloc(sizeof(MSG));
		int finished_row_count = 0;		

		task->start_row = 0;
		task->num_of_rows = rowsPerTask;

		//assign tasks to workers, send them a SIGUSR1 signal
		for (int i=0; i<numberOfChildren; i++){
			write(task_pfd[1], task, sizeof(TASK));
			kill(pid[i], SIGUSR1);
			task->start_row += rowsPerTask;
		}
		
		//read data from Data pipe until all results return
		while (finished_row_count < IMAGE_HEIGHT){
			read(data_pfd[0], msg, sizeof(MSG));
			if (firstRead==false){
				printf("Start collecting the image lines\n");
				firstRead=true;
			}
			for (int x=0; x<IMAGE_WIDTH; x++)
				pixels[msg->row_index*IMAGE_WIDTH+x] = msg->rowdata[x];
			//if the child is idling and the not all pixels are processed, assignment work, send a SIGUSR1 signal
			if (msg->child_pid != 0 && task->start_row < IMAGE_HEIGHT){
				write(task_pfd[1], task, sizeof(TASK));
				kill(msg->child_pid, SIGUSR1);
				task->start_row += rowsPerTask;
			}
			finished_row_count++;
		}
		
		//when work is done for all pixels, terminate all child and
		int status;
		int* no_of_task_done = (int*) malloc(sizeof(int) * numberOfChildren);
		//collect children process info.(number of task they have done)
		for (int i=0; i<numberOfChildren; i++){
			kill(pid[i], SIGINT);
			waitpid(pid[i], &status, 0);
			no_of_task_done[i] = WEXITSTATUS(status);
		}
		for (int i=0; i<numberOfChildren; i++){
			printf("Child process %d terminated and completed %d tasks\n", pid[i], no_of_task_done[i]);
		}
		printf("All Child processes have completed\n");

		//Display CPU resource usage statistics
		struct rusage children_usage, parent_usage;
		getrusage(RUSAGE_CHILDREN, &children_usage);
		getrusage(RUSAGE_SELF, &parent_usage);
		printf("Total time spent by all child processes in user mode = %.3f ms\n", children_usage.ru_utime.tv_sec*1000.0 + children_usage.ru_utime.tv_usec/1000.0);
		printf("Total time spent by all child processes in system mode = %.3f ms\n", children_usage.ru_stime.tv_sec*1000.0 + children_usage.ru_stime.tv_usec/1000.0);
		printf("Total time spent by parent process in user mode = %.3f ms\n", parent_usage.ru_utime.tv_sec*1000.0 + parent_usage.ru_utime.tv_usec/1000.0);
		printf("Total time spent by parent process in system mode = %.3f ms\n", parent_usage.ru_stime.tv_sec*1000.0 + parent_usage.ru_stime.tv_usec/1000.0);

		//Display timings
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		float difftime = (end_time.tv_nsec - start_time.tv_nsec)/1000000.0 + (end_time.tv_sec - start_time.tv_sec)*1000.0;
		printf("Total elapse time measured by parent process = %.3f ms\n", difftime);

		printf("Draw the image\n");
		//Draw the image by using the SDL2 library
		DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);
	}

	return 0;
}