#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <asm/current.h>

#define procfs_name "Mythread_info"
#define BUFSIZE  1024
char buf[BUFSIZE]; //kernel buffer

static ssize_t Mywrite(struct file *fileptr, const char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/
    ssize_t ret;
    char local_buf[BUFSIZE];

    if (buffer_len >= BUFSIZE) {
        pr_err("Write failed: buffer too large\n");
        return -EINVAL;
    }

    if (copy_from_user(local_buf, ubuf, buffer_len)) {
        pr_err("Write failed: unable to copy from user\n");
        return -EFAULT;
    }

    local_buf[buffer_len] = '\0';

    memset(buf, 0, BUFSIZE);

    ret = snprintf(buf, BUFSIZE, "Received: %s", local_buf);

    pr_info("Proc file write: %s", ret);

    return buffer_len;
    /****************/
}


static ssize_t Myread(struct file *fileptr, char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/
    struct task_struct *thread;
    int buf_len = strlen(buf);
    ssize_t ret;

    if (*offset > 0) {
        return 0;
    }
    
    if (buf_len + 100 >= BUFSIZE) {
        return 0;
    }
    
    buf_len += snprintf(buf + buf_len, BUFSIZE - buf_len,
        "\nPID: %d, "
        "TID: %d, "
        "time: %lld\n",
        current->tgid,
        current->pid, 
        current->utime/100/1000
    );
    

    ret = min(buffer_len, (size_t)buf_len);
    
    if (copy_to_user(ubuf, buf, ret)) {
        pr_err("Read failed: unable to copy to user\n");
        return -EFAULT;
    }
    
    *offset += ret;
    
    memset(buf, 0, BUFSIZE);
    
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
    remove_proc_entry(procfs_name, NULL);
}

module_init(My_Kernel_Init);
module_exit(My_Kernel_Exit);

MODULE_LICENSE("GPL");
