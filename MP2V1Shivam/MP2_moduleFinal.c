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
#include <linux/kthread.h>  
#include <linux/spinlock.h>
#include "mp2_given.h"


#define PROC_ENTRY "/proc/mp2/statusKEV"
#define MAX_PROC_SIZE PAGE_SIZE
#define FILENAME "statusKEV"
#define DIRECTORY "mp2KEV"
#define MAX_USER_RT_PRIO        100
/*Task States */
#define MP2_TASK_RUNNING  0
#define MP2_TASK_READY    1
#define MP2_TASK_SLEEPING 2


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel module that calculates CPU time of a process");
MODULE_AUTHOR("G22");
static struct proc_dir_entry* mp2_dir;
static struct proc_dir_entry* mp2_entry_status;
static struct kmem_cache* mp2_cache;
static struct task_struct* dispatching_thread;

static DEFINE_MUTEX(process_mutex);
static DEFINE_SPINLOCK(mr_lock);


struct mp2_task_struct
{
	struct task_struct * task;
	struct list_head task_node;
	struct timer_list task_timer;
	struct list_head mp2_rq_list; //Run_Queue for KThread
	unsigned int task_state;
	uint64_t next_period;
	pid_t pid;
	unsigned long period;
	unsigned long processing_time;
};

struct mp2_task_struct *curr_running_task; 
static struct list_head mp2_task_struct_list;	  //assign a list head to mp2_task_struct
static struct list_head mp2_rq;                   //assign a list head to mp2_rq_list

/*Add the task to the Run Queue sorted via priority(Deadline) and moves a node to READY state from SLEEPING*/

void add_task_to_rq(struct mp2_task_struct *tmp)
{
	struct list_head *prev, *curr;
	struct mp2_task_struct *curr_task;

	/* Task state updated to ready */
	tmp->task_state = MP2_TASK_READY;

	/* Get the run queue head */
	prev = &mp2_rq;
	curr = prev->next;

	/* Find the right position to insert the new task
	   according to its priority
	*/
	list_for_each(curr, &mp2_rq) {
		curr_task = list_entry(curr, typeof(*tmp), mp2_rq_list);

		if (curr_task->period > tmp->period) {
			break;
		}
		prev = curr;
	}

	/* Add it to run queue list */
	__list_add(&(tmp->mp2_rq_list),prev,curr); 
}

void remove_task_from_rq(struct mp2_task_struct *tmp)
{
	list_del(&(tmp->mp2_rq_list));
}

/*timer function, will be called when the timer expires*/

static void wakeup_timer_handler(pid_t pid)
{
	
	//struct mp2_task_struct *tmp = find_mp2_task_by_pid(pid);
	struct mp2_task_struct *tmp;
	unsigned long flags;
	spin_lock_irqsave(&mr_lock, flags);
	list_for_each_entry(tmp, &mp2_task_struct_list, task_node) {
		if (tmp->pid == pid) {
			break;
		}
	}
	spin_unlock_irqrestore(&mr_lock, flags);
	/* tmp should not be NULL.. sanity check*/
	if (tmp == NULL) {
		printk(KERN_WARNING "G22:MP2: task not found\n");
		return;
	}

	/* Add the task to runqueue */
	add_task_to_rq(tmp);

	/* Wake up kernel scheduler thread */
	wake_up_process(dispatching_thread);
}




static bool mp2_admission_control(unsigned int processing_time, unsigned int period)
{
	struct mp2_task_struct *tmp;
	long new_total_utilization = (processing_time*1000)/period;

	
	mutex_lock(&process_mutex);
	/* Add C/P values for all exisiting processes */
	list_for_each_entry(tmp, &mp2_task_struct_list, task_node) {
		new_total_utilization += ((tmp->processing_time)*1000)/(tmp->period);
	}
	mutex_unlock(&process_mutex);


	/* If total utilization exceeds the limit, reject */
	if (new_total_utilization>693) {
		return false;
	}

	/* else admit */
	return true;
}





