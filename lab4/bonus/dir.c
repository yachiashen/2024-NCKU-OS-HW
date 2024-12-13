#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "osfs.h"

/**
 * 在 Linked Allocation 下，我們必須遍歷所有鏈結的 data blocks。
 * 本函式用來遍歷目錄的所有 data block，以找尋檔案。
 */
static struct dentry *osfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    struct osfs_sb_info *sb_info = dir->i_sb->s_fs_info;
    struct osfs_inode *parent_inode = dir->i_private;
    uint32_t block_no = parent_inode->i_head;
    struct osfs_block_header *hdr;
    int dir_entry_count;
    int i;

    pr_info("osfs_lookup: Looking up '%.*s' in inode %lu\n",
            (int)dentry->d_name.len, dentry->d_name.name, dir->i_ino);

    while (block_no != OSFS_NO_NEXT_BLOCK) {
        hdr = (struct osfs_block_header *)(sb_info->data_blocks + block_no * BLOCK_SIZE);

        dir_entry_count = parent_inode->i_size / sizeof(struct osfs_dir_entry);
        if (dir_entry_count > MAX_DIR_ENTRIES)
            dir_entry_count = MAX_DIR_ENTRIES;

        struct osfs_dir_entry *dir_entries = (struct osfs_dir_entry *)hdr->data;
        int entries_in_this_block = dir_entry_count > MAX_DIR_ENTRIES ? MAX_DIR_ENTRIES : dir_entry_count;

        for (i = 0; i < entries_in_this_block; i++) {
            if (strlen(dir_entries[i].filename) == dentry->d_name.len &&
                strncmp(dir_entries[i].filename, dentry->d_name.name, dentry->d_name.len) == 0) {
                struct inode *inode = osfs_iget(dir->i_sb, dir_entries[i].inode_no);
                if (IS_ERR(inode)) {
                    pr_err("osfs_lookup: Error getting inode %u\n", dir_entries[i].inode_no);
                    return ERR_CAST(inode);
                }
                return d_splice_alias(inode, dentry);
            }
        }

        // 移動到下一個block
        dir_entry_count -= entries_in_this_block;
        block_no = hdr->next_block;
        if (dir_entry_count <= 0)
            break;
    }

    return NULL;
}


static int osfs_iterate(struct file *filp, struct dir_context *ctx)
{
    struct inode *inode = file_inode(filp);
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    struct osfs_inode *osfs_inode = inode->i_private;
    uint32_t block_no = osfs_inode->i_head;
    int total_entries = osfs_inode->i_size / sizeof(struct osfs_dir_entry);
    int curr_entry = ctx->pos - 2;

    if (ctx->pos == 0) {
        if (!dir_emit_dots(filp, ctx))
            return 0;
    }

    while (block_no != OSFS_NO_NEXT_BLOCK && curr_entry < total_entries) {
        struct osfs_block_header *hdr = (struct osfs_block_header *)(sb_info->data_blocks + block_no * BLOCK_SIZE);
        struct osfs_dir_entry *dir_entries = (struct osfs_dir_entry *)hdr->data;

        int block_entries = total_entries > MAX_DIR_ENTRIES ? MAX_DIR_ENTRIES : total_entries;

        for (; curr_entry < block_entries; curr_entry++) {
            struct osfs_dir_entry *entry = &dir_entries[curr_entry];
            unsigned int type = DT_UNKNOWN;
            if (!dir_emit(ctx, entry->filename, strlen(entry->filename), entry->inode_no, type))
                return -EINVAL;
            ctx->pos++;
        }

        total_entries -= block_entries;
        curr_entry -= block_entries;
        block_no = hdr->next_block;
    }

    return 0;
}

