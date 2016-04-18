#include <linux/module.h>
 #include <linux/init.h>
 #include <linux/kernel.h>

  static int rday_1 = 1;
  int rday_2 = 2;
  int rday_3 = 3;

  EXPORT_SYMBOL(rday_3);

   static int __init hi(void)
 {
     printk(KERN_INFO "module m2 being loaded.\n");
     return 0;
 }

  static void __exit bye(void)
 {
     printk(KERN_INFO "module m2 being unloaded.\n");
 }

  module_init(hi);
  module_exit(bye);

  MODULE_AUTHOR("Piyush Shrivastava");
  MODULE_LICENSE("GPL");
  MODULE_DESCRIPTION("Let's try some exporting.");
