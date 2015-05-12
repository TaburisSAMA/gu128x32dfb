/*
 *    Filename: gu128x32dfb.c
 *    Version: 0.1.0
 *    Description: gu128x32d-7000 LCD framebuffer driver
 *    License: GPLv2
 *    Author: Copyright (C) bqvision
 *    Date: 2013-9-22
 
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>


#define GU128X32DFB_NAME        "gu128x32dfb"

#define GU128X32D_WIDTH	        (128)
#define GU128X32D_HEIGHT	(32)
#define GU128X32D_PAGE          (8)
#define GU128X32D_SIZE		(GU128X32D_WIDTH * (GU128X32D_HEIGHT/GU128X32D_PAGE))
#define GU128X32D_RATE          20

/*
 * linux gpio number = (gpio_bank - 1) * 32 + gpio_bit
 * eg:GPIO1_IO17 maps to (1 - 1) * 32 + 17 = 17
 * 参考：1、arch/arm/plat-mxc/include/mach/iomux-mx6q.h
 *       2、http://www.kosagi.com/w/index.php?title=Definitive_GPIO_guide       
 *
 * */
#define GPIO_LCD_DIRECTION      138     /* MX6Q_PAD_DISP0_DAT16__GPIO_5_10 */
#define GPIO_LCD_RS             135     /* _MX6Q_PAD_DISP0_DAT13__GPIO_5_7 */  
#define GPIO_LCD_NWR            136     /* MX6Q_PAD_CSI0_DAT14__GPIO_5_8 */
#define GPIO_LCD_NRD            137     /* MX6Q_PAD_DISP0_DAT15__GPIO_5_9 */
#define GPIO_LCD_DATA           120     /* MX6Q_PAD_DISP0_DAT3__GPIO_4_24 */
#define LCD_READ_DELAY          4000

static struct gpio user_gpio_inst[12];

static struct platform_device *gu128x32dfb_device;

static unsigned char *gu128x32d_buffer;
static unsigned char *gu128x32d_cache;
static DEFINE_MUTEX(gu128x32d_mutex);
static unsigned char gu128x32d_updating;
static void gu128x32d_update(struct work_struct *delayed_work);
static struct workqueue_struct *gu128x32d_workqueue;
static DECLARE_DELAYED_WORK(gu128x32d_work, gu128x32d_update);
static unsigned char gu128x32d_rate;
static unsigned char init_display_char[GU128X32D_SIZE]=
{
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x2,0x2,0xc2,0x2,0x2,0x2,0x2,0xfe,0x82,0x82,0x82,0x82,0x82,
0x2,0x0,0x0,0x4,0x4,0xc4,0x64,0x9c,0x87,0x84,0x84,0xe4,0x84,0x84,
0x84,0x84,0x4,0x0,0x0,0x0,0x0,0xf8,0x48,0x48,0x48,0x49,0x4e,0x4a,
0x48,0x48,0x48,0x78,0x0,0x0,0x20,0x24,0x24,0xe4,0x24,0x24,0x24,
0x20,0x10,0x10,0xff,0x10,0x10,0xf0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x20,0x20,0x20,0x3f,
0x20,0x20,0x20,0x20,0x3f,0x20,0x20,0x20,0x20,0x20,0x20,0x0,0x4,
0x2,0x1,0x7f,0x0,0x20,0x20,0x20,0x20,0x3f,0x20,0x20,0x20,0x20,
0x20,0x0,0x40,0x20,0x18,0x7,0x0,0x7e,0x22,0x22,0x22,0x22,0x22,
0x22,0x22,0x7e,0x0,0x0,0x8,0x1c,0xb,0x8,0xc,0x5,0x4e,0x24,0x10,
0xc,0x3,0x20,0x40,0x3f,0x0,0x0,0x0,0x18,0x24,0x24,0x18,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x18,0x24,0x24,0x18,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x18,0x24,0x24,0x18,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
};//正在启动。。。

static struct fb_fix_screeninfo gu128x32dfb_fix = {
	.id = "gu128x32d",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_MONO10,
	.xpanstep = 0,
	.ypanstep = 0,
	.ywrapstep = 0,
	.line_length = GU128X32D_WIDTH / 8,
	.accel = FB_ACCEL_NONE,
};

