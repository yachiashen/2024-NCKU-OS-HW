#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

struct osfs_inode *osfs_get_osfs_inode(struct super_block *sb, uint32_t ino)
{
    struct osfs_sb_info *sb_info = sb->s_fs_info;

    if (ino == 0 || ino >= sb_info->inode_count) // File system inode count upper bound
        return NULL;
    return &((struct osfs_inode *)(sb_info->inode_table))[ino];
}

int osfs_get_free_inode(struct osfs_sb_info *sb_info)
{
    uint32_t ino;

    for (ino = 1; ino < sb_info->inode_count; ino++) {
        if (!test_bit(ino, sb_info->inode_bitmap)) {
            set_bit(ino, sb_info->inode_bitmap);
            sb_info->nr_free_inodes--;
            return ino;
        }
    }
    pr_err("osfs_get_free_inode: No free inode available\n");
    return -ENOSPC;
}

struct inode *osfs_iget(struct super_block *sb, unsigned long ino)
{
    struct osfs_inode *osfs_inode;
    struct inode *inode;

    osfs_inode = osfs_get_osfs_inode(sb, ino);
    if (!osfs_inode)
        return ERR_PTR(-EFAULT);

    inode = new_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_mode = osfs_inode->i_mode;
    i_uid_write(inode, osfs_inode->i_uid);
    i_gid_write(inode, osfs_inode->i_gid);
    inode->__i_atime = osfs_inode->__i_atime;
    inode->__i_mtime = osfs_inode->__i_mtime;
    inode->__i_ctime = osfs_inode->__i_ctime;
    inode->i_size = osfs_inode->i_size;
    inode->i_blocks = osfs_inode->i_blocks;
    inode->i_private = osfs_inode;

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &osfs_dir_inode_operations;
        inode->i_fop = &osfs_dir_operations;
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_op = &osfs_file_inode_operations;
        inode->i_fop = &osfs_file_operations;
    }

    insert_inode_hash(inode);

    return inode;
}

int osfs_alloc_data_block(struct osfs_sb_info *sb_info, uint32_t *block_no)
{
    uint32_t i;
    for (i = 0; i < sb_info->block_count; i++) {
        if (!test_bit(i, sb_info->block_bitmap)) {
            set_bit(i, sb_info->block_bitmap);
            sb_info->nr_free_blocks--;
            *block_no = i;

            // 初始化區塊的 next_block 為 OSFS_NO_NEXT_BLOCK
            {
                struct osfs_block_header *hdr = (struct osfs_block_header *)(sb_info->data_blocks + i * BLOCK_SIZE);
                hdr->next_block = OSFS_NO_NEXT_BLOCK;
            }

            return 0;
        }
    }
    pr_err("osfs_alloc_data_block: No free data block available\n");
    return -ENOSPC;
}

void osfs_free_data_block(struct osfs_sb_info *sb_info, uint32_t block_no)
{
    clear_bit(block_no, sb_info->block_bitmap);
    sb_info->nr_free_blocks++;
}
