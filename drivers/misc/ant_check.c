#include <linux/init.h>	
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
          
#include <linux/fb.h>


#define CONFIG_ANT_SYS


struct ant_check_info
{
	struct mutex io_lock;
	u32 irq_gpio;
	u32 irq_gpio_flags;
	int irq;
	int ant_check_state;
	struct input_dev *ipdev;
#ifdef CONFIG_ANT_SYS
	struct class *ant_sys_class;
#endif
	struct pinctrl *ant_pinctrl;
	struct pinctrl_state *ant_pinctrl_state;
};

struct ant_check_info *global_ant_info;

static irqreturn_t ant_interrupt(int irq, void *data)
{
	struct ant_check_info *ant_info = data;
	int ant_gpio;

	ant_gpio = gpio_get_value_cansleep(ant_info->irq_gpio);
	pr_err("ant_interrupt gpio value = %d\n", ant_gpio);
	if(ant_gpio == ant_info->ant_check_state){
		pr_err("ant_interrupt do nothing\n");
		return IRQ_HANDLED;
	}else{
		ant_info->ant_check_state = ant_gpio;
	}
	if (ant_gpio) {
			input_report_key(ant_info->ipdev, KEY_ANT_CONNECT, 1);
			input_report_key(ant_info->ipdev, KEY_ANT_CONNECT, 0);
			input_sync(ant_info->ipdev);
	}else{
			input_report_key(ant_info->ipdev, KEY_ANT_UNCONNECT, 1);
			input_report_key(ant_info->ipdev, KEY_ANT_UNCONNECT, 0);
			input_sync(ant_info->ipdev);
	}
	 //enable_irq(irq);
//	 mutex_unlock(&ant_info->io_lock);
     return IRQ_HANDLED;
}

static int ant_parse_dt(struct device *dev, struct ant_check_info *pdata)
{
	struct device_node *np = dev->of_node;

	pdata->irq_gpio = of_get_named_gpio_flags(np, "ant_check,irq_gpio",
				0, &pdata->irq_gpio_flags);
	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;

	return 0;
}

#ifdef CONFIG_ANT_SYS
static ssize_t ant_state_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int state;
	if (global_ant_info->ant_check_state) {
		state = 3;
	}else{
		state = 2;
	}
	pr_err("Macle ant_state_show state = %d, custome_state=%d\n", global_ant_info->ant_check_state, state);
	return sprintf(buf, "%d\n", state);
}

static struct class_attribute ant_state =
	__ATTR(ant_state, S_IRUGO, ant_state_show, NULL);

static int ant_register_class_dev(struct ant_check_info *ant_info){
	int err;
	if (!ant_info->ant_sys_class) {
		ant_info->ant_sys_class = class_create(THIS_MODULE, "ant_class");
		if (IS_ERR(ant_info->ant_sys_class)){
			ant_info->ant_sys_class = NULL;
			printk(KERN_ERR "could not allocate ant_class\n");
			return -1;
		}
	}

	err = class_create_file(ant_info->ant_sys_class, &ant_state);
	if(err < 0){
		class_destroy(ant_info->ant_sys_class);
		return -1;
	}
	return 0;
}
#endif

static void ant_pinctrl_init(struct device *dev, struct ant_check_info *ant_info)
{
	int rc = 0;

	ant_info->ant_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(ant_info->ant_pinctrl)){
		printk("[ant check] get ant_pinctrl error\n");
		return;
	}

	ant_info->ant_pinctrl_state
		= pinctrl_lookup_state(ant_info->ant_pinctrl, "ant_check_irq_state");
	if (IS_ERR_OR_NULL(ant_info->ant_pinctrl_state)){
		devm_pinctrl_put(ant_info->ant_pinctrl);
		printk("[ant check] get ant_pinctrl_state error\n");
		return;
	}

        rc = pinctrl_select_state(ant_info->ant_pinctrl, ant_info->ant_pinctrl_state);
	if(rc < 0){
		printk("[ant check] pinctrl_select_state error\n");
	}

	return;
}

