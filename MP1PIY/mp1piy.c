
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
#define FILENAME "status1"
#define DIRECTORY "mp1piy"
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
//static struct timer_list timer;

static DEFINE_MUTEX(list_lock);




//static ssize_t mp1_read(struct file *file,const char __user *buffer, size_t count, loff_t *data)

static ssize_t mp1_read(char *buffer, char **buffer_location, off_t offset, int buffer_lenth, int *eof, void *data)
{
	printk(KERN_INFO "MP1PIY -- I am in the read\n");
	char * buff= NULL;
	struct Full_registration *full_registration;
	size_t len = 1 + list_size * (2 * sizeof(unsigned int) + 3);
	int copied = 0;	
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

}


static ssize_t mp1_write(struct file *file,const char __user *buffer, size_t count, loff_t *data)
{
    struct Full_registration *full_registration;
    long pid;
    char * buff = NULL;
    buff = kmalloc(sizeof(char), GFP_KERNEL);
    /* Add new PID to list of registered processes */
    if(!copy_from_user(buff, buffer, count)) 
    {
	printk(KERN_INFO "MP1PIY -- copy from user got executed well!! \n");
	printk(KERN_INFO "MP1PIY -- count is %d\n", count);
	printk(KERN_INFO "MP1PIY -- *buff is %c\n", *buff);
	printk(KERN_INFO "MP1PIY -- *buff in string is %s\n", *buff);
	//printk(KERN_INFO "MP1PIY -- &pid is %i\n", &pid);
	printk(KERN_INFO "MP1PIY -- *buffer is %c\n", *buffer);
	printk(KERN_INFO "MP1PIY -- *buffer str is %s\n", *buffer);

        //if(!kstrtol(buff, BASE10, &pid))
	if(strict_strtoul(buff, 10, pid) != 0)
        {
	    printk(KERN_INFO "MP1PIY -- String to long conversion is successful \n");
	    printk(KERN_INFO "MP1PIY -- pid is %d", pid);
            if ((full_registration = kmalloc(sizeof(struct Full_registration), GFP_ATOMIC)) == NULL) 
            {
		printk(KERN_INFO "MP1PIY -- Error in write\n");
                return -ENOMEM;
            }
            full_registration->pid = pid;
            full_registration->cpu_time = 0;

            mutex_lock(&list_lock);
            INIT_LIST_HEAD(&(full_registration->list));
            list_add_tail(&(full_registration->list), &(list.list));
            list_size++;
            mutex_unlock(&list_lock);
        } 
	else 
	{
            printk(KERN_INFO "MP1PIY -- Error converting PID string to a long.\n");
            kfree(buff);
            return -1;
        }
    } 
    else 
    {
        printk(KERN_INFO "MP1PIY -- Error copying data from user.\n");
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

// mp1_init - Called when module is loaded
int __init mp1_init(void)
{
   printk(KERN_ALERT "MP1PIY MODULE LOADING\n");
   // Insert your code here ...

   proc_dir = proc_mkdir(DIRECTORY, NULL);   
   proc_entry = proc_create(FILENAME,0666, proc_dir, &mp1_file);
   INIT_LIST_HEAD(&list.list);
   printk(KERN_ALERT "MP1PIY MODULE LOADED\n");
   return 0;   
}

// mp1_exit - Called when module is unloaded
void __exit mp1_exit(void)
{
   printk(KERN_ALERT "MP1PIY MODULE UNLOADING\n");
   // struct Full_registration *curr_proc;
   // struct Full_registration *tmp;
   // mutex_lock(&list_lock);
   // list_for_each_entry_safe(curr_proc, tmp, &list.list, list) {
   //     printk(KERN_INFO "Deleting PID: %ld\n", curr_proc->pid);
   //     list_del(&curr_proc->list);
   //     kfree(curr_proc);
   //     list_size--;
   // }
   // mutex_unlock(&list_lock);
   remove_proc_entry(FILENAME,proc_dir);
   //remove_proc_entry(proc_dir);
   
   printk(KERN_ALERT "MP1PIY MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp1_init);
module_exit(mp1_exit);
