#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/list.h>
#include<linux/module.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include<linux/sched.h>
#include "my_module_variables.h"


/* TODO 1: define the size of the dynamically 
*  allocated memory region as a macro here and
*  declare the global pointer variable that will 
*  point to a dynamically allocated memory block
*/

#define memSize 4096

static int *memLoc;

dev_t dev = 0;

static struct class *dev_class;
static struct cdev my_cdev;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t my_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static long my_ioctl(struct file *filp, unsigned int mode, unsigned long arg);

static struct file_operations fops = 
{
	.owner	= THIS_MODULE,
	.read 	= my_read,
	.write	= my_write,
	.open	= my_open,
	.release = my_release, 
	.unlocked_ioctl = my_ioctl,
};
static long my_ioctl(struct file *filp, unsigned int mode, unsigned long arg){
	    long longRootPid;
	    char ** commandArgs;
	    pid_t rootPid;
	    struct task_struct *givenProcces; //current task_struct
	    struct task_struct *task;
	    struct list_head *list;
	    int childNum = 0;
	    switch (mode)
	    {
		case READ_VAL:
		    printk(KERN_INFO"Read from user space\n");
		    break;
		//this case should get the PID number from user space and traverse 
		case WRITE_VAL:
		    if (copy_from_user(&commandArgs, (long unsigned *)arg, sizeof(char**)))
		    {
		        return -EACCES;
		    }
		    printk(KERN_INFO"Value of pid is: %s\n", *commandArgs);
		    printk(KERN_INFO"Value of pid is: %s\n", commandArgs[1]);
		    
		    //converting from strgin to long
		    if(kstrtol(commandArgs[0], 10, &longRootPid) != 0){
		    printk(KERN_INFO"Error in conversion of string pid to long value");
		    return -EFAULT;
		    }
		    
		    rootPid = (pid_t) longRootPid;
		    //getting task_struct of given procces
		    givenProcces = pid_task(find_vpid(rootPid), PIDTYPE_PID);
		    
		    if(givenProcces == NULL){
		    printk(KERN_INFO"Pid is invalid");
		    return -EFAULT;
		    }
	
		    printk(KERN_INFO"PID: %d, Procces name: %s\n", givenProcces->pid, givenProcces->comm);
		    /*
			    list_for_each(list, &(givenProcces->children)) {
    				task = list_entry(list, struct task_struct, sibling);
    				childNum++;
				}
			*/	
			printk(KERN_INFO"Child num: %d", childNum);
		    break;
		default:
		    return -EINVAL;
	    }
 
    return 0;
}
static int my_open(struct inode *inode, struct file * file)
{	
/* TODO 2: dynamically allocate a single block 
*  of memory region here
*/
	memLoc = kmalloc(memSize, GFP_KERNEL);
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
/* TODO 3: free the dynamically allocated memory region here
*/
	kfree(memLoc);
	return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
/* TODO 4: make the user application read the content of
*  the dynamically allocated memory region here
*/	
	if(copy_to_user(buf, memLoc, memSize) != 0){
	return -EFAULT;
	}
	
	return len;
}

static ssize_t my_write(struct file *filp,const char __user *buf, size_t len, loff_t* off)
{
/* TODO 5: make the user application write to
*  the dynamically allocated memory region here
*/
	
	if(copy_from_user(memLoc, buf, memSize) != 0){
	return -EFAULT;
	}
	return len;
}

static int __init my_driver_init(void)
{
	/* Allocating Major number dynamically*/
	/* &dev is address of the device */
	/* <0 to check if major number is created */
	if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO"Major = %d Minor =  %d..\n", MAJOR(dev),MINOR(dev));

	/* creating cdev structure*/
	cdev_init(&my_cdev, &fops);

	/* adding character device to the system */
	if((cdev_add(&my_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}	 

	/* creating struct class */
	if((dev_class =  class_create(THIS_MODULE, "my_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}

	/* creating device */

	if((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	printk(KERN_INFO"Device driver insert...done properly...");
	return 0;

r_device: 
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit my_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Device driver is removed successfully...\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aditya");
MODULE_DESCRIPTION("The character device driver");


