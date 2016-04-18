#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/kthread.h>  // for threads
#include <linux/spinlock.h>
#include "mp2_given.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel module that schedules tasks based on RMS");
MODULE_AUTHOR("Mingwei Hu, Martin Liu, shashank bharadwaj");

#define PROC_ENTRY "/proc/mp2/status"
#define MAX_PROC_SIZE PAGE_SIZE
#define PROCESS_CACHE "mp2_cache"
#define DIRECTORY "mp2"
#define FILENAME "status"
#define READY 0
#define RUNNING 1
#define SLEEPING 2
#define MAX_LOAD 693
#define MAX_USER_RT_PRIO        100

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry* proc_entry;
static struct task_struct* dispatching_thread;  //the kernel thread used for calling the schedule() function
static struct kmem_cache* mp2_cache;  //the memory cache for storing the linked-list objects

/*mutex for normal protecting purpose, 
spinlock for protecting the linked-list inside the timer-interrupt*/
static DEFINE_MUTEX(process_mutex);
static DEFINE_SPINLOCK(mr_lock);

/*An augmentation for the process-control-block
Includes the task_struct with additional attributes such as timer, task_sate, and the list_head */
typedef struct mp2_task_struct_t
{
	struct task_struct* linux_task;
	unsigned long period;
	unsigned long processing_time;
	pid_t pid;
	int task_state;
	struct timer_list wakeup_timer;
	struct timeval* start;
	struct timeval* end;
	struct list_head list;
}mp2_task_obj;

/*this variable is used for recording the currently running task
will be initialized to NULL*/
mp2_task_obj* curr_running_task; 

//create the link-list head
LIST_HEAD(process_head) ;

/*When a new task tries to register, this function gets invoked to check if it can accept the job*/
static int admission_control(unsigned long period, unsigned long processing_time)
{
	mp2_task_obj *curr, *next;
	long int load=processing_time*1000/period;
	mutex_lock(&process_mutex);
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		load+=(curr->processing_time*1000/curr->period);
	}
	mutex_unlock(&process_mutex);
	if(load<MAX_LOAD) return 0;
	return -EPERM;
}

/*This is called by the kernel thread to select the job with the highest priority*/
static int select_process_from_list(void)
{
	struct sched_param sparam_high;
	struct sched_param sparam_low;

	unsigned long min= (unsigned long)(-1);
	mp2_task_obj* high=NULL;
	mp2_task_obj *curr, *next;
	// int num_ready_tasks=0;

	mutex_lock(&process_mutex);
	printk(KERN_INFO "searching in the list for for the READY task with the highest priority (shortest period).\n");
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		if(curr->task_state == READY && curr->period<min  )
		{
			high=curr;
			min=curr->period;
		}
	}
	if(high)  /*found the ready job with highest priority(compared to other ready jobs)*/
	{
		/*first check if there is job still runnning with a higher priority*/
		if(curr_running_task && curr_running_task->task_state==RUNNING && curr_running_task->period<high->period)
		{
			printk(KERN_INFO "task %d has the highest priority among those ready but lower than the currently running task" 
							"(pid: %d)--no preemption", high->pid, curr_running_task->pid);
		}
		else /*If no, the the selected job is ready to preempt other jobs */
		{
			printk(KERN_INFO "task %d has the highest priority and will preempt the current running task"
										"(if any)\n", high->pid);
			if(curr_running_task)
			{
				//preempt the currently running job
				sparam_low.sched_priority=0;
				sched_setscheduler(curr_running_task->linux_task, SCHED_NORMAL, &sparam_low);
				if(curr_running_task->task_state==RUNNING)
					curr_running_task->task_state = READY;
			}
			sparam_high.sched_priority=MAX_USER_RT_PRIO-1;
			sched_setscheduler(high->linux_task, SCHED_FIFO, &sparam_high);
			wake_up_process(high->linux_task);
			do_gettimeofday(high->start);		
			high->task_state = RUNNING;
			curr_running_task=high;
		}
	}
	else
	{
		if(curr_running_task)
		{
			/*no ready job found, then just premmpt the currently running job*/
			printk(KERN_INFO "no task is ready to run except the currently running task: process %d \n", curr_running_task->pid);
			sparam_low.sched_priority=0;
			sched_setscheduler(curr_running_task->linux_task, SCHED_NORMAL, &sparam_low);
			if(curr_running_task->task_state==RUNNING)
				curr_running_task->task_state = READY;
			curr_running_task=NULL;
		}
		else
			printk(KERN_INFO "no task is ready to run \n ");
	}
	mutex_unlock(&process_mutex);

	//set the dispatching thread to sleep
	set_current_state(TASK_UNINTERRUPTIBLE);

	return 0;
}

