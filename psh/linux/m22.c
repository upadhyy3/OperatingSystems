#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

static int __init hi(void)
 {
     printk(KERN_INFO "module m22 being loaded.\n");
     printk(KERN_INFO "exported variable rday3 = %d", rday_3);
     return 0;
 }

static void __exit bye(void)
 {
     printk(KERN_INFO "module m2 being unloaded.\n");
 }

module_init(hi);
module_exit(bye);

