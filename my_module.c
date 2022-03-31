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

#define mem_size 1024
#define maxQueue 200
uint8_t *kernel_buffer;

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
// queue for bfs search
struct task_struct *bfsQueue[maxQueue];
int front = 0;
int rear = 0;

static struct file_operations fops = 
{
	.owner	= THIS_MODULE,
	.read 	= my_read,
	.write	= my_write,
	.open	= my_open,
	.release = my_release, 
	.unlocked_ioctl = my_ioctl,
};

void breadthFirstSearch(struct task_struct *task)
{   
	struct task_struct *child, *grandChild, *currentTask = task;
	struct list_head *list, *grandChildList;
	int i, j, frontDummy = -1;
	printk(KERN_INFO"PID: %d, Procces name: %s\n", task->pid, task->comm);
	while(frontDummy != rear){
		//after initial call, don't add children
		if(frontDummy == -1){
		list_for_each(list, &currentTask->children) {
			//if rear pointer reaches maximum, sets it to beginning of array
			if(rear + 1 == maxQueue) rear = 0;
			child = list_entry(list, struct task_struct, sibling);
			//queue children
			bfsQueue[rear] = child;
			rear++;
		}
		}
		j = rear;
		for(i = front; i<j; i++){
			//if front pointer reaches maximum, sets it to beginning of array
			if(front + 1 == maxQueue) front = 0;
			//dequeue children, print its pid and name
			printk(KERN_INFO"PID: %d, Procces name: %s\n", bfsQueue[front]->pid, bfsQueue[front]->comm);

			list_for_each(grandChildList, &bfsQueue[front]->children) {
				if(rear + 1 == maxQueue) rear = 0;
				grandChild = list_entry(grandChildList, struct task_struct, sibling);
				//queue grandchildren
				bfsQueue[rear] = grandChild;
				rear++;
			}
			front++;
		}
		currentTask = bfsQueue[front];
		frontDummy = front;
	}
}


void depthFirstSearch(struct task_struct *task)
{   
	struct task_struct *child;
	struct list_head *list;

	printk(KERN_INFO"PID: %d, Procces name: %s\n", task->pid, task->comm);

	list_for_each(list, &task->children) {
		child = list_entry(list, struct task_struct, sibling);
		depthFirstSearch(child);
	}
}

static long my_ioctl(struct file *filp, unsigned int mode, unsigned long arg){
	long longRootPid;
	char ** commandArgs;
	pid_t rootPid;
	struct task_struct *givenProcces;

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
			printk(KERN_INFO"Initial pid: %s\n", *commandArgs);
			printk(KERN_INFO"Flag: %s\n", commandArgs[1]);

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
			if(strcmp(commandArgs[1], "-d") == 0){
			depthFirstSearch(givenProcces);
			}else if(strcmp(commandArgs[1], "-b") == 0){
			breadthFirstSearch(givenProcces);
			}else{
			printk(KERN_INFO"Invalid flag");
			}


			break;
		default:
			return -EINVAL;
	}

	return 0;
}
static int my_open(struct inode *inode, struct file * file)
{	
	if((kernel_buffer = kmalloc(mem_size, GFP_KERNEL)) == 0) {
		printk(KERN_INFO "Cannot allocate the memory to the kernel...\n");
		return -1;
	}
	printk(KERN_INFO "Device file opened...\n");
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	kfree(kernel_buffer);
	printk(KERN_INFO "Device FILE closed...\n");
	return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{	
	if(copy_to_user(buf, kernel_buffer, mem_size) != 0) return -EFAULT;
	printk(KERN_INFO "Data read : BONE...\n");
	return mem_size;
}

static ssize_t my_write(struct file *filp,const char __user *buf, size_t len, loff_t* off)
{
	if(copy_from_user(kernel_buffer, buf, len) != 0) return -EFAULT;
	printk(KERN_INFO "Data is written successfully...\n");
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