/*Kernel thread function, will be call when the kernel thread is waken up*/
static int kthread_dispatch(void* data)
{
	while(1)
	{
		if(kthread_should_stop())
		{
			printk(KERN_INFO "dispatching thread finished its job \n");
			return 0;			
		}
		select_process_from_list();
		schedule();
	}
	return 0;
}

/*Initializing the kernel thread by calling kthread_create()*/
static int kthread_init (void) 
{
    printk(KERN_INFO "Initiating kernel thread... \n");
    dispatching_thread = kthread_create( kthread_dispatch, NULL, "kthread");
    if(! dispatching_thread)
    {
    	return -ENOMEM;
    }

    return 0;
}

/*when a job passes the admission control, this function adds it to the linked-list
 (the lock is placed outside the caller of this function)*/
static void add_process_to_list(mp2_task_obj* task)
{
	list_add_tail(&task->list, &process_head);
	printk(KERN_INFO "process %d added\n", task->pid);
}

/*clean the linked-list, called when module is unloaded*/
static void clean_up_list(void)
{
	mp2_task_obj *curr, *next;
	printk(KERN_INFO "deleting the list using list_for_each_entry_safe\n");
	mutex_lock(&process_mutex);
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		printk(KERN_INFO "freeing this node %d", curr->pid);
		del_timer(&curr->wakeup_timer);
		kfree(curr->start);
		kfree(curr->end);
		list_del(&curr->list);
		kmem_cache_free(mp2_cache, (void*)curr);
	}
	mutex_unlock(&process_mutex);
}

/*called by the read_proc call back function when user read the status
The lock is already enforced in the calling function*/
static int get_user_info(char* buffer)
{
	mp2_task_obj *curr, *next;
	int bytes=0;
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		bytes+=sprintf(buffer, "%s%d, %lu, %lu\n", buffer, curr->pid, curr->period, curr->processing_time );
	}
	printk(KERN_INFO "returning %d bytes to user! \n", bytes);
	return bytes;
}

/*timer call-back function, will be called when the timer expires*/
static void wakeup_timer_callback( unsigned long data )
{
	unsigned long flags;
	pid_t pid = (pid_t)data;
	mp2_task_obj *curr, *next;
	spin_lock_irqsave(&mr_lock, flags);
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		if(curr->pid==pid)
		{
			curr->task_state=READY;
			printk(KERN_INFO "process %d ready to run, waiting to be scheduled ! \n", pid);
		}
	}
	spin_unlock_irqrestore(&mr_lock, flags);

	wake_up_process(dispatching_thread);
}

/*when a new process writes registration message, this function gets called to try to register the process*/
static int register_process(pid_t pid, unsigned long period, unsigned long processing_time)
{
	mp2_task_obj* newtask=NULL;
	if(!mp2_cache)
	{
		printk(KERN_ALERT "memory space not available!\n");
		return -ENOMEM;
	}
	if(admission_control(period,processing_time)<0)
	{
		printk(KERN_NOTICE "utilization load exceeded, cannot admit process %d\n", pid);
		return -EUSERS;
	}
	newtask=kmem_cache_alloc(mp2_cache, GFP_KERNEL);
	if(!newtask)
	{
		printk(KERN_ALERT "failed to alloc space for job %d!\n", pid);
		return -ENOMEM;
	}
	newtask->pid=pid;
	newtask->linux_task=find_task_by_pid(pid);
	if(!newtask->linux_task)
	{
		printk(KERN_ALERT "process %d not found in the process control block !\n", pid);
		return -ENOMEM;
	}
	newtask->period=period;
	newtask->processing_time=processing_time;
	newtask->task_state=SLEEPING;
	newtask->start=(struct timeval*)kmalloc(sizeof(struct timeval), GFP_KERNEL);
	newtask->end=(struct timeval*)kmalloc(sizeof(struct timeval), GFP_KERNEL);	
	do_gettimeofday(newtask->start);
	init_timer(&newtask->wakeup_timer );
	setup_timer(&newtask->wakeup_timer, wakeup_timer_callback, pid);

	mutex_lock(&process_mutex);
	add_process_to_list(newtask);
	mutex_unlock(&process_mutex);

	printk(KERN_INFO "process with pid %d, period %lu, and processing time %lu is registered ! \n", pid, period, processing_time);

	return 0;
}

