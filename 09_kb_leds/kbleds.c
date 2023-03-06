#include <linux/init.h>
#include <linux/kd.h> /* For KDSETLED */
#include <linux/module.h>
#include <linux/tty.h> /* For tty_struct */
#include <linux/vt.h> /* For MAX_NR_CONSOLES */
#include <linux/vt_kern.h> /* for fg_console */
#include <linux/console_struct.h> /* For vc_cons */

#define BLINK_DELAY (HZ / 2)
#define ALL_LEDS_ON (LED_CAP | LED_NUM | LED_SCR)
#define RESTORE_LEDS 0xFF

static struct timer_list my_timer;
static struct tty_driver *my_driver;
static unsigned long kbledstatus = 0;

/* Function my_timer_func blinks the keyboard LEDs periodically by invoking
 * command KDSETLED of ioctl() on the keyboard driver.
 * The argument to KDSETLED is alternatively set to 7 (thus causing the led
 * mode to be set to LED_SHOW_IOCTL, and all the leds are lit) and to 0xFF
 * (any value above 7 switches back the led mode to LED_SHOW_FLAGS, thus
 * the LEDs reflect the actual keyboard status).  To learn more on this,
 * please see file: drivers/tty/vt/keyboard.c, function setledstate().
 */

static void my_timer_func(struct timer_list *unused)
{
	struct tty_struct *t = vc_cons[fg_console].d->port.tty;

	if (kbledstatus == ALL_LEDS_ON)
		kbledstatus = RESTORE_LEDS;
	else
		kbledstatus = ALL_LEDS_ON;

	(my_driver->ops->ioctl)(t, KDSETLED, kbledstatus);

	my_timer.expires = jiffies + BLINK_DELAY;
	add_timer(&my_timer);
}

static int __init kbleds_init(void)
{
	int i;

	pr_info("kbleds: loading\n");
	pr_info("kbleds: fgconsole is %x\n", fg_console);

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons[i].d)
			break;
		pr_info("poet_atkm: console[%i/%i] #%i, tty %p, itty %p\n", i,
			MAX_NR_CONSOLES, vc_cons[i].d->vc_num,
			(void *)vc_cons[i].d->port.tty,
			(void *)vc_cons[i].d->port.itty);
	}
	pr_info("kbleds: finished scanning consoles\n");

	if (!vc_cons[fg_console].d->port.tty)
		return -EFAULT;

	my_driver = vc_cons[fg_console].d->port.tty->driver;
	pr_info("kbleds:%d tty driver magic %x\n", fg_console,
		my_driver->magic);

	/* Set up the LED blink timer the first time. */
	timer_setup(&my_timer, my_timer_func, 0);
	my_timer.expires = jiffies + BLINK_DELAY;
	add_timer(&my_timer);

	return 0;
}

static void __exit kbleds_exit(void)
{
	pr_info("kbleds: unloading...\n");
	del_timer(&my_timer);
	(my_driver->ops->ioctl)(vc_cons[fg_console].d->port.tty, KDSETLED,
				RESTORE_LEDS);
}

module_init(kbleds_init);
module_exit(kbleds_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Example module illustrating the use of Keyboard LEDs.");
MODULE_VERSION("0.1");