static int osfs_add_dir_entry(struct inode *dir, uint32_t inode_no, const char *name, size_t name_len)
{
    struct osfs_sb_info *sb_info = dir->i_sb->s_fs_info;
    struct osfs_inode *parent_inode = dir->i_private;
    uint32_t block_no = parent_inode->i_head;
    struct osfs_block_header *hdr;
    int dir_entry_count = parent_inode->i_size / sizeof(struct osfs_dir_entry);
    int max_entries_per_block = MAX_DIR_ENTRIES;

    // 如果沒有任何block，先分配一個
    if (block_no == OSFS_NO_NEXT_BLOCK) {
        if (osfs_alloc_data_block(sb_info, &block_no) < 0)
            return -ENOSPC;
        parent_inode->i_head = block_no;
        parent_inode->i_tail = block_no;
        parent_inode->i_blocks = 1;
    }

    // 尋找可以放置的entry位置
    // 若當前tail block已滿，則配置新block並串接
    uint32_t cur_block = parent_inode->i_tail;
    hdr = (struct osfs_block_header *)(sb_info->data_blocks + cur_block * BLOCK_SIZE);
    int used_entries = (parent_inode->i_size / sizeof(struct osfs_dir_entry)) % max_entries_per_block;

    if (used_entries == 0 && parent_inode->i_size != 0) {
        // 需要新block
        uint32_t new_block;
        if (osfs_alloc_data_block(sb_info, &new_block) < 0)
            return -ENOSPC;
        hdr->next_block = new_block;
        parent_inode->i_tail = new_block;
        parent_inode->i_blocks++;
        hdr = (struct osfs_block_header *)(sb_info->data_blocks + new_block * BLOCK_SIZE);
        used_entries = 0;
    }

    // 檢查檔名是否已存在
    {
        // 需遍歷全部目錄
        uint32_t check_block = parent_inode->i_head;
        int remain_entries = parent_inode->i_size / sizeof(struct osfs_dir_entry);
        while (check_block != OSFS_NO_NEXT_BLOCK && remain_entries > 0) {
            struct osfs_block_header *chk_hdr = (struct osfs_block_header *)(sb_info->data_blocks + check_block * BLOCK_SIZE);
            struct osfs_dir_entry *entries = (struct osfs_dir_entry *)chk_hdr->data;
            int e_count = remain_entries > max_entries_per_block ? max_entries_per_block : remain_entries;
            for (int i=0; i<e_count; i++) {
                if (strlen(entries[i].filename) == name_len &&
                    strncmp(entries[i].filename, name, name_len) == 0) {
                    pr_warn("osfs_add_dir_entry: File '%.*s' already exists\n", (int)name_len, name);
                    return -EEXIST;
                }
            }
            remain_entries -= e_count;
            check_block = chk_hdr->next_block;
        }
    }

    // 新增目錄項目
    struct osfs_dir_entry *dir_entries = (struct osfs_dir_entry *)hdr->data;
    if (used_entries >= max_entries_per_block) {
        pr_err("osfs_add_dir_entry: Parent directory is full\n");
        return -ENOSPC;
    }

    strncpy(dir_entries[used_entries].filename, name, name_len);
    dir_entries[used_entries].filename[name_len] = '\0';
    dir_entries[used_entries].inode_no = inode_no;

    parent_inode->i_size += sizeof(struct osfs_dir_entry);

    return 0;
}

static int osfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct osfs_inode *parent_inode = dir->i_private;
    struct inode *inode;
    struct osfs_inode *osfs_inode;
    int ino, ret;

    if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode)) {
        pr_err("File type not supported\n");
        return -EINVAL;
    }

    struct osfs_sb_info *sb_info = dir->i_sb->s_fs_info;
    if (sb_info->nr_free_inodes == 0 || sb_info->nr_free_blocks == 0)
        return -ENOSPC;

    ino = osfs_get_free_inode(sb_info);
    if (ino < 0 || ino >= sb_info->inode_count)
        return -ENOSPC;

    inode = new_inode(dir->i_sb);
    if (!inode)
        return -ENOMEM;

    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    inode->i_ino = ino;
    inode->i_sb = dir->i_sb;
    inode->i_blocks = 0;
    simple_inode_init_ts(inode);

    if (S_ISDIR(mode)) {
        inode->i_op = &osfs_dir_inode_operations;
        inode->i_fop = &osfs_dir_operations;
        set_nlink(inode, 2);
        inode->i_size = 0;
    } else if (S_ISREG(mode)) {
        inode->i_op = &osfs_file_inode_operations;
        inode->i_fop = &osfs_file_operations;
        set_nlink(inode, 1);
        inode->i_size = 0;
    }

    osfs_inode = osfs_get_osfs_inode(dir->i_sb, ino);
    if (!osfs_inode) {
        iput(inode);
        return -EIO;
    }

    memset(osfs_inode, 0, sizeof(*osfs_inode));
    osfs_inode->i_ino = ino;
    osfs_inode->i_mode = inode->i_mode;
    osfs_inode->i_uid = i_uid_read(inode);
    osfs_inode->i_gid = i_gid_read(inode);
    osfs_inode->i_size = inode->i_size;
    osfs_inode->i_blocks = 0;
    osfs_inode->i_head = OSFS_NO_NEXT_BLOCK;
    osfs_inode->i_tail = OSFS_NO_NEXT_BLOCK;
    osfs_inode->__i_atime = osfs_inode->__i_mtime = osfs_inode->__i_ctime = current_time(inode);
    inode->i_private = osfs_inode;

    // 對於目錄，先分配一個block
    if (S_ISDIR(mode)) {
        uint32_t block_no;
        ret = osfs_alloc_data_block(sb_info, &block_no);
        if (ret) {
            iput(inode);
            return ret;
        }
        osfs_inode->i_head = block_no;
        osfs_inode->i_tail = block_no;
        osfs_inode->i_blocks = 1;
    }

    // 新增該檔案至父目錄
    ret = osfs_add_dir_entry(dir, ino, dentry->d_name.name, dentry->d_name.len);
    if (ret) {
        iput(inode);
        return ret;
    }

    // 更新父目錄時間戳
    parent_inode->__i_mtime = parent_inode->__i_ctime = current_time(dir);
    mark_inode_dirty(dir);

    d_instantiate(dentry, inode);
    dget(dentry);

    pr_info("osfs_create: File '%.*s' created with inode %lu\n",
            (int)dentry->d_name.len, dentry->d_name.name, inode->i_ino);

    return 0;
}


const struct inode_operations osfs_dir_inode_operations = {
    .lookup = osfs_lookup,
    .create = osfs_create,
};

const struct file_operations osfs_dir_operations = {
    .iterate_shared = osfs_iterate,
    .llseek = generic_file_llseek,
};