/*when the process ends, it trys to deregister itself and this function gets called 
in the kernel space to remove the process from the linked-list*/
static int deregister_process(pid_t pid)
{
	mp2_task_obj *curr, *next;
	mutex_lock(&process_mutex);
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		if(curr->pid==pid)
		{
			if(curr_running_task==curr)
				curr_running_task=NULL;
			del_timer(&curr->wakeup_timer);
			kfree(curr->start);
			kfree(curr->end);
			list_del(&curr->list);
			kmem_cache_free(mp2_cache, (void*)curr);
			printk(KERN_INFO "process %d deregistered!\n", pid);
			mutex_unlock(&process_mutex);
			return 0;
		}
	}
	mutex_unlock(&process_mutex);
	return -ENOENT;
}

/*read_proc call back function, similar to mp1, when user reads the status,
 this function goes through the linked-list and print all the user information (pid,period,processing_time) */
static int read_proc_cb(char* buffer, 
				  char** buffer_location, off_t offset, 
				  int buffer_length, int* eof, void* data)
{
	int bytes=0;
	char* temp;
	printk(KERN_INFO "User is reading %s\n", PROC_ENTRY );
	if(offset>0) return bytes;
	temp=(char*)kmalloc(MAX_PROC_SIZE*sizeof(char), GFP_KERNEL);
	temp[0]=0;

	mutex_lock(&process_mutex);
	bytes=get_user_info(temp);
	mutex_unlock(&process_mutex);

	if(bytes>buffer_length)
	{
		printk(KERN_ALERT "Buffer capacity exceeded! \n");
		snprintf(buffer, buffer_length, "%s",temp);
		bytes=buffer_length;
	}
	else
	{
		bytes=sprintf(buffer, "%s", temp);
	}

	kfree(temp);
	return bytes;
}

/*helper function to convert a string to long int*/
static unsigned long get_int(char* info)
{
	unsigned long info_;
	if(strict_strtol(info,10,&info_)!=0)
	{
		printk(KERN_ALERT "Invalid process id found! \n");
		return -EINVAL;	
	}
	return info_; 
}

/*Parse the message user writes to the proc file system, 
and then it initialize pid, period, and processing time*/
static int get_info_from_msg(const char* msg, pid_t* pid, unsigned long* period, unsigned long* processing_time)
{
	char* temp=kmalloc(256*sizeof(char), GFP_KERNEL);
	char* pos=NULL;
	char* start=NULL;
	int i=0;
	if(!msg){
		kfree(temp);
		return -EINVAL;	
	} 
	strcpy(temp,msg);
	pos=temp+3;
	start=strchr(pos, ',');

	while(start)
	{
		(*start)=0;
		printk(KERN_INFO "%s \n", pos);
		if(i==0) (*pid)=get_int(pos);
		else if(i==1) (*period)=get_int(pos);
		else ;
		i++;
		pos=start+2;
		start=strchr(pos, ',');
	}
	printk(KERN_INFO "%s \n", pos);
	if(i==2) (*processing_time)=get_int(pos);
	else (*pid)=get_int(pos);

	kfree(temp);
	if(*pid==-EINVAL || *period==-EINVAL || *processing_time==-EINVAL) return -EINVAL;
	return 0;
}

/*when a yield message is received, this function gets called and set the process to sleep*/
static int yield_process(pid_t pid)
{
	mp2_task_obj *curr, *next;
	long int dt;
	long int offset;
	int ret;
	mutex_lock(&process_mutex);
	list_for_each_entry_safe(curr, next, &process_head, list)
	{
		if(curr->pid==pid)
		{
			curr->task_state=SLEEPING;
			printk(KERN_INFO "process %d set to sleep, waiting for next period \n", pid);
			do_gettimeofday(curr->end);
			dt=(curr->end->tv_sec*1000000+curr->end->tv_usec-(curr->start->tv_sec*1000000 + curr->start->tv_usec))/1000 ;
			printk(KERN_INFO "job %d spent %ldms during this period\n", pid, dt);
			if(dt>curr->period)
			{
				printk(KERN_ALERT "process %d's processing time exceeds the period! \n", pid);
				offset=curr->period-(dt-curr->period*(dt/curr->period)) ;
				printk(KERN_ALERT "process %d's next period begins in %ld \n", pid, offset);
				ret=mod_timer(&curr->wakeup_timer, jiffies+msecs_to_jiffies(offset) );				
			}
			else
			{
				printk(KERN_ALERT "process %d's next period begins in %ldms \n", pid, curr->period-dt);
				ret=mod_timer(&curr->wakeup_timer, jiffies+msecs_to_jiffies( curr->period - dt) );				
			}
			if(ret){
				printk(KERN_ALERT "Error in mod_timer\n");
				mutex_unlock(&process_mutex);
				return -EINVAL;
			}
			set_task_state(curr->linux_task, TASK_UNINTERRUPTIBLE );
			wake_up_process( dispatching_thread );
			mutex_unlock(&process_mutex);
			return 0;
		}
	}
	mutex_unlock(&process_mutex);

	return -ENOENT;
}

