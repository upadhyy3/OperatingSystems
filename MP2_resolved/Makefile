obj-m += mp2_module.o

all:  
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc user.c -o user

clear:
	dmesg --clear

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -rf *o user

