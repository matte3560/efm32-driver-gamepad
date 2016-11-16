/*
 * This is a gamepad Linux kernel module.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/pid.h>

#include <asm/io.h>
#include <asm/siginfo.h>
#include <asm/errno.h>

#include "offsets.h"

// Device name
#define DEVICE_NAME "tdt4258"
#define CDEV_GAMEPAD "gamepad"
#define CDEV_DAC "dac"

// Device number used for gamepad
static dev_t gamepad_dev;

// cdev struct
static struct cdev gamepad_cdev;

// Class struct
static struct class *gamepad_cl;

// Platfom information
static struct resource *gamepad_res;
static int gamepad_irq_even;
static int gamepad_irq_odd;

// Last read input
static uint8_t gamepad_input;

// Task struct of process to signal on interrupt
static struct task_struct* gamepad_task = NULL;
static struct mutex gamepad_task_mutex; // Used to guard task struct pointer



// User program opens the driver
static int gamepad_open(struct inode *inode, struct file *filp) {
	// Lock task mutex
	mutex_lock(&gamepad_task_mutex);
	
	if (gamepad_task == NULL) {
		// No other processes using gamepad, set task and unlock mutex
		gamepad_task = current;
		mutex_unlock(&gamepad_task_mutex);

		printk("Opened by PID %i\n", current->pid);
		return 0;
	} else {
		// Gamepad is already in use, unlock mutex
		mutex_unlock(&gamepad_task_mutex);

		printk("Failed attempt to open by PID %i, device busy\n", current->pid);
		return -EBUSY;
	}
}

// User program closes the driver
static int gamepad_release(struct inode *inode, struct file *filp) {
	// Reset task pointer
	mutex_lock(&gamepad_task_mutex);
	gamepad_task = NULL;
	mutex_unlock(&gamepad_task_mutex);

	return 0;
}

// User program reads from the driver
static ssize_t gamepad_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
	if (count >= 1) {
		buff[0] = gamepad_input;
	} else {
		printk("Read buffer size too small for gamepad input data!\n");
	}

	return 1;
}

// User program writes to the driver
static ssize_t gamepad_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp) {
	printk("Write\n");

	return count;
}

// File operations struct for cdev
static struct file_operations gamepad_fops = {
	.owner = THIS_MODULE,
	.read = gamepad_read,
	.write = gamepad_write,
	.open = gamepad_open,
	.release = gamepad_release
};


// Interrupt handler
static irqreturn_t gamepad_irq_handler(int irq, void *dev_id) {
	// Clear interrupt
	iowrite32(ioread32((uint32_t*)(gamepad_res->start + OFF_GPIO_IF)), (uint32_t*)(gamepad_res->start + OFF_GPIO_IFC));

	// Read input
	gamepad_input = ioread32((uint32_t*)(gamepad_res->start + OFF_GPIO_PC_DIN));

	// Send signal to program
	if (gamepad_task != NULL) {
		send_sig_info(SIGUSR1, SEND_SIG_NOINFO, gamepad_task);
	}

	return IRQ_HANDLED;
}


static int tdt4258_probe(struct platform_device *p_dev) {
	int result;

	printk("Device found for gamepad driver\n");

	// Get platform info
	gamepad_res = platform_get_resource(p_dev, IORESOURCE_MEM, 0);
	printk("Name: %s\n", gamepad_res->name);
	printk("Base addr: %x\n", gamepad_res->start);
	gamepad_irq_even = platform_get_irq(p_dev, 0);
	gamepad_irq_odd = platform_get_irq(p_dev, 1);
	printk("Interrupt even: %i, odd: %i\n", gamepad_irq_even, gamepad_irq_odd);

	// Configure GPIO buttons
	iowrite32(0x33333333, (uint32_t*)(gamepad_res->start + OFF_GPIO_PC_MODEL));
	iowrite32(0xFF, (uint32_t*)(gamepad_res->start + OFF_GPIO_PC_DOUT));

	// Register interrupt handler
	result = request_irq(gamepad_irq_even, (irq_handler_t)gamepad_irq_handler,
			0, CDEV_GAMEPAD, 0);
	if (result != 0) return -1; // Failed to set up interrupts
	result = request_irq(gamepad_irq_odd, (irq_handler_t)gamepad_irq_handler,
			0, CDEV_GAMEPAD, 0);
	if (result != 0) return -1; // Failed to set up interrupts

	// Configure GPIO interrupt generation
	iowrite32(0x22222222, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIPSELL));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIFALL));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIRISE));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_IEN));

	// Allocate device number
	result = alloc_chrdev_region(&gamepad_dev, 1, 1, CDEV_GAMEPAD);
	if (result < 0) return -1; // Failed to allocate device number
	printk("Device number allocated: major %i, minor %i\n", MAJOR(gamepad_dev), MINOR(gamepad_dev));

	// Initialize cdev
	cdev_init(&gamepad_cdev, &gamepad_fops);
	result = cdev_add(&gamepad_cdev, gamepad_dev, 1);
	if (result < 0) return -1; // Failed to add cdev

	// Make visible in userspace
	gamepad_cl = class_create(THIS_MODULE, CDEV_GAMEPAD);
	device_create(gamepad_cl, NULL, gamepad_dev, NULL, CDEV_GAMEPAD);
	
	return 0;
}

static int tdt4258_remove(struct platform_device *p_dev) {
	// Disable GPIO interrupt generation
	iowrite32(0x0, (uint32_t*)(gamepad_res->start + OFF_GPIO_IEN));

	// Unregister interrupt handler
	free_irq(gamepad_irq_even, 0);
	free_irq(gamepad_irq_odd, 0);

	// Disable GPIO buttons
	iowrite32(0x0, (uint32_t*)(gamepad_res->start + OFF_GPIO_PC_MODEL));

	// Delete class
	device_destroy(gamepad_cl, gamepad_dev);
	class_destroy(gamepad_cl);

	// Delete cdev
	cdev_del(&gamepad_cdev);

	// Free device number
	unregister_chrdev_region(gamepad_dev, 1);

	return 0;
}

static const struct of_device_id tdt4258_of_match[] = {
	{ .compatible = DEVICE_NAME, },
	{ },
};
MODULE_DEVICE_TABLE(of, tdt4258_of_match);

static struct platform_driver tdt4258_driver = {
	.probe = tdt4258_probe,
	.remove = tdt4258_remove,
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tdt4258_of_match,
	},
};

/*
 * tdt4258_init - function to insert this module into kernel space
 *
 * This is the first of two exported functions to handle inserting this
 * code into a running kernel
 *
 * Returns 0 if successfull, otherwise -1
 */

static int __init tdt4258_init(void)
{
	printk("Hello World, here is your module speaking\n");

	// Register platform driver
	platform_driver_register(&tdt4258_driver);

	// Init task mutex
	mutex_init(&gamepad_task_mutex);

	return 0;
}

/*
 * tdt4258_cleanup - function to cleanup this module from kernel space
 *
 * This is the second of two exported functions to handle cleanup this
 * code from a running kernel
 */

static void __exit tdt4258_cleanup(void)
{
	 printk("Short life for a small module...\n");

	 // Unregister platform driver
	 platform_driver_unregister(&tdt4258_driver);
}

module_init(tdt4258_init);
module_exit(tdt4258_cleanup);

MODULE_DESCRIPTION("Module for accessing gamepad buttons.");
MODULE_LICENSE("GPL");

