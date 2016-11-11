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

#include <asm/io.h>
#include <asm/siginfo.h>
#include <asm/errno.h>

#include "offsets.h"

// Device name
#define DEVICE_NAME "gamepad"

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

// PID of process to signal on interrupt
int gamepad_pid = -1;
struct mutex gamepad_pid_mutex; // Used to guard PID



// User program opens the driver
static int gamepad_open(struct inode *inode, struct file *filp) {
	// Lock PID mutex
	mutex_lock(&gamepad_pid_mutex);
	
	if (gamepad_pid == -1) {
		// No other processes using gamepad, set PID and unlock mutex
		gamepad_pid = current->pid;
		mutex_unlock(&gamepad_pid_mutex);

		printk("Opened by PID %i\n", gamepad_pid);
		return 0;
	} else {
		// Gamepad is already in use, unlock mutex
		mutex_unlock(&gamepad_pid_mutex);

		printk("Failed attempt to open by PID %i, device busy\n", current->pid);
		return -EBUSY;
	}
}

// User program closes the driver
static int gamepad_release(struct inode *inode, struct file *filp) {
	// Reset PID
	mutex_lock(&gamepad_pid_mutex);
	gamepad_pid = -1;
	mutex_unlock(&gamepad_pid_mutex);

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
	// Read input
	gamepad_input = ioread32((uint32_t*)(gamepad_res->start + OFF_GPIO_PC_DIN));

	// Clear interrupt
	iowrite32(ioread32((uint32_t*)(gamepad_res->start + OFF_GPIO_IF)), (uint32_t*)(gamepad_res->start + OFF_GPIO_IFC));

	return IRQ_HANDLED;
}


static int gamepad_probe(struct platform_device *p_dev) {
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
			0, DEVICE_NAME, 0);
	if (result != 0) return -1; // Failed to set up interrupts
	result = request_irq(gamepad_irq_odd, (irq_handler_t)gamepad_irq_handler,
			0, DEVICE_NAME, 0);
	if (result != 0) return -1; // Failed to set up interrupts

	// Configure GPIO interrupt generation
	iowrite32(0x22222222, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIPSELL));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIFALL));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_EXTIRISE));
	iowrite32(0xff, (uint32_t*)(gamepad_res->start + OFF_GPIO_IEN));

	// Allocate device number
	result = alloc_chrdev_region(&gamepad_dev, 1, 1, DEVICE_NAME);
	if (result < 0) return -1; // Failed to allocate device number
	printk("Device number allocated: major %i, minor %i\n", MAJOR(gamepad_dev), MINOR(gamepad_dev));

	// Initialize cdev
	cdev_init(&gamepad_cdev, &gamepad_fops);
	result = cdev_add(&gamepad_cdev, gamepad_dev, 1);
	if (result < 0) return -1; // Failed to add cdev

	// Make visible in userspace
	gamepad_cl = class_create(THIS_MODULE, DEVICE_NAME);
	device_create(gamepad_cl, NULL, gamepad_dev, NULL, DEVICE_NAME);
	
	return 0;
}

static int gamepad_remove(struct platform_device *p_dev) {
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

static const struct of_device_id gamepad_of_match[] = {
	{ .compatible = "tdt4258", },
	{ },
};
MODULE_DEVICE_TABLE(of, gamepad_of_match);

static struct platform_driver gamepad_driver = {
	.probe = gamepad_probe,
	.remove = gamepad_remove,
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = gamepad_of_match,
	},
};

/*
 * gamepad_init - function to insert this module into kernel space
 *
 * This is the first of two exported functions to handle inserting this
 * code into a running kernel
 *
 * Returns 0 if successfull, otherwise -1
 */

static int __init gamepad_init(void)
{
	printk("Hello World, here is your module speaking\n");

	// Register platform driver
	platform_driver_register(&gamepad_driver);

	// Init PID mutex
	mutex_init(&gamepad_pid_mutex);

	return 0;
}

/*
 * gamepad_cleanup - function to cleanup this module from kernel space
 *
 * This is the second of two exported functions to handle cleanup this
 * code from a running kernel
 */

static void __exit gamepad_cleanup(void)
{
	 printk("Short life for a small module...\n");

	 // Unregister platform driver
	 platform_driver_unregister(&gamepad_driver);
}

module_init(gamepad_init);
module_exit(gamepad_cleanup);

MODULE_DESCRIPTION("Module for accessing gamepad buttons.");
MODULE_LICENSE("GPL");