/*proc_write call back function, similar to mp1, it gets called when user writes to the proc file system;
this function first try to identify the type of the message (R,Y,D), then it calls corresponding functions*/
static int write_proc_cb(struct file* file, const char __user*  buffer, unsigned long count, void* data)
{
	char* temp=(char*)kmalloc((count+1)*sizeof(char), GFP_KERNEL);
	pid_t pid=0;
	unsigned long period=0;
	unsigned long processing_time=0;

	printk(KERN_INFO "receiving %lu bytes from user! \n", count);

	if(copy_from_user(temp, buffer, count))
	{
		return -EFAULT ;
	}
	temp[count]=0;

	printk(KERN_INFO "process is writing %s to %s \n", temp ,PROC_ENTRY);
	if(get_info_from_msg(temp, &pid, &period, &processing_time)<0)
	{
		printk(KERN_ALERT "write_proc callback failed to extract info from user !\n");
		kfree(temp);
		return -EINVAL;
	}

	switch(temp[0])
	{
		case 'R': case 'r':
		{
			printk(KERN_INFO "REGISTRATION: PROCESS %d \n",pid);
			if(register_process(pid, period, processing_time)<0)
			{
				printk(KERN_INFO "cannot register process %d \n", pid);
				return -EINVAL;
			}
			break;
		}
		case 'Y': case 'y':
		{
			printk(KERN_INFO "YIELD: PROCESS %d\n",pid);
			if(yield_process(pid)<0)
			{
				printk(KERN_ALERT "unable to find process %d to yield \n ", pid);
				return -EINVAL;
			}
			break;
		}
		case 'D': case 'd':
		{
			printk(KERN_INFO "DEREGISTRATION: PROCESS %d\n",pid);
			if(deregister_process(pid))
			{
				printk(KERN_INFO "Trying to deregister process %d, but process not found. \n", pid);
				return -ENOENT;
			}
			break;
		}
		default:
		{	
			printk(KERN_ALERT "INVALID VALUE\n");
			return -EINVAL;
		}
	}
	kfree(temp);
	return count;
}

static const struct file_operations mp2_file = 
{
	.owner      = THIS_MODULE,
	.read       = read_proc_cb,
	.write      = write_proc_cb,
};

/*Entry for mp1_module.c, called when module is inserted.
Trys to created the proc file entry and initialize the memory cache for allocating space for the linked-list*/
static int __init mp2_module_init(void)
{

	proc_dir = proc_mkdir(DIRECTORY, NULL);
	proc_entry = proc_create(FILENAME,0666, proc_dir, &mp2_file);
	
	if(kthread_init()<0)
	{
		printk(KERN_ALERT "Could not create kernel thread! \n");
		return -ENOMEM;
	}
	if (proc_entry==NULL) 
	{
		printk(KERN_ALERT "Error: could not create proc file!");
		return -ENOMEM;
	}

	mp2_cache =(struct kmem_cache*) kmem_cache_create( PROCESS_CACHE, sizeof(mp2_task_obj), 0, GFP_KERNEL ,NULL);
	curr_running_task=NULL;
	
	printk(KERN_INFO "\n MP2 Module loaded! \n");

	return 0;
}

/*Exit for mp2_module, called when rmmod mp2_module is called.
It stops the kernel thread and frees all the memory previouslt allocated*/
static void __exit mp2_module_exit(void)
{
	if(curr_running_task)
		curr_running_task=NULL;
	kthread_stop(dispatching_thread);
	clean_up_list();
	kmem_cache_destroy (mp2_cache);
	remove_proc_entry("status", proc_dir) ;
	remove_proc_entry("mp2",NULL);

	printk(KERN_INFO "\n MP2 module unloaded!\n");
}

module_init(mp2_module_init);
module_exit(mp2_module_exit);