static struct fb_var_screeninfo gu128x32dfb_var = {
	.xres = GU128X32D_WIDTH,
	.yres = GU128X32D_HEIGHT,
	.xres_virtual = GU128X32D_WIDTH,
	.yres_virtual = GU128X32D_HEIGHT,
	.bits_per_pixel = 1,
	.red = { 0, 1, 0 },
      	.green = { 0, 1, 0 },
      	.blue = { 0, 1, 0 },
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};

static int gu128x32dfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return vm_insert_page(vma, vma->vm_start,
		virt_to_page(gu128x32d_buffer));
}

static struct fb_ops gu128x32dfb_ops = {
	.owner = THIS_MODULE,
	.fb_read =NULL,
	.fb_write =NULL,
	.fb_fillrect =NULL,
	.fb_copyarea = NULL,
	.fb_imageblit =NULL,
	.fb_mmap = gu128x32dfb_mmap,
};

static int gu128x32fb_gpio_init(void)
{
	int i;
	int ret;
	user_gpio_inst[0].gpio=GPIO_LCD_DIRECTION;
	user_gpio_inst[0].flags=GPIOF_DIR_OUT;
	user_gpio_inst[0].label=NULL;
	user_gpio_inst[1].gpio=GPIO_LCD_RS;
	user_gpio_inst[1].flags=GPIOF_DIR_OUT;
	user_gpio_inst[1].label=NULL;
	user_gpio_inst[2].gpio=GPIO_LCD_NWR;
	user_gpio_inst[2].flags=GPIOF_DIR_OUT;
	user_gpio_inst[2].label=NULL;
	user_gpio_inst[3].gpio=GPIO_LCD_NRD;
	user_gpio_inst[3].flags=GPIOF_DIR_OUT;
	user_gpio_inst[3].label=NULL;
    for(i=0;i<8;i++)
	{
		user_gpio_inst[4+i].gpio=GPIO_LCD_DATA+i;
		user_gpio_inst[4+i].flags=GPIOF_DIR_OUT;
		user_gpio_inst[4+i].label="D"+(i+0x30);
	}
        for(i=0;i<12;i++)
        {
            ret=gpio_request(user_gpio_inst[i].gpio,NULL);
        
        }
	if(ret)
	{
		printk("gu128x32fb_gpio_init fail!\n");
		return ret;
	}

    gpio_direction_output(GPIO_LCD_DIRECTION,1);
    gpio_direction_output(GPIO_LCD_RS,1);
    gpio_direction_output(GPIO_LCD_NWR,1);
	gpio_direction_output(GPIO_LCD_NRD,1);
	for(i=0;i<8;i++)
		gpio_direction_output(GPIO_LCD_DATA+i,1);
#if 0
    gpio_set_value(GPIO_LCD_DIRECTION,1);
    gpio_set_value(GPIO_LCD_RS,1);
    gpio_set_value(GPIO_LCD_NWR,1);
	gpio_set_value(GPIO_LCD_NRD,1);
	for(i=0;i<8;i++)
		gpio_set_value(GPIO_LCD_DATA+i,1);
#endif
	return ret;


}
static void gu128x32fb_gpio_exit(void)
{
    int i;
    for(i=0;i<12;i++)
	gpio_free(user_gpio_inst[i].gpio);
}
static void  gu128x32_set_lcd_data(unsigned char * lcd_data)
{
	int i;
	unsigned char temp;
	temp=*lcd_data;
	for(i=0;i<8;i++)
	{
	
            if(temp & 0x01)
			gpio_set_value(GPIO_LCD_DATA+i,1);
		else
			gpio_set_value(GPIO_LCD_DATA+i,0);
            temp=temp>>1;
                            
	}

}
static void gu128x32_write_data(unsigned char   lcd_data)
{
	gu128x32_set_lcd_data(&lcd_data);
	udelay(200);
	gpio_set_value(GPIO_LCD_NWR,0);
	udelay(100);
	gpio_set_value(GPIO_LCD_NWR,1);

}

static void gu128x32_clear_screen(void)
{   
     gu128x32_write_data(0x0c);   
}    

