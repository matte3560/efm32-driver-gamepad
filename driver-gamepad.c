/*
 * This is a gamepad Linux kernel module.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>

// Device number used for gamepad
dev_t gamepad_dev;

// cdev struct
struct cdev gamepad_cdev;

// Class struct
struct class *gamepad_cl;



// User program opens the driver
static int gamepad_open(struct inode *inode, struct file *filp) {
	return 0;
}

// User program closes the driver
static int gamepad_release(struct inode *inode, struct file *filp) {
	return 0;
}

// User program reads from the driver
static ssize_t gamepad_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
	return 0;
}

// User program writes to the driver
static ssize_t gamepad_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp) {
	return 0;
}

// File operations struct for cdev
static struct file_operations gamepad_fops = {
	.owner = THIS_MODULE,
	.read = gamepad_read,
	.write = gamepad_write,
	.open = gamepad_open,
	.release = gamepad_release
};


static int gamepad_probe(struct platform_device *p_dev) {
	int result;

	printk("Device found for gamepad driver\n");

	// Allocate device number
	result = alloc_chrdev_region(&gamepad_dev, 1, 1, "gamepad");
	if (result != 0) {
		return -1; // Failed to allocate device number
	}
	printk("Device number allocated: major %i, minor %i\n", MAJOR(gamepad_dev), MINOR(gamepad_dev));

	// Initialize cdev
	cdev_init(&gamepad_cdev, &gamepad_fops);
	cdev_add(&gamepad_cdev, gamepad_dev, 1);

	// Make visible in userspace
	gamepad_cl = class_create(THIS_MODULE, "gamepad");
	device_create(gamepad_cl, NULL, gamepad_dev, NULL, "gamepad");
	
	return 0;
}

static int gamepad_remove(struct platform_device *p_dev) {
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
		.name = "gamepad",
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