static int register_process(pid_t pid, unsigned long period, unsigned long processing_time)
{
	printk(KERN_INFO "G22:MP2 - regsitering process %d\n", pid);
	struct mp2_task_struct *new_task;
	if(!mp2_cache)
	{
		printk(KERN_ALERT "memory space not available!\n");
		return -ENOMEM;
	}
	

/*	if(admission_control(period,processing_time)<0)
	{
		printk(KERN_NOTICE "utilization load exceeded, cannot admit process %d\n", pid);
		return -EUSERS;
	}*/
	
	new_task=kmem_cache_alloc(mp2_cache, GFP_KERNEL);

	if(!new_task)
	{
		printk(KERN_ALERT "failed to alloc space for job %d!\n", pid);
		return -ENOMEM;
	}
	new_task->pid=pid;
/*	newtask->task=find_task_by_pid(pid);
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
	//list_add_tail(newtask->list, &process_head);
	add_process_to_list(newtask);
	mutex_unlock(&process_mutex);
*/
	if (mp2_admission_control(new_task->processing_time, new_task->period)==false) {
	printk(KERN_WARNING "mp2: Registration for PID:%lu failed during Admission Control",
	new_task->pid);
	kfree(new_task);
	return;
	}

	printk(KERN_INFO "mp2: Registration for PID:%lu with processing time:%lu and Period:%lu\n",
	       new_task->pid,
	       new_task->processing_time,
	       new_task->period);

	/* Find the task struct (check for pid type)*/
	new_task->task = find_task_by_pid(new_task->pid);

	if (new_task->task == NULL) {
		printk(KERN_WARNING "mp2: Task not found\n");
		kfree(new_task);
		return;
	}

	/* Calculate the next release time for this process */
	new_task->next_period = jiffies + msecs_to_jiffies(new_task->period);

	/* Enter critical region */
	mutex_lock(&process_mutex);
	/* Add entry to the list */
	list_add_tail(&(new_task->task_node), &mp2_task_struct_list);

	/* Exit critical region */
	mutex_unlock(&process_mutex);	
//up(&mp2_sem);

	/* Setup the timer for this task */
	setup_timer(&new_task->task_timer, wakeup_timer_handler, new_task->pid);

	/* Mark mp2 task state as sleeping */
	new_task->task_state = MP2_TASK_SLEEPING;
	printk(KERN_INFO "process with pid %d, period %lu, and processing time %lu is registered ! \n", pid, period, processing_time);

	return 0;
}

void yield_process(pid_t pid)
{
	//unsigned int pid;
	struct mp2_task_struct *tmp;
	uint64_t release_time;
	unsigned long flags;

	/* Check if this is the current running process */
	if (curr_running_task && (curr_running_task->pid == pid)) 
	{
		tmp = curr_running_task;
	} 
	else 
	{
		/* Else check on the mp2_task_struct_list */
		mutex_lock(&process_mutex);
		list_for_each_entry(tmp, &mp2_task_struct_list, task_node) {
		if (tmp->pid == pid) 
		 {
		  break;
		 }
		}
		mutex_unlock(&process_mutex);	
	}

	/* If process is not found on the list, something is wrong */
	if (tmp == NULL) {
		printk(KERN_WARNING "mp2: Task not found for yield:%u\n",pid);
		return;
	}

	/* Check if we still have time for next release */
	if (jiffies < tmp->next_period) {
		/* If yes, put this task in sleep,remove it from rq(if present there,
		   and start the timer
		*/
		release_time = tmp->next_period - jiffies;
		printk(KERN_INFO "mp2: release_time:%llu,%d\n", release_time,tmp->pid);

		/* Change the task state to SLEEPING */
		tmp->task_state = MP2_TASK_SLEEPING;

		/* Start the timer according to release time */
		mod_timer(&tmp->task_timer, jiffies + release_time);

		/* If this task was currently executing,
		   remove it from run queue and wake up
		   scheduler thread */
		if (curr_running_task && (curr_running_task->pid == tmp->pid)) {
			spin_lock_irqsave(&mr_lock, flags);
			remove_task_from_rq(tmp);
			spin_unlock_irqrestore(&mr_lock, flags);
			curr_running_task = NULL;
			wake_up_process(dispatching_thread);
		}
	} else {
		/* Process needs to be on run queue
		   If in sleeping state, move it to run queue
		*/
		if (tmp->task_state == MP2_TASK_SLEEPING) {
			tmp->task_state = MP2_TASK_READY;
			spin_lock_irqsave(&mr_lock, flags);
			add_task_to_rq(tmp);
			spin_unlock_irqrestore(&mr_lock, flags);
		}
		wake_up_process(dispatching_thread);
		curr_running_task = NULL;
	}

	/* Lower the priority of the task */
	//mp2_set_sched_priority(tmp, SCHED_NORMAL, 0);
	sched_setscheduler(tmp->task, SCHED_NORMAL, 0);
	set_task_state(tmp->task, TASK_UNINTERRUPTIBLE);
	printk(KERN_INFO "mp2: Yield for %u\n",pid);

	schedule();
}