static void gu128x32_vfd_power_on(void)
{
     gu128x32_write_data(0x1f);   
     gu128x32_write_data(0x28);   
     gu128x32_write_data(0x61);
     gu128x32_write_data(0x40); 
	 gu128x32_write_data(0x01);	  
}  
static void gu128x32_vfd_power_off(void)
{
     gu128x32_write_data(0x1f);   
     gu128x32_write_data(0x28);   
     gu128x32_write_data(0x61);
     gu128x32_write_data(0x40); 
	 gu128x32_write_data(0x00);	  
} 

static void gu128x32_select_current_window(unsigned char winNC)   
{   
     gu128x32_write_data(0x1f);   
     gu128x32_write_data(0x28);   
     gu128x32_write_data(0x77);   
     gu128x32_write_data(0x01);   
     gu128x32_write_data(winNC);   
} 

static void gu128x32_init_vfd(void)
{
	gpio_set_value(GPIO_LCD_RS,1);
	gpio_set_value(GPIO_LCD_NWR,1);
	gpio_set_value(GPIO_LCD_NRD,1);
	gu128x32_write_data(0x1b);
	gu128x32_write_data(0x40);
	gu128x32_select_current_window(0);
}

static void gu128x32_set_cursor_pos(int x,int y)   
{   
	gu128x32_write_data(0x1f);   
	gu128x32_write_data(0x24);   
	gu128x32_write_data(x & 0xFF);   
	gu128x32_write_data((x >> 8) & 0xFF);   
	gu128x32_write_data(y & 0xFF);   
	gu128x32_write_data((y >> 8) & 0xFF);   
}  
static void gu128x32_set_bitmap(void)
{
	gu128x32_write_data(0x1f);
	gu128x32_write_data(0x28);
	gu128x32_write_data(0x66);
	gu128x32_write_data(0x11);
	gu128x32_write_data(GU128X32D_WIDTH);
	gu128x32_write_data(0);
	gu128x32_write_data(GU128X32D_HEIGHT/8);
	gu128x32_write_data(0);
	gu128x32_write_data(0x01);
	
} 
static int gu128x32dfb_probe(struct platform_device *device)
{
	int ret = -EINVAL;
 	struct fb_info *info = framebuffer_alloc(0, &device->dev);

	if (!info)
		goto none;

	info->screen_base = (char __iomem *) gu128x32d_buffer;
	info->screen_size = GU128X32D_SIZE;
	info->fbops = &gu128x32dfb_ops;
	info->fix = gu128x32dfb_fix;
	info->var = gu128x32dfb_var;
	info->pseudo_palette = NULL;
	info->par = NULL;
	info->flags = FBINFO_FLAG_DEFAULT;

	if (register_framebuffer(info) < 0)
		goto fballoced;

	platform_set_drvdata(device, info);

	printk(KERN_INFO "fb%d: %s frame buffer device sucess!\n", info->node,
		info->fix.id);

	return 0;

fballoced:
	framebuffer_release(info);

none:
	return ret;
}

static int gu128x32dfb_remove(struct platform_device *device)
{
	struct fb_info *info = platform_get_drvdata(device);

	if (info) {
		unregister_framebuffer(info);
		framebuffer_release(info);
	}

	return 0;
}

static struct platform_driver gu128x32dfb_driver = {
	.probe	= gu128x32dfb_probe,
	.remove = gu128x32dfb_remove,
	.driver = {
		.name	= GU128X32DFB_NAME,
	},
};


static void gu128x32d_queue(void)
{
	queue_delayed_work(gu128x32d_workqueue, &gu128x32d_work,
		HZ / gu128x32d_rate);
}

static unsigned char gu128x32d_enable(void)
{
	unsigned char ret;

	mutex_lock(&gu128x32d_mutex);

	if (!gu128x32d_updating) {
		gu128x32d_updating = 1;
		gu128x32d_queue();
		ret = 0;
	} else
		ret = 1;

	mutex_unlock(&gu128x32d_mutex);

	return ret;
}

