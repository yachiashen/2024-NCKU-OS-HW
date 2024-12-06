#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <asm/current.h>

#define procfs_name "Mythread_info"
#define BUFSIZE  1024
char buf[BUFSIZE];

static ssize_t Mywrite(struct file *fileptr, const char __user *ubuf, size_t buffer_len, loff_t *offset){
    /* Do nothing */
	return 0;
}


static ssize_t Myread(struct file *fileptr, char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/
    struct task_struct *thread;
    int buf_len = 0;
    ssize_t ret;

    if (*offset > 0) {
        return 0;
    }

    memset(buf, 0, BUFSIZE);
    
    for_each_thread(current, thread) {
        if (thread->pid == current->pid) {
            continue;
        }
        
        if (buf_len + 100 >= BUFSIZE) {
            break;
        }
        
        buf_len += snprintf(buf + buf_len, BUFSIZE - buf_len,
            "PID: %d, "
            "TID: %d, "
            "Priority: %d, "
            "State: %ld\n",
            current->pid,
            thread->pid, 
            thread->prio, 
            thread->__state);
    }

    ret = min(buffer_len, (size_t)buf_len);
    
    if (copy_to_user(ubuf, buf, ret)) {
        return -EFAULT;
    }
    
    *offset += ret;
    
    return ret;
    /****************/
}

static struct proc_ops Myops = {
    .proc_read = Myread,
    .proc_write = Mywrite,
};

static int My_Kernel_Init(void){
    proc_create(procfs_name, 0644, NULL, &Myops);   
    pr_info("My kernel says Hi");
    return 0;
}

static void My_Kernel_Exit(void){
    pr_info("My kernel says GOODBYE");
}

module_init(My_Kernel_Init);
module_exit(My_Kernel_Exit);

MODULE_LICENSE("GPL");
