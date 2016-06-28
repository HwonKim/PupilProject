#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/time.h>

// User-defined macros //
#define NUM_GPIO_PINS		26
#define MAX_GPIO_NUMBER		28
#define DEVICE_NAME 		"raspi2_gpio"
#define BUF_SIZE			512
#define INTERRUPT_DEVICE_NAME "gpio interrupt"

// User-defined data types //
enum state {low, high};
enum direction {in, out};

/*
	struct raspi2_gpio_dev - Per gpio pin data structure
	@ cdev : instance of struct cdev
	@ pin : instance of struct gpio
	@ state : logic state (low, high) of a GPIO pin
	@ dir : direction of GPIO pin
	@ irq_prem : used to enable/disable interrupt on GPIO pin
	@ irq_flag : used to indicate rising/falling dege trigger
	@ lock : used to protect atomic code section
*/

struct raspi2_gpio_dev{
	struct cdev cdev;
	struct gpio pin;
	enum state state;
	enum direction dir;
	bool irq_perm;
	unsigned long irq_flag;
	unsigned int irq_counter;
	spinlock_t lock;
};

// Foward declaration of functions //
static int raspi2_gpio_init(void);
static void raspi2_gpio_exit(void);

// Declaration of entry points //
static int raspi2_gpio_open(struct inode *inode, struct file *filp);
static ssize_t raspi2_gpio_read( struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t raspi2_gpio_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int raspi2_gpio_release(struct inode *inode, struct file *filp);

// File Operation Structure //
static struct file_operations raspi2_gpio_fops = {
	.owner = THIS_MODULE,
	.open = raspi2_gpio_open,
	.release = raspi2_gpio_release,
	.read = raspi2_gpio_read,
	.write = raspi2_gpio_write,
};

// Global varibles for GPIO driver //
struct raspi2_gpio_dev *raspi2_gpio_devp[NUM_GPIO_PINS];
static dev_t first;
static struct class *raspi2_gpio_class;
static unsigned int last_interrupt_time = 0;
static uint64_t epochMilli;

// Utils Function //
unsigned int millis(void){
	struct timeval timeval;
	uint64_t timeNow;

	do_gettimeofday(&timeval);
	timeNow = (uint64_t)timeval.tv_sec * (uint64_t)1000 + (uint64_t)(timeval.tv_usec/1000);

	return (uint32_t)(timeNow - epochMilli);
}

static irqreturn_t irq_handler(int irq, void *arg){
	unsigned long flags;
	unsigned int interrupt_time = millis();

	if(interrupt_time - last_interrupt_time < 200){
		printk(KERN_NOTICE "Ignored Interrupt [%d]\n", irq);
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
	local_irq_save(flags);
	printk(KERN_NOTICE "Interrupt [%d] was triggered\n", irq);
	local_irq_restore(flags);

	return IRQ_HANDLED;
}
// File Operations Function //
static int raspi2_gpio_open(struct inode *inode, struct file *filp)
{
	struct raspi2_gpio_dev *raspi2_gpio_devp;
	unsigned int gpio;
	int err, irq;
	unsigned long flags;

	gpio = iminor(inode);
	printk(KERN_INFO "GPIO[%d] opened\n", gpio);
	raspi2_gpio_devp = container_of(inode->i_cdev, struct raspi2_gpio_dev, cdev);

	if((raspi2_gpio_devp->irq_perm == true) && (raspi2_gpio_devp->dir == in)){
		if((raspi2_gpio_devp->irq_counter++ == 0)){
			irq = gpio_to_irq(gpio);
			if(raspi2_gpio_devp->irq_flag == IRQF_TRIGGER_RISING){
				spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
				err = request_irq( irq, irq_handler, IRQF_SHARED | IRQF_TRIGGER_RISING, INTERRUPT_DEVICE_NAME, raspi2_gpio_devp);
				printk(KERN_INFO "interrupt requested\n");
				spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
			}else{
				spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
				err = request_irq(irq, irq_handler, IRQF_SHARED | IRQF_TRIGGER_FALLING, INTERRUPT_DEVICE_NAME, raspi2_gpio_devp);
				printk(KERN_INFO "interrupt requested\n");
				spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
			}

			if(err != 0){
				printk(KERN_ERR "unable to claim irq : %d, error %d\n", irq, err);
				return err;
			}
		}
	}
	filp->private_data = raspi2_gpio_devp;
	return 0;
}
static ssize_t raspi2_gpio_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	unsigned int gpio;
	ssize_t retval;
	char byte;

	gpio = iminor(filp->f_path.dentry->d_inode);
	for (retval = 0; retval < count; ++retval){
		byte = '0' + gpio_get_value(gpio);
		if(put_user(byte, buf+retval))
			break;
	}
	return retval;
}
static ssize_t raspi2_gpio_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	unsigned int gpio, len = 0, value = 0; 
	char kbuf[BUF_SIZE];
	struct raspi2_gpio_dev *raspi2_gpio_devp = filp->private_data;
	unsigned long flags;

	gpio = iminor(filp->f_path.dentry->d_inode);
	len = count < BUF_SIZE ? count -1 : BUF_SIZE-1;
	if(copy_from_user(kbuf, buf, len) != 0)
		return -EFAULT;
	kbuf[len] = '\0';

	printk(KERN_INFO "Request from user : %s\n", kbuf);

	if(strcmp(kbuf, "out") == 0){
		printk(KERN_ALERT "gpio[%d] direction set to output\n", gpio);
		if(raspi2_gpio_devp->dir != out){
			spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
			gpio_direction_output(gpio, low);
			raspi2_gpio_devp->dir = out;
			raspi2_gpio_devp->state = low;
			spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
		}
	}else if(strcmp(kbuf, "in") == 0){
		if(raspi2_gpio_devp->dir != in){
			printk(KERN_INFO "Set gpio[%d] direction : input \n", gpio);
			spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
			gpio_direction_input(gpio);
			raspi2_gpio_devp->dir = in;
			spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
		}
	}else if((strcmp(kbuf, "1") == 0) || (strcmp(kbuf,"0") == 0)){
		sscanf(kbuf, "%d", &value);
		if(raspi2_gpio_devp->dir == in){
			printk("Cannot set GPIO %d, direction : input\n", gpio);
			return -EPERM;
		}else if(raspi2_gpio_devp->dir == out){
			if(value > 0){
				spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
				gpio_set_value(gpio, high);
				raspi2_gpio_devp->state = high;
				printk("GPIO %d, state : high\n", gpio);
				spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
			}else{
				spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
				gpio_set_value(gpio, low);
				raspi2_gpio_devp->state = low;
				printk("GPIO %d, state : low\n", gpio);
				spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
			}
		}
		
	}else if((strcmp(kbuf, "rising") == 0) || (strcmp(kbuf, "falling") == 0)){
		spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
		gpio_direction_input(gpio);
		raspi2_gpio_devp -> dir = in;
		raspi2_gpio_devp -> irq_perm = true;
		if(strcmp(kbuf, "rising") == 0)
			raspi2_gpio_devp->irq_flag = IRQF_TRIGGER_RISING;
		else
			raspi2_gpio_devp->irq_flag = IRQF_TRIGGER_FALLING;
		spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
	}else if(strcmp(kbuf, "disable-irq") == 0){
		spin_lock_irqsave(&raspi2_gpio_devp->lock, flags);
		raspi2_gpio_devp->irq_perm = false;
		spin_unlock_irqrestore(&raspi2_gpio_devp->lock, flags);
	}else{
		printk(KERN_ERR "Invalid Value\n");
		return -EINVAL;
	}
	*f_pos += count;
	return count;
}
static int raspi2_gpio_release(struct inode *inode, struct file *filp)
{
	unsigned int gpio;
	struct raspi2_gpio_dev *raspi2_gpio_devp;
	raspi2_gpio_devp = container_of(inode->i_cdev, struct raspi2_gpio_dev, cdev);

	gpio = iminor(inode);

	printk(KERN_INFO "Closing GPIO %d\n", gpio);

	spin_lock(&raspi2_gpio_devp->lock);
	if(raspi2_gpio_devp->irq_perm == true){
		if(raspi2_gpio_devp->irq_counter > 0){
			raspi2_gpio_devp->irq_counter--;
			if(raspi2_gpio_devp->irq_counter == 0){
				printk(KERN_INFO "interrupt on gpio[%d] released\n", gpio);
				free_irq(gpio_to_irq(gpio), raspi2_gpio_devp);
			}
		}
	}
	spin_unlock(&raspi2_gpio_devp->lock);

	if(raspi2_gpio_devp->irq_perm == false && raspi2_gpio_devp->irq_counter > 0){
			spin_lock(&raspi2_gpio_devp->lock);
			free_irq(gpio_to_irq(gpio), raspi2_gpio_devp);
			raspi2_gpio_devp->irq_counter = 0;
			spin_unlock(&raspi2_gpio_devp->lock);
			printk(KERN_INFO "interrupt on gpio[%d] disabled\n", gpio);
	}
	
	return 0;
}

