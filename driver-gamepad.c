/*
 * This is a gamepad Linux kernel module.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/fs.h>

// Device number used for gamepad
dev_t dev;


static int gamepad_probe(struct platform_device *p_dev) {
	int result;

	printk("Device found for gamepad driver\n");

	// Allocate device number
	result = alloc_chrdev_region(&dev, 0, 1, "gamepad");
	if (result != 0) {
		return -1; // Failed to allocate device number
	}
	printk("Device number allocated: major %i, minor %i\n", MAJOR(dev), MINOR(dev));
	
	return 0;
}

static int gamepad_remove(struct platform_device *p_dev) {
	// Free device number
	unregister_chrdev_region(dev, 1);

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