/*when the process ends, it trys to deregister itself and this function gets called 
in the kernel space to remove the process from the linked-list*/
static void deregister_process(pid_t pid)
{
	printk(KERN_INFO "\nG22:MP2 - Deregistering process %d\n", pid);
	unsigned long flags;
	struct mp2_task_struct *tmp;
	mutex_lock(&process_mutex);
	list_for_each_entry(tmp, &mp2_task_struct_list, task_node) {
		if (tmp->pid == pid) 
		 {
	 	   break;
	 	 }
	}
	mutex_unlock(&process_mutex);
	if (tmp) 
	{
		spin_lock_irqsave(&mr_lock, flags);
		remove_task_from_rq(tmp);
		/* Delete the task from mp2 task struct list */
		list_del(&tmp->task_node);
		/* Exit critical region */
		spin_unlock_irqrestore(&mr_lock, flags);	
		/* Delete the timer */
		del_timer_sync(&tmp->task_timer);
		if (tmp == curr_running_task)
	 	{
          		/* Reset the priority to normal */
	  		sched_setscheduler(tmp->task_state, SCHED_NORMAL, 0);
	  		curr_running_task = NULL;
	 	}
        	/* Free the structure */
		kfree(tmp);
		wake_up_process(dispatching_thread);
	}
        else
	{ 	printk(KERN_WARNING "G22:mp2: Task not found for yield:%u\n",pid);
	}
}

static int user_data_parser(const char* user_data, const char* register_flag, pid_t* pid, unsigned long* period, unsigned long* processing_time)
{
	printk(KERN_INFO "G22:MP2:step in the mp2 parser");
	char delimiters[] = " .,;:!-";
	
	register_flag     = strsep(&user_data,delimiters);
	pid               = (pid_t)simple_strtol(strsep(&user_data,delimiters), NULL, 10);
	period            = simple_strtol(strsep(&user_data,delimiters), NULL, 10);
	processing_time   = simple_strtol(strsep(&user_data,delimiters), NULL, 10);
	
	if(*pid==-EINVAL || *period==-EINVAL || *processing_time==-EINVAL) 
	{	
		return -EINVAL;
	}
	return 0;
}