static void gu128x32d_disable(void)
{
	mutex_lock(&gu128x32d_mutex);

	if (gu128x32d_updating) {
		gu128x32d_updating = 0;
		cancel_delayed_work(&gu128x32d_work);
		flush_workqueue(gu128x32d_workqueue);
	}

	mutex_unlock(&gu128x32d_mutex);
}

   
static unsigned char reserv(unsigned char n) 
{ 
  unsigned i; 
  unsigned r = 0;
  unsigned char shift;

  shift = sizeof(n)*8;
  for(i = 1; i <= shift; i++) 
  { 
        r |= ((n&1)<<(shift - i));
        n >>= 1; 
  } 
  return r; 
}
static void gu128x32d_update(struct work_struct *work)
{
	unsigned short i;

	if (memcmp(gu128x32d_cache, gu128x32d_buffer, GU128X32D_SIZE)) {
            //gu128x32_clear_screen();
		gu128x32_set_cursor_pos(0,0);
		gu128x32_set_bitmap();
		for (i = 0; i < GU128X32D_WIDTH; i++) 
		{
			gu128x32_write_data(reserv(gu128x32d_buffer[i]));
			gu128x32_write_data(reserv(gu128x32d_buffer[GU128X32D_WIDTH+i]));
			gu128x32_write_data(reserv(gu128x32d_buffer[GU128X32D_WIDTH * 2 +i]));
			gu128x32_write_data(reserv(gu128x32d_buffer[GU128X32D_WIDTH * 3 +i]));
		}
		memcpy(gu128x32d_cache, gu128x32d_buffer, GU128X32D_SIZE);
                
	}

	if (gu128x32d_updating)
		gu128x32d_queue();
}

static int __init gu128x32dfb_init(void)
{
	int ret = -EINVAL;

	gu128x32d_rate=GU128X32D_RATE ;//20HZ

        gu128x32d_buffer = (unsigned char *) get_zeroed_page(GFP_KERNEL);
	if (gu128x32d_buffer == NULL) {
		printk(KERN_ERR GU128X32DFB_NAME  ": ERROR: "
			"can't get a free page\n");
		ret = -ENOMEM;
		goto none;
	}

	gu128x32d_cache = kmalloc(sizeof(unsigned char) * GU128X32D_SIZE, GFP_KERNEL);
	if (gu128x32d_cache == NULL) {
		printk(KERN_ERR GU128X32DFB_NAME ": ERROR: "
			"can't alloc cache buffer (%i bytes)\n",
			GU128X32D_SIZE);
		ret = -ENOMEM;
		goto bufferalloced;
	}

       memcpy(gu128x32d_buffer,init_display_char,GU128X32D_SIZE);
       gu128x32d_workqueue = create_singlethread_workqueue(GU128X32DFB_NAME);
	if (gu128x32d_workqueue == NULL)
		goto cachealloced;

	gu128x32dfb_device =platform_device_alloc(GU128X32DFB_NAME, 0);

	if (gu128x32dfb_device)
		ret = platform_device_add(gu128x32dfb_device);
	else
		goto workqueuealloced;

	ret = platform_driver_register(&gu128x32dfb_driver);

	if (ret) {
		platform_device_put(gu128x32dfb_device);
		platform_driver_unregister(&gu128x32dfb_driver);
		goto workqueuealloced;
	}
	else
	{
               
                gu128x32fb_gpio_init();
		gu128x32_init_vfd();
		gu128x32_vfd_power_on();
		gu128x32d_enable();
		goto none;
	}
        printk("%s:%d", __FUNCTION__, __LINE__);

workqueuealloced:
	destroy_workqueue(gu128x32d_workqueue);
cachealloced:
	kfree(gu128x32d_cache);

bufferalloced:
	free_page((unsigned long) gu128x32d_buffer);

none:
	return ret;
}

static void __exit gu128x32dfb_exit(void)
{
        printk("%s:%d", __FUNCTION__, __LINE__);
	gu128x32_vfd_power_off();
	platform_device_unregister(gu128x32dfb_device);
	platform_driver_unregister(&gu128x32dfb_driver);
	gu128x32d_disable();
	destroy_workqueue(gu128x32d_workqueue);
	kfree(gu128x32d_cache);
	free_page((unsigned long) gu128x32d_buffer);
	gu128x32fb_gpio_exit();
}

module_init(gu128x32dfb_init);
module_exit(gu128x32dfb_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("LYN <taburissama@gmail.com>");
MODULE_DESCRIPTION("gu128x32d LCD framebuffer driver");
