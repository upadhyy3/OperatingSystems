#include <linux/module.h>
 #include <linux/init.h>
 #include <linux/kernel.h>

static int answer = 42 ;
module_param(answer, int, 0644);
MODULE_PARM_DESC(answer, "Just the learning phase");

 static int __init hi(void)
 {
     printk(KERN_INFO "p1 module being loaded.\n");
     printk(KERN_INFO "Initial answer = %d.\n", answer);
     return 0;
 }

 static void __exit bye(void)
 {
     printk(KERN_INFO "p1 module being unloaded.\n");
     printk(KERN_INFO "Final answer = %d.\n", answer);
 }

 module_init(hi);
 module_exit(bye);