int mp2_write(struct file* file, const char __user*  buffer, unsigned long count, void* data)
{
	printk(KERN_INFO "step in the mp2 write");
	char register_flag; 
	pid_t pid=0;
	unsigned long period=0;
	unsigned long processing_time=0;
	//long int process_id;
	char* user_data=(char*)kmalloc((count+1)*sizeof(char), GFP_KERNEL);
	//TODO may wanna check the max length for the user input
	// count< max


	if(copy_from_user(user_data, buffer, count)) 
	{	
		return -EFAULT ;
	}	
	//user_data[count]=NULL;
	user_data[count]=0;

	//extract the sepcific info from the user_data and store them 
	if(user_data_parser(user_data, &register_flag, &pid, &period, &processing_time)<0)
	{
		printk(KERN_WARNING " unable to extract the data from user_data\n");
		return -EFAULT ;
	}

	
	switch(register_flag)
	{	
		case 'R':
			register_process(pid,period,processing_time);
			break;
		case 'Y':
			yield_process(pid);
			break;
		case 'D':
			deregister_process(pid);
			break;
		default: 	
			printk(KERN_WARNING " invaild option in registeration\n");
			break; 
	}



	kfree(user_data);
	return 0 ;
}

static int kthread_dispatch(void* data)
{ 	
	struct mp2_task_struct *tmp;
	unsigned long flags;
	struct sched_param sparam_high;
	struct sched_param sparam_low;
	while(1)
	{	
		set_current_state(TASK_UNINTERRUPTIBLE);	//set the dispatching thread to sleep

		if(kthread_should_stop())
		{
			printk(KERN_INFO "dispatching thread finished its job \n");
			return 0;			
		}

		schedule(); 				// give up the control 
		
		/* Check if we have anything on the runqueue */
		if (!list_empty(&mp2_rq)) {
			/* If there is a task waiting on the run queue,
			   and has a higher priority than current running task if any
			   schedule it
			*/
			spin_lock_irqsave(&mr_lock, flags);
			tmp = list_first_entry(&mp2_rq, typeof(*tmp), mp2_rq_list);
			spin_unlock_irqrestore(&mr_lock, flags);
			/* If there is some task running currenly, put it into ready state */
			if (curr_running_task && curr_running_task->period > tmp->period) {
				printk(KERN_INFO "G22:MP2: Scheduling out current process\n");
				sparam_low.sched_priority=0;
				sched_setscheduler(curr_running_task->task, SCHED_NORMAL, &sparam_low);
				set_task_state(curr_running_task->task, TASK_UNINTERRUPTIBLE);
				curr_running_task->task_state = MP2_TASK_READY;
				curr_running_task = NULL;
				}
			else {
				printk(KERN_INFO "G22:MP2 -No Preemption: Task %d has the highest priority among those ready but lower than the currently running task" 
							"(pid: %d)", tmp->pid, curr_running_task->pid);
			     }
			}

			/* Wake up the selected process */
			wake_up_process(tmp->task);
			/* Raise its priority */
			sparam_high.sched_priority=MAX_USER_RT_PRIO-1;
			sched_setscheduler(tmp->task, SCHED_FIFO, &sparam_high);
			/* update the current variable */
			curr_running_task = tmp;
			/* Set the state to RUNNING */
			curr_running_task->task_state = MP2_TASK_RUNNING;
			printk(KERN_INFO "next task running:%d\n",tmp->pid);
			/* Update the next releast time */
			curr_running_task->next_period += msecs_to_jiffies(curr_running_task->period);
	}
	return 0;
}


int mp2_read(struct file *mp2_file,const char __user *buffer, size_t count, loff_t *data)
{
/*
	int bytes=0;
	char* temp;
	printk(KERN_INFO "User is reading %s\n", PROC_ENTRY );
	if(offset>0) return bytes;
	temp=(char*)kmalloc(MAX_PROC_SIZE*sizeof(char), GFP_KERNEL);
	temp[0]=0;

	mutex_lock(&process_lock);
	bytes=get_user_info(temp);
	mutex_unlock(&process_lock);

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
*/
	printk(KERN_INFO "\nG22:MP2 - Entering read call back\n");
	size_t copied = 0;
        struct mp2_task_struct *tmp;
        char * buff = NULL;
    	if((buff =(char*) kmalloc(count,GFP_KERNEL))== NULL)
	 {
	 return -ENOMEM;
	 }    
	/* Iterate through the linked list and update the user buffer by using the copy_to_user function */
    	list_for_each_entry(tmp, &mp2_task_struct_list, task_node) {
        copied += sprintf(buffer, "%s%d, %lu, %lu\n", buffer, tmp->pid, tmp->period, tmp->processing_time );
	}
   	 /* sprintf returns the length of string copied and it does not include the null character automatically appended at the end so we have to increase the length by 1*/
   	 copied = strlen(buff) + 1;
    	copy_to_user(buffer, buff, copied);
    	kfree(buff);
    	printk(KERN_INFO "returning %d bytes to user! \n", copied);
    	return copied;
	
}



