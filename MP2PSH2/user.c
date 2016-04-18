/** @file mp2_user_app.c */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>

#define MIN_RATIO 2
#define MIN_NUM_IT 5
#define MAX_NUM_IT 100
#define MP2_DIR "/proc/mp2/status"
#define FACT_IT 1000000
 
//doing a simple factorial
int factorial_n(int n)
{
	if(n<=1) return 1;
	return n*factorial_n(n-1);
}

//the real job, doing a series of factorial
void do_job(void)
{
	int i, res;
	for(i=0; i<FACT_IT ; i++)
	{
		res=factorial_n(i%20);
	}
}

//function that estimates the processing time of a job
unsigned long get_computation_time(const long int it)
{
	long int jobs=it;
	clock_t t=clock();
	while(jobs>0)
	{
		do_job();
		jobs--;
	}
	// printf("%lu\n", clock() - t );
	t = (clock()-t)*1000/CLOCKS_PER_SEC ;
	// printf("%lu\n", t );
	return t/it;
}

//user space register function, writes "R, <PID>, <PERIOD>, <COMPUTATION>" to the proc file entry
int register_process( int pid, unsigned long period, unsigned long jobProcessingTime )
{
	char* command=NULL;
	if(asprintf(&command, "echo \"R, %d, %lu, %lu\" > %s", pid, period, jobProcessingTime, MP2_DIR )<0)
	{
		fprintf(stderr, "failed to register process %d\n", pid );
		exit(1);
	}
	system(command);
 	return 0;
}

//Writes "Y, PID" to proc fie entry
int yield_process(int pid)
{
	char* command=NULL;
	if(asprintf(&command, "echo \"Y, %d\" > %s", pid, MP2_DIR )<0)
	{
		fprintf(stderr, "failed to yield process %d\n", pid );
		exit(1);
	}
	system(command);

 	return 0;
}

//Writes "D, PID" to proc file entry
int deregister_process(int pid)
{
	char* command=NULL;
	if(asprintf(&command, "echo \"D, %d\" > %s", pid, MP2_DIR )<0)
	{
		fprintf(stderr, "failed to deregister process %d\n", pid );
		exit(1);
	}
	system(command);

 	return 0;	
}

/*this function is not called, just for testing purpose*/
char* get_admitted_list(void)
{
	FILE* pFile;
	pFile=fopen(MP2_DIR, "r");
	char* buf;
	long lSize;
	if (pFile==NULL) 
	{
		fputs ("File error",stderr); 
		exit (1);
	}
	fseek (pFile , 0 , SEEK_END);
  	lSize = ftell (pFile);
  	rewind (pFile);
  	buf=(char*)malloc((lSize+1)*sizeof(char));
  	if(fread(buf, 1, lSize, pFile)!=lSize)
  	{
		fprintf(stderr, "Error occurs when reading %s ! \n", MP2_DIR );
  		exit(1);
  	}
  	buf[lSize]=0;
	fclose(pFile);
	return buf;
}

/*check one line and see if the pid is there*/
int check_status(const char* buf, pid_t pid)
{
	char* pos=NULL;
	char* temp=NULL;
	int    id;
	asprintf(&temp, "%s" , buf);
	pos=strchr(temp,',');
	(*pos)=0;
	pos=NULL;
	id=atoi(temp);
	if(temp)
		free(temp);

	if(id==pid) {
       	// printf("found pid %d \n", pid);
		return 1;
	}
	return 0;
}

/*Go through the list returned by the module and check if the process is admitted
in addition, it prints all the processes that are admitted*/
int process_in_list(pid_t pid)
{
   FILE *fp;
   char *line = NULL;
   size_t len = 0;
   ssize_t read;
   fp = fopen(MP2_DIR, "r");
   if (fp == NULL)
   {
	   fprintf(stderr, "Error occurs when reading %s ! \n", MP2_DIR );
       exit(EXIT_FAILURE);
   }
   while ((read = getline(&line, &len, fp)) != -1) {
       // printf("Retrieved line of length %zu :\n", read);
       printf("%s", line);
       if(check_status(line, pid)==1)
       {
       		printf("Process %d passed the admission control!\n", pid);
       		free(line);
       		return 1;
       }
   }
   if(line)
   		free(line);
   return 0;
}

int main(int argc, char *argv[])
{
	pid_t pid=getpid();
	unsigned long period;
	unsigned long computation;
	long int start_time;
	long int up_time;
	long int complete_time;
	long int num_it;
	long int i;
	struct timeval* t0;
	struct timeval* wakeup_time;
	struct timeval* finish_time;

	if(argc != 3 )
	{
		fprintf(stderr, " usage: %s <period> <numOfJobs>\n", argv[0] );
		exit(EXIT_FAILURE);
	}

	t0=(struct timeval*)malloc(sizeof(struct timeval));
	wakeup_time=(struct timeval*)malloc(sizeof(struct timeval));
	finish_time=(struct timeval*)malloc(sizeof(struct timeval));

	memset(t0,          0, sizeof(struct timeval));
	memset(wakeup_time, 0, sizeof(struct timeval));
	memset(finish_time, 0, sizeof(struct timeval));

	period = strtoul(argv[1], NULL, 10);
	num_it = strtol(argv[2], NULL, 10);
	if(period<=0 || num_it<=0)
	{
		fprintf(stderr, "Invalid input ! \n" );
		free(t0);
		free(wakeup_time);
		free(finish_time);
		exit(EXIT_FAILURE);
	}
	// srand (time(NULL));
	// comprand = rand() % 10 + 1;
	if(num_it>MAX_NUM_IT) num_it=MAX_NUM_IT;
	if(num_it<MIN_NUM_IT) num_it=MIN_NUM_IT;
	computation = get_computation_time(num_it);
	if(period<MIN_RATIO*computation )
		period=MIN_RATIO*computation;

	printf("process %d : computation time is %lums and period is %lums \n", pid ,computation, period );

	register_process(pid, period, computation);

	if(process_in_list(pid)==0)
	{
		printf("process %d does not pass admission control!\n", pid);
		free(t0);
		free(wakeup_time);
		free(finish_time);
		exit(EXIT_FAILURE);
	}
	i=num_it;
	gettimeofday(t0,NULL);
	start_time=t0->tv_sec*1000000 + t0->tv_usec;
	yield_process(pid);

	/*entry point for doing the job, one iteration = one job
	after finishing the job the yield function is called */

	while(i>0)
	{
		gettimeofday(wakeup_time, NULL);
		up_time=wakeup_time->tv_sec*1000000+wakeup_time->tv_usec ; 
		printf("process %d's job#%ld wakes up at %ldms\n", pid, num_it-i, (up_time - start_time )/1000 );
	
		do_job();

		gettimeofday(finish_time, NULL);
		complete_time=finish_time->tv_sec*1000000+finish_time->tv_usec ; 
		printf("process %d's job#%ld spent %ldms during computation\n", pid, num_it-i, (complete_time - up_time )/1000 );
		// printf("process %d's job#%ld finishes at %ldms and it spent %ldms\n", pid, num_it-i, 
		// 		(complete_time - start_time)/1000,(complete_time - up_time )/1000 );
	
		yield_process(pid);	
		i--;	
	}

	deregister_process(pid);
	free(t0);
	free(wakeup_time);
	free(finish_time);
	return 0;
}
