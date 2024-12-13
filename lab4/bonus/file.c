#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * 從給定的偏移量開始找到對應的block與在block內的offset。
 * Inputs:
 *   sb_info, osfs_inode, offset
 * Outputs:
 *   *block_no: 目標的block編號
 *   *block_offset: 在該block內的偏移
 * Returns:
 *   0成功, 否則負值
 */
static int osfs_find_block(struct osfs_sb_info *sb_info, struct osfs_inode *osfs_inode, loff_t offset,
                           uint32_t *block_no, size_t *block_offset)
{
    if (osfs_inode->i_head == OSFS_NO_NEXT_BLOCK) {
        return -ENODATA; // no data block allocated yet
    }

    size_t remaining = offset;
    uint32_t curr = osfs_inode->i_head;
    struct osfs_block_header *hdr;
    while (curr != OSFS_NO_NEXT_BLOCK) {
        hdr = (struct osfs_block_header *)(sb_info->data_blocks + curr * BLOCK_SIZE);

        if (remaining < (BLOCK_SIZE - sizeof(uint32_t))) {
            *block_no = curr;
            *block_offset = remaining;
            return 0;
        }

        remaining -= (BLOCK_SIZE - sizeof(uint32_t));
        curr = hdr->next_block;
    }

    return -EFAULT;
}

static ssize_t osfs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;

    if (*ppos >= osfs_inode->i_size)
        return 0;

    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    ssize_t bytes_read = 0;
    size_t to_read = len;

    loff_t curr_pos = *ppos;

    while (to_read > 0) {
        uint32_t block_no;
        size_t block_offset;
        int ret = osfs_find_block(sb_info, osfs_inode, curr_pos, &block_no, &block_offset);
        if (ret < 0)
            break;

        struct osfs_block_header *hdr = (struct osfs_block_header *)(sb_info->data_blocks + block_no * BLOCK_SIZE);
        size_t avail = (BLOCK_SIZE - sizeof(uint32_t)) - block_offset;
        size_t chunk = (to_read < avail) ? to_read : avail;
        if (copy_to_user(buf + bytes_read, hdr->data + block_offset, chunk))
            return -EFAULT;

        curr_pos += chunk;
        to_read -= chunk;
        bytes_read += chunk;
    }

    *ppos += bytes_read;
    return bytes_read;
}

/**
 * 為了寫入，我們若需要在檔案尾端增加block，則配置新block並串接。
 */
static int osfs_append_block(struct osfs_sb_info *sb_info, struct osfs_inode *osfs_inode, uint32_t *new_block)
{
    int ret = osfs_alloc_data_block(sb_info, new_block);
    if (ret < 0)
        return ret;

    if (osfs_inode->i_head == OSFS_NO_NEXT_BLOCK) {
        // 檔案目前沒有block
        osfs_inode->i_head = *new_block;
        osfs_inode->i_tail = *new_block;
    } else {
        // 串接在尾端
        struct osfs_block_header *tail_hdr = (struct osfs_block_header *)(sb_info->data_blocks + osfs_inode->i_tail * BLOCK_SIZE);
        tail_hdr->next_block = *new_block;
        osfs_inode->i_tail = *new_block;
    }
    osfs_inode->i_blocks++;
    return 0;
}

/**
 * 從檔案末端開始增加直到包含offset的位置
 */
static int osfs_ensure_offset(struct osfs_sb_info *sb_info, struct osfs_inode *osfs_inode, loff_t offset)
{
    // 若offset超過現有區塊串的範圍，要增加足夠的block
    loff_t size_in_blocks = (BLOCK_SIZE - sizeof(uint32_t));
    loff_t required_blocks = (offset / size_in_blocks) + 1;
    if ((loff_t)osfs_inode->i_blocks >= required_blocks)
        return 0;

    // 需要增加的區塊數
    loff_t add = required_blocks - osfs_inode->i_blocks;
    while (add > 0) {
        uint32_t nb;
        int ret = osfs_append_block(sb_info, osfs_inode, &nb);
        if (ret < 0)
            return ret;
        add--;
    }
    return 0;
}


static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{   
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;

    // 確保包含要寫入的範圍都有block
    loff_t end_offset = *ppos + len;
    int ret = osfs_ensure_offset(sb_info, osfs_inode, end_offset - 1);
    if (ret < 0)
        return ret;

    ssize_t bytes_written = 0;
    size_t to_write = len;
    loff_t curr_pos = *ppos;

    while (to_write > 0) {
        uint32_t block_no;
        size_t block_offset;
        ret = osfs_find_block(sb_info, osfs_inode, curr_pos, &block_no, &block_offset);
        if (ret < 0)
            return ret;

        struct osfs_block_header *hdr = (struct osfs_block_header *)(sb_info->data_blocks + block_no * BLOCK_SIZE);

        size_t avail = (BLOCK_SIZE - sizeof(uint32_t)) - block_offset;
        size_t chunk = (to_write < avail) ? to_write : avail;
        if (copy_from_user(hdr->data + block_offset, buf + bytes_written, chunk))
            return -EFAULT;

        curr_pos += chunk;
        to_write -= chunk;
        bytes_written += chunk;
    }

    if (*ppos + bytes_written > osfs_inode->i_size) {
        osfs_inode->i_size = *ppos + bytes_written;
        inode->i_size = osfs_inode->i_size;
    }

    *ppos += bytes_written;
    inode->__i_mtime = inode->__i_ctime = current_time(inode);
    osfs_inode->__i_mtime = osfs_inode->__i_ctime = inode->__i_mtime;
    mark_inode_dirty(inode);

    pr_info("osfs_write: Written %ld bytes to inode %lu\n", bytes_written, inode->i_ino);
    return bytes_written;
}

const struct file_operations osfs_file_operations = {
    .open = generic_file_open,
    .read = osfs_read,
    .write = osfs_write,
    .llseek = default_llseek,
};

const struct inode_operations osfs_file_inode_operations = {
};