static const struct file_operations mp2_file = {
    .owner      = THIS_MODULE,
    .read       = mp2_read,
    .write      = mp2_write,
};



static int __init mp2_module_init(void)
{
	/*
	mp2_dir=proc_mkdir("mp2", NULL) ;
	mp2_entry_status=create_proc_entry("status", 0666, mp2_dir) ;
	mp2_wq=create_workqueue("MP2_WORKQUEUE");	

	if(!mp2_wq)
	{
		printk(KERN_ALERT "Error: failed to create work queue! \n");
		return -ENOMEM;
	}

	if(mp2_dir==NULL || mp2_entry_status==NULL) 
	{
		remove_proc_entry("status",mp2_dir);
		remove_proc_entry("mp2",NULL);
		printk(KERN_ALERT "Error: could not create proc file!");
		return -ENOMEM;
	}
	mp2_entry_status->read_proc = mp2_read;
	mp2_entry_status->write_proc = mp2_write;
	mp2_entry_status->mode  = S_IFREG | S_IRUGO ;
	mp2_entry_status->uid 	=0;
	mp2_entry_status->gid   =0;
	*/
	//printk(KERN_INFO "searching in the list for for the READY task with the highest priority (shortest period).\n");
	printk(KERN_INFO "step in the mp2 init");
	mp2_dir = proc_mkdir(DIRECTORY, NULL);
   	mp2_entry_status = proc_create(FILENAME,0666, mp2_dir, &mp2_file);
   	INIT_LIST_HEAD(&mp2_task_struct_list);
	mp2_cache =(struct kmem_cache*) kmem_cache_create( mp2_cache, sizeof(struct mp2_task_struct), 0, GFP_KERNEL ,NULL);
	
        /* Create a kernel thread */
        printk(KERN_INFO "Initiating kernel thread... \n");
        dispatching_thread = kthread_create(kthread_dispatch, NULL, "kthread");
        if(! dispatching_thread)
    	{
	printk(KERN_INFO "Eror in allocating memory \n");
    	return -ENOMEM;
    	}
	
	/*
	init_timer(&my_timer);
	setup_timer(&my_timer, my_timer_callback, 0);
	mod_timer(&my_timer, jiffies+msecs_to_jiffies(5000));
	printk(KERN_INFO "mp2 Module loaded! \n");
*/
	return 0;
}

static void __exit mp2_module_exit(void)
{
	
	if(curr_running_task){
	curr_running_task=NULL;
	}
	kthread_stop(dispatching_thread);
	struct mp2_task_struct *tmp;
	struct mp2_task_struct *swp;
	kmem_cache_destroy (mp2_cache);
	remove_proc_entry("status", mp2_dir);
	remove_proc_entry("mp2",NULL);
	/*ret = del_timer(&my_timer);
	if(ret) printk(KERN_INFO "The timer is still in used \n");
	else
		printk(KERN_INFO "timer uninstalled\n");

	flush_workqueue(mp2_wq);
	destroy_workqueue(mp2_wq);
	clean_up_list();
*/
	mutex_lock(&process_mutex);
	list_for_each_entry_safe(tmp, swp, &mp2_task_struct_list, task_node)
	{
		list_del(&tmp->task_node);
		kfree(tmp);
	}	
	mutex_unlock(&process_mutex);
	printk(KERN_INFO "mp2 module unloaded!!!!!!\n");

}

module_init(mp2_module_init);
module_exit(mp2_module_exit);