// Init & Exit //
static int raspi2_gpio_init(void)
{
	int i, ret, index = 0;
	struct timeval tv;

	if(alloc_chrdev_region(&first, 0, NUM_GPIO_PINS, DEVICE_NAME) <0){
		printk(KERN_DEBUG "Cannot register device\n");
		return -1;
	}

	if((raspi2_gpio_class = class_create(THIS_MODULE, DEVICE_NAME)) == NULL){
		printk(KERN_DEBUG "Cannot create class %s\n", DEVICE_NAME);
		return -EINVAL;
	}
	
	for(i = 0; i < MAX_GPIO_NUMBER; i++){
		if( i != 0 && i != 1){
			raspi2_gpio_devp[index] = kmalloc(sizeof(struct raspi2_gpio_dev), GFP_KERNEL);

			if(!raspi2_gpio_devp[index]){
				printk("Bad kmalloc\n");
				return -ENOMEM;
			}
			
			if(gpio_request_one(i, GPIOF_OUT_INIT_LOW, NULL) < 0)
			{
				printk(KERN_ALERT "Error requesting GPIO %d\n", i);
				return -ENODEV;
			}

			raspi2_gpio_devp[index]->dir = out;
			raspi2_gpio_devp[index]->state = low;
			raspi2_gpio_devp[index]->irq_perm = false;
			raspi2_gpio_devp[index]->irq_flag = IRQF_TRIGGER_RISING;
			raspi2_gpio_devp[index]->irq_counter = 0;
			raspi2_gpio_devp[index]->cdev.owner = THIS_MODULE;

			spin_lock_init(&raspi2_gpio_devp[index]->lock);

			cdev_init(&raspi2_gpio_devp[index]->cdev, &raspi2_gpio_fops);

			if((ret = cdev_add(&raspi2_gpio_devp[index]->cdev, (first+i), 1))){
				printk(KERN_ALERT "Error %d adding cdev\n", ret);
				for(i = 0; i < MAX_GPIO_NUMBER; i++){
					if(i != 0 && i != 1){
						device_destroy(raspi2_gpio_class, MKDEV(MAJOR(first), MINOR(first) +i));
					}
				}
				class_destroy(raspi2_gpio_class);
				unregister_chrdev_region(first, NUM_GPIO_PINS);

				return ret;
			}

			if(device_create(raspi2_gpio_class, NULL, MKDEV(MAJOR(first), MINOR(first)+i), NULL,
							"raspi2GPIO%d", i) == NULL)
			{
				printk(KERN_ALERT "Create Device File error\n");
				return -1;
			}

			index ++;
		}
	}

	do_gettimeofday(&tv);
	epochMilli = (uint64_t) tv.tv_sec * (uint64_t)1000 + (uint64_t)(tv.tv_usec/1000);
	printk("RaspberryPi GPIO driver initialized\n");
	return 0;
}

static void raspi2_gpio_exit(void)
{
	int i = 0;
	unregister_chrdev_region(first, NUM_GPIO_PINS);

	for(i = 0; i < NUM_GPIO_PINS; i++){
		kfree(raspi2_gpio_devp[i]);
	}

	for(i = 0; i < MAX_GPIO_NUMBER; i++){
		if(i != 0 && i != 1){
			gpio_direction_output(i, 0);
			device_destroy(raspi2_gpio_class, MKDEV(MAJOR(first), MINOR(first)+i));
			gpio_free(i);
		}
	}

	class_destroy(raspi2_gpio_class);

	printk(KERN_INFO "RaspberryPi GPIO driver removed\n");
}

module_init(raspi2_gpio_init);
module_exit(raspi2_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hwon Kim <hwkim107@naver.com>");

