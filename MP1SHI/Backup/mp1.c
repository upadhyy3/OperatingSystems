
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include "mp1_given.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("22");
MODULE_DESCRIPTION("CS-423 MP1");

#define DEBUG 1
#define FILENAME "statusSHI"
#define DIRECTORY "mp1shi"
#define BASE10 10


static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_entry;

struct Full_registration 
{
    unsigned long pid;       /* PID of the process */
    unsigned long cpu_time;  /* CPU time of the process */
    struct list_head list;
};

static struct Full_registration list;
static unsigned int list_size = 0;
static struct timer_list timer;

static DEFINE_MUTEX(list_lock);




//static ssize_t mp1_read(struct file *file,const char __user *buffer, size_t count, loff_t *data)

static ssize_t mp1_read(char *buffer, char **buffer_location, off_t offset, int buffer_lenth, int *eof, void *data)
{
	printk(KERN_INFO "I am in the read\n");
	/*size_t copied=0;
	char * buff= NULL;
	struct Full_registration *full_registration;
	size_t len = 1 + list_size * (2 * sizeof(unsigned int) + 3);
	
    if ((buff = (char *) kcalloc(len+1,sizeof(char),GFP_ATOMIC)) == NULL)
    {
        return -ENOMEM;
    }
	
	
	list_for_each_entry(full_registration,&list.list,list){

	copied += sprintf(buff + copied, "%lu: %lu\n", full_registration->pid, full_registration->cpu_time);
}
	copied = strlen(buff) + 1;
	copy_to_user(buffer, buff, copied);
	kfree(buff);
	return copied ;
*/

    size_t len = 0;
    struct Full_registration *full_registration;

    /* Each entry = (unsigned long) + ": " + (unsigned long) + "\n" */
    size_t buff_size = 1 + list_size * (2 * sizeof(unsigned int) + 3);

    char * str = NULL;
    if ((str = kcalloc(buff_size + 1, sizeof(char), GFP_ATOMIC)) == NULL)
        return -ENOMEM;

    /* On sepearate lines, write each PID and its utime. Then copy it to the
     * provided buffer (i.e. the user's terminal) */
    list_for_each_entry(full_registration, &list.list, list) {
        len += sprintf(str + len, "%lu: %lu\n", full_registration->pid, full_registration->cpu_time);
	printk(KERN_INFO "PId %lu Time %lu",full_registration->pid, full_registration->cpu_time);
    }
    len = strlen(str) + 1;
    copy_to_user(buffer, str, len);

    kfree(str);
    return len;
}


static ssize_t mp1_write(struct file *file,const char __user *buffer, size_t count, loff_t *data)
{
    struct Full_registration *full_registration;
    long pid;
    char * buff = NULL;
 if ((buff = kcalloc(count + 1, sizeof(char), GFP_ATOMIC)) == NULL)
        return -ENOMEM;
    //buff = kmalloc(sizeof(char), GFP_KERNEL);
    /* Add new PID to list of registered processes */
    if(!copy_from_user(buff, buffer, count)) 
    {
        if(!strict_strtoul(buff, BASE10, &pid)) 
        {    printk(KERN_INFO "Linked list to be updated with PId:%lu",pid);
            if ((full_registration = kmalloc(sizeof(struct Full_registration), GFP_ATOMIC)) == NULL) 
            {
                return -ENOMEM;
            }
            full_registration->pid = pid;
            full_registration->cpu_time = 0;
		printk(KERN_INFO "Linked list executed");
            mutex_lock(&list_lock);
            INIT_LIST_HEAD(&(full_registration->list));
            list_add_tail(&(full_registration->list), &(list.list));
            list_size++;
            mutex_unlock(&list_lock);
        } 
	else 
	{
            printk(KERN_INFO "Error converting PID string to a long.\n");
            kfree(buff);
            return -1;
        }
    } 
    else 
    {
        printk(KERN_INFO "Error copying data from user.\n");
        kfree(buff);
        return -1;
    }

    kfree(buff);
    return count;	
}

static const struct file_operations mp1_file = {
    .owner      = THIS_MODULE,
    .read       = mp1_read,
    .write 	= mp1_write,	
};


static void update_utime(struct work_struct * work) {
    struct Full_registration * curr_proc = NULL;
    struct Full_registration * tmp = NULL;

    /* Update utime for each registered process */
    mutex_lock(&list_lock);
    list_for_each_entry_safe(curr_proc, tmp, &list.list, list) {
	printk(KERN_INFO "We are in the workQueue");
        if (get_cpu_use(curr_proc->pid, &(curr_proc->cpu_time))) {
          printk(KERN_INFO "Deleting closed process (PID: %lu).\n", curr_proc->pid);

           list_del(&(curr_proc->list));
            mutex_unlock(&list_lock);
            kfree(work);
            return;
        }
    }
    mutex_unlock(&list_lock);
    kfree(work);
}

static void sched_work_function(unsigned long data) {
    struct work_struct * work = NULL;

    /* System provided workqueue via workqueue.h */
    if (system_long_wq) {
        /* Must use GFP_ATOMIC (non-blocking) instead of GFP_KERNEL (blocking) 
         * Note: Deallocated after the work is finished. */
        if ((work = kmalloc(sizeof(struct work_struct), GFP_ATOMIC)) == NULL) {
            printk(KERN_INFO "Error allocating memory for work.\n");
            return;
        }

        INIT_WORK(work, update_utime);
        if (queue_work(system_long_wq, work) == 0) {
            kfree(work);
            printk(KERN_INFO "Work already on queue.\n");
        }
    }

    /* Set next timer */
    if (mod_timer(&timer, jiffies + msecs_to_jiffies(5000))) {
       printk(KERN_INFO "Error setting kernel timer.\n");
       return;
    } 
}

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE LOADING\n");
   #endif
   // Insert your code here ...

   proc_dir = proc_mkdir(DIRECTORY, NULL);   
   proc_entry = proc_create(FILENAME,0666, proc_dir, &mp1_file);
   INIT_LIST_HEAD(&list.list);
   printk(KERN_ALERT "MP1 MODULE LOADED\n");
   
    /* Set up kernel timer to fire 5 seconds from now */
    setup_timer(&timer, sched_work_function, 0);
    if (mod_timer(&timer, jiffies + msecs_to_jiffies(5000))) {
        printk(KERN_INFO "Error setting kernel timer.\n");
        return -1;
	}
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   #ifdef DEBUG
   printk(KERN_ALERT "MP1 MODULE UNLOADING\n");
   #endif
    struct Full_registration *curr_proc;
    struct Full_registration *tmp;
    mutex_lock(&list_lock);
    list_for_each_entry_safe(curr_proc, tmp, &list.list, list) {
        printk(KERN_INFO "Deleting PID: %ld\n", curr_proc->pid);
        list_del(&curr_proc->list);
        kfree(curr_proc);
        list_size--;
    }
    mutex_unlock(&list_lock);
   remove_proc_entry(FILENAME,proc_dir);
   remove_proc_entry(DIRECTORY,NULL);
    
    /* Safely delete kernel timer */
    del_timer_sync(&timer);

    /* Force queued work to be completed. */
    flush_workqueue(system_long_wq);

    printk(KERN_INFO "Exit MP1 Module.\n");
   
   printk(KERN_ALERT "MP1 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
