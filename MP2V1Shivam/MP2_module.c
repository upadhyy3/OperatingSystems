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


#define PROC_ENTRY "/proc/mp2/statusShivam"
#define FILENAME "status"
#define DIRECTORY "mp2"
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
//static struct kmem_cache* mp2_cache;
static struct task_struct* dispatching_thread;
//static DECLARE_WAIT_QUEUE_HEAD (mp2_waitqueue);

static DEFINE_MUTEX(process_mutex);
static DEFINE_SPINLOCK(mr_lock);


typedef struct mp2_task_struct
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
}mp2_task_obj;

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
	//wake_up_interruptible(&mp2_waitqueue);
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
/*	if(!mp2_cache)
	{
		printk(KERN_ALERT "memory space not available!\n");
		return -ENOMEM;
	}
*/	

/*	if(admission_control(period,processing_time)<0)
	{
		printk(KERN_NOTICE "utilization load exceeded, cannot admit process %d\n", pid);
		return -EUSERS;
	}*/
	
//	new_task=kmem_cache_alloc(mp2_cache, GFP_KERNEL);
	new_task = kmalloc(sizeof(*new_task), GFP_KERNEL);
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
			//wake_up_interruptible(&mp2_waitqueue);
		}
	} else {
		/* Process needs to be on run queue
		   If in sleeping state, move it to run queue
		*/
		if (tmp->task_state == MP2_TASK_SLEEPING) {
			tmp->task_state = MP2_TASK_READY;
			spin_lock_irqsave(&mr_lock, flags);
			add_task_to_rq(tmp);
			spin_unlock_irqrestore(&mr_lock,