static int ant_probe(struct platform_device *pdev)
{
	int rc = 0;
	int err;
	struct ant_check_info *ant_info;
		
	if (pdev->dev.of_node) {
		ant_info = kzalloc(sizeof(struct ant_check_info), GFP_KERNEL);		  
		if (!ant_info) {
			pr_err("Macle %s: failed to alloc memory for module data\n",__func__);
			return -ENOMEM;
		}
		err = ant_parse_dt(&pdev->dev, ant_info);
		if (err) {
			dev_err(&pdev->dev, "Macle ant_probe DT parsing failed\n");
			goto free_struct;
		}
	} else{
		return -ENOMEM;
	}
	mutex_init(&ant_info->io_lock);

	platform_set_drvdata(pdev, ant_info);

/*input system config*/
	ant_info->ipdev = input_allocate_device();
	if (!ant_info->ipdev) {
		pr_err("ant_probe: input_allocate_device fail\n");
		goto input_error;
	}
	ant_info->ipdev->name = "ant_check-input";
	input_set_capability(ant_info->ipdev, EV_KEY, KEY_ANT_CONNECT);
	input_set_capability(ant_info->ipdev, EV_KEY, KEY_ANT_UNCONNECT);
//	set_bit(EV_KEY, ant_info->ipdev->evbit);
	rc = input_register_device(ant_info->ipdev);
	if (rc) {
		pr_err("ant_probe: input_register_device fail rc=%d\n", rc);
		goto input_error;
	}

	ant_pinctrl_init(&pdev->dev, ant_info);

/*interrupt config*/
	if (gpio_is_valid(ant_info->irq_gpio)) {
		rc = gpio_request(ant_info->irq_gpio, "ant_check");
		if (rc < 0) {
		        pr_err("ant_probe: gpio_request fail rc=%d\n", rc);
		        goto free_input_device;
		}

		rc = gpio_direction_input(ant_info->irq_gpio);
		if (rc < 0) {
		        pr_err("ant_probe: gpio_direction_input fail rc=%d\n", rc);
		        goto err_irq;
		}
		ant_info->ant_check_state = gpio_get_value(ant_info->irq_gpio);
		pr_err("ant_probe: irq_gpio = %d, state=%d\n", ant_info->irq_gpio, ant_info->ant_check_state);

		ant_info->irq = gpio_to_irq(ant_info->irq_gpio);
		pr_err("Macle irq = %d\n", ant_info->irq);

		rc = devm_request_threaded_irq(&pdev->dev, ant_info->irq, NULL,
			ant_interrupt,
			IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING|IRQF_ONESHOT, "ant-switch-irq", ant_info);
		if (rc < 0) {
			pr_err("ant_probe: request_irq fail rc=%d\n", rc);
			goto err_irq;
		}
		device_init_wakeup(&pdev->dev, true);
		enable_irq_wake(ant_info->irq);

	}else{
		pr_err("Macle irq gpio not provided\n");
	        goto free_input_device;
	}

#ifdef CONFIG_ANT_SYS
	ant_register_class_dev(ant_info);
#endif
	   global_ant_info = ant_info;

printk("ant_probe ok\n");

       return 0;
err_irq:
	disable_irq_wake(ant_info->irq);
	device_init_wakeup(&pdev->dev, 0);
	gpio_free(ant_info->irq_gpio);

free_input_device:
	input_unregister_device(ant_info->ipdev);

input_error:
	platform_set_drvdata(pdev, NULL);
free_struct:
	kfree(ant_info);

	return rc;
}

static int ant_remove(struct platform_device *pdev)
{
	struct ant_check_info *ant = platform_get_drvdata(pdev);
#ifdef CONFIG_ANT_SYS
	class_destroy(ant->ant_sys_class);
#endif
	pr_err("ant_remove\n");
	disable_irq_wake(ant->irq);
	device_init_wakeup(&pdev->dev, 0);
	free_irq(ant->irq, ant->ipdev);
	gpio_free(ant->irq_gpio);
	input_unregister_device(ant->ipdev);
	return 0;
}



static struct of_device_id sn_match_table[] = {
	{ .compatible = "ant_check", },
	{ },
}; 

static struct platform_driver ant_driver = {
	.probe                = ant_probe,
	.remove                = ant_remove,
	.driver                = {
		.name        = "ant_check",
		.owner        = THIS_MODULE,
		.of_match_table = sn_match_table,
	},
};

static int __init ant_init(void)
{
	return platform_driver_register(&ant_driver);
}

static void __exit ant_exit(void)
{
	platform_driver_unregister(&ant_driver);
}

module_init(ant_init);
module_exit(ant_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("lisuyang");