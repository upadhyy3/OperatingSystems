
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/jiffies.h>

#define PROCFS_MAX_SIZE	1024
#define PROCFS_NAME "status"

static struct proc_dir_entry *proc_entry;
static char procfs_buffer[PROCFS_MAX_SIZE];
static unsigned long procfs_buffer_size= 0;

int procfile_read(char *buffer,char **buffer_location,off_t offset, int *eof, void *data)
{
	int ret;
	printk(KERN_INFO "procfile_read (/proc/%s) called\n", PROCFS_NAME);
	if(offset > 0)
	{
	 ret =0;
	}
	else
	{
	 memcpy(buffer, procfs_buffer, procfs_buffer_size);
	 ret = procfs_buffer_size; 
	}	
	return ret;
}

int procfile_write(struct file *file, const char *buffer, unsigned long count,void *data)
{
	procfs_buffer_size= count;
	if(procfs_buffer_size > PROCFS_MAX_SIZE)
	{ procfs_buffer_size = PROCFS_MAX_SIZE;
	}
	if (copy_from_user(procfs_buffer,buffer, procfs_buffer_size))
	{
	return -EFAULT;
	}
	return procfs_buffer_size;
}
	
static const struct file_operations hello_proc_fops = {
	.owner = THIS_MODULE,
	.open  = procfile_read  	
	
}
static int init_method(void)
	{
	/* printk(KERN_INFO "Hello World"); */
	proc_entry = create_proc_entry(PROCFS_NAME, 0644, NULL);

	if (proc_entry == NULL)
	{
	remove_proc_entry(PROCFS_NAME, &proc_root);
	printk(KERN_ALERT "Error: could not initialize /proc/%s\n", PROCFS_NAME);
	return -ENOMEM;
	}	

/*	proc_entry->read_proc   = procfile_read;
	proc_entry->write_proc  = procfile_write;
	proc_entry->owner 	= THIS_MODULE;
	proc_entry->mode	= S_IFREG | S_IRUGO;
 	proc_entry->uid		= 0;
	proc_entry->gid		= 0;
	proc_entry->size	= 37;
*/
	printk(KERN_INFO "/proc/%s created\n", PROCFS_NAME);
	
return 0;
}

static void exit_method(void)
{

	remove_proc_entry(PROCFS_NAME, &proc_root);
	printk(KERN_INFO "/proc/%s remoced\n", PROCFS_NAME);
/* printk(KERN_INFO "Bye World");*/
}

module_init(init_method);
module_exit(exit_method);

