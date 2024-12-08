#include "../include/newfs.h"

extern struct newfs_super newfs_super;
extern struct custom_options newfs_options;	

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 计算路径的层级
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    /* 读取逻辑块 */
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;

    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);

    while (size_aligned != 0)
    {
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    /* 读取逻辑块 */
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 将dentry插入inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;

    // 分配数据块
    int cur_blk = inode->dir_cnt / MAX_DENTRY_PER_BLK();
    if (inode->dir_cnt % MAX_DENTRY_PER_BLK() == 1) {
        /* 当前数据块已满，需要寻找新的数据块*/
        int byte_cursor = 0; 
        int bit_cursor  = 0; 
        int dno_cursor  = 0;
        boolean is_find_free_data_blk = FALSE;
        /* 检查位图是否有空位 */
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); 
            byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
                if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                        /* 当前dno_cursor位置空闲 */
                    newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                    inode->block_pointer[cur_blk] = dno_cursor;
                    is_find_free_data_blk = TRUE;           
                    break;
                }
                dno_cursor++;
            }
            if (is_find_free_data_blk) {
                break;
            }
        }

        if (!is_find_free_data_blk || dno_cursor == newfs_super.max_data)
            return -NEWFS_ERROR_NOSPACE;
    }

    return inode->dir_cnt;
}

struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    // dentry 指向 inode
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    // inode 指向 dentry                                                                                                
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    
    // inode指向文件类型，则分配数据指针
    if (NEWFS_IS_REG(inode)) {
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLK_SZ());
        }
    }
    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  cur_dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino             = inode->ino;
    int offset;

    // 将内存中的 inode 刷回 磁盘的 inode_d
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
        inode_d.block_pointer[i] = inode->block_pointer[i];
    }

    /* 先写inode本身 */
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
        NEWFS_INODE_SZ()) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }

    /* 再写inode下方的数据 */
    if (NEWFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;

        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            offset = NEWFS_DATA_OFS(inode->block_pointer[i]);
            while ((dentry_cursor != NULL) && (offset < NEWFS_DATA_OFS(inode->block_pointer[i] + 1))) {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
                
                // 递归调用 将目录项的inode写回
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                // 下一个目录项
                cur_dentry_cursor = dentry_cursor;
                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);

                free(cur_dentry_cursor);
            }
        }
    }
    else if (NEWFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            if(!inode->block_pointer[i]) continue;
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->block_pointer[i]), inode->data[i], 
                NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
            free(inode->data[i]);
        }
    }
    free(inode);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int dir_cnt = 0;
    int offset;
    
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        NEWFS_INODE_SZ()) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;

    for(int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
        inode->block_pointer[i] = inode_d.block_pointer[i];
    }

    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        // 节点指向的数据块
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            offset = NEWFS_DATA_OFS(inode->block_pointer[i]);
            // 磁盘无指针，按照 dentry 大小遍历
            while ((dir_cnt > 0) && (offset + sizeof(struct newfs_dentry_d) < NEWFS_DATA_OFS(inode->block_pointer[i] + 1))) {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, 
                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return NULL;
                }
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                newfs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct newfs_dentry_d);
                dir_cnt --;
            }
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            inode->data[i] = (uint8_t *)malloc(NEWFS_BLK_SZ());
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->block_pointer[i]), (uint8_t *)inode->data[i], 
                NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}

/**
 * @brief 获得第 dir 个 dentry
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }

    // 最外层文件夹名称
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        // 当前 dentry 对应的 inode
        inode = dentry_cursor->inode;

        // 若未到文件层级出现文件类型
        if (NEWFS_IS_REG(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }

        // 若为文件夹类型
        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            // 未找到该文件 or 文件夹 
            // mkdir mknod
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            // 找到正确的文件
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    // 从磁盘中读出 inode
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

/**
 * @brief 初始化超级块
 */ 
static void init_newfs_super_d(struct newfs_super_d* newfs_super_d) {
    newfs_super_d->sz_usage = 0;
    newfs_super_d->super_blks = SUPER_BLKS;
    newfs_super_d->super_blk_offset = NEWFS_SUPER_OFS;
    
    // 计算 inode 位图
    newfs_super_d->map_inode_blks = INODE_MAP_BLKS;
    newfs_super_d->map_inode_offset = newfs_super_d->super_blk_offset + 
                                     NEWFS_BLKS_SZ(newfs_super_d->super_blks);
    
    // 计算数据位图
    newfs_super_d->map_data_blks = DATA_MAP_BLKS;
    newfs_super_d->map_data_offset = newfs_super_d->map_inode_offset + 
                                    NEWFS_BLKS_SZ(newfs_super_d->map_inode_blks);
    
    // 计算 inode 区域
    newfs_super_d->ino_blks = INODE_BLKS;
    newfs_super_d->ino_offset = newfs_super_d->map_data_offset + 
                               NEWFS_BLKS_SZ(newfs_super_d->map_data_blks);
    
    // 计算数据区域
    newfs_super_d->data_blks = DATA_BLKS;
    newfs_super_d->data_offset = newfs_super_d->ino_offset + 
                                NEWFS_BLKS_SZ(newfs_super_d->ino_blks);
    
    // 计算最大值
    newfs_super_d->max_ino = MAX_INODE_PER_BLK * newfs_super_d->ino_blks;
    newfs_super_d->max_data = newfs_super_d->data_blks;
}

/**
 * @brief 从磁盘超级块同步到内存超级块
 */
static void sync_super_to_memory(struct newfs_super_d* newfs_super_d) {
    newfs_super.sz_usage = newfs_super_d->sz_usage;
    newfs_super.super_blks = newfs_super_d->super_blks;
    newfs_super.super_blk_offset = newfs_super_d->super_blk_offset;
    newfs_super.map_inode_blks = newfs_super_d->map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d->map_inode_offset;
    newfs_super.map_data_blks = newfs_super_d->map_data_blks;
    newfs_super.map_data_offset = newfs_super_d->map_data_offset;
    newfs_super.ino_blks = newfs_super_d->ino_blks;
    newfs_super.ino_offset = newfs_super_d->ino_offset;
    newfs_super.data_blks = newfs_super_d->data_blks;
    newfs_super.data_offset = newfs_super_d->data_offset;
    newfs_super.max_ino = newfs_super_d->max_ino;
    newfs_super.max_data = newfs_super_d->max_data;
}

/**
 * @brief 从内存超级块同步到磁盘超级块
 */
static void sync_super_to_disk(struct newfs_super_d* newfs_super_d) {
    newfs_super_d->magic_num = NEWFS_MAGIC_NUM;
    newfs_super_d->sz_usage = newfs_super.sz_usage;
    newfs_super_d->super_blks = newfs_super.super_blks;
    newfs_super_d->super_blk_offset = newfs_super.super_blk_offset;
    newfs_super_d->map_inode_blks = newfs_super.map_inode_blks;
    newfs_super_d->map_inode_offset = newfs_super.map_inode_offset;
    newfs_super_d->map_data_blks = newfs_super.map_data_blks;
    newfs_super_d->map_data_offset = newfs_super.map_data_offset;
    newfs_super_d->ino_blks = newfs_super.ino_blks;
    newfs_super_d->ino_offset = newfs_super.ino_offset;
    newfs_super_d->data_blks = newfs_super.data_blks;
    newfs_super_d->data_offset = newfs_super.data_offset;
    newfs_super_d->max_ino = newfs_super.max_ino;
    newfs_super_d->max_data = newfs_super.max_data;
}

/**
 * @brief 写回位图
 */
static int sync_maps_to_disk(struct newfs_super_d* newfs_super_d) {
    // 写回索引节点位图
    if (newfs_driver_write(newfs_super_d->map_inode_offset, 
                          (uint8_t *)(newfs_super.map_inode), 
                          NEWFS_BLKS_SZ(newfs_super_d->map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    // 写回数据块位图
    if (newfs_driver_write(newfs_super_d->map_data_offset, 
                          (uint8_t *)(newfs_super.map_data), 
                          NEWFS_BLKS_SZ(newfs_super_d->map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 卸载文件系统
 */
int newfs_umount() {
    struct newfs_super_d newfs_super_d; 

    // 1. 检查文件系统是否已挂载
    if (!newfs_super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    // 2. 从根节点开始,递归地将所有inode写回磁盘
    newfs_sync_inode(newfs_super.root_dentry->inode);

    // 3. 将内存中的超级块信息同步到磁盘超级块结构
    sync_super_to_disk(&newfs_super_d);

    // 4. 将超级块写回磁盘
    if (newfs_driver_write(NEWFS_SUPER_OFS, 
                          (uint8_t *)&newfs_super_d, 
                          sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    // 5. 将位图写回磁盘
    int ret = sync_maps_to_disk(&newfs_super_d);
    if (ret != NEWFS_ERROR_NONE) {
        return ret;
    }

    // 6. 清理资源
    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}

int newfs_mount(struct custom_options options) {
    int ret = NEWFS_ERROR_NONE;
    struct newfs_super_d newfs_super_d;
    struct newfs_dentry* root_dentry;
    struct newfs_inode* root_inode;
    boolean is_init = FALSE;
    
    // 1. 初始化基本信息
    newfs_super.is_mounted = FALSE;
    int fd = ddriver_open(options.device);
    if (fd < 0) {
        return fd;
    }
    newfs_super.fd = fd;
    
    // 2. 获取设备信息
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    newfs_super.sz_blks = NEWFS_IO_SZ() * 2;

    // 3. 创建根目录项
    root_dentry = new_dentry("/", NEWFS_DIR);
    
    // 4. 读取并检查超级块
    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t*)(&newfs_super_d), 
        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM) {
        init_newfs_super_d(&newfs_super_d);
        is_init = TRUE;
    }
    
    // 5. 同步超级块到内存
    sync_super_to_memory(&newfs_super_d);
    
    // 6. 分配并读取位图
    newfs_super.map_inode = (uint8_t*)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_data = (uint8_t*)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    
    if (newfs_driver_read(newfs_super_d.map_inode_offset, newfs_super.map_inode,
        NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    
    if (newfs_driver_read(newfs_super_d.map_data_offset, newfs_super.map_data,
        NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    
    // 7. 处理根目录
    if (is_init) {
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }
    
    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted = TRUE;
    
    return ret;
}

/**
 * @brief 为inode分配一个数据块
 * 
 * @param inode 需要分配数据块的inode
 * @param blk_no 分配的数据块编号
 * @return int 成功返回NEWFS_ERROR_NONE，失败返回错误码
 */
int newfs_alloc_data_blk(struct newfs_inode* inode, int blk_no) {
    // 检查参数
    if (blk_no >= NEWFS_DATA_PER_FILE) {
        return -NEWFS_ERROR_NOSPACE;
    }

    // 在数据块位图中寻找空闲块
    int byte_cursor = 0; 
    int bit_cursor = 0; 
    int data_blk_cursor = 0;
    boolean is_find_free_blk = FALSE;

    // 遍历数据块位图
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); byte_cursor++) {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if ((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {
                // 找到空闲数据块
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_blk = TRUE;
                break;
            }
            data_blk_cursor++;
        }
        if (is_find_free_blk) {
            break;
        }
    }

    // 检查是否找到空闲块
    if (!is_find_free_blk || data_blk_cursor >= newfs_super.max_data) {
        return -NEWFS_ERROR_NOSPACE;
    }

    // 分配数据块
    inode->block_pointer[blk_no] = data_blk_cursor;
    
    // 为数据块分配内存
    if (inode->data[blk_no] == NULL) {
        inode->data[blk_no] = (uint8_t *)malloc(NEWFS_BLK_SZ());
        if (inode->data[blk_no] == NULL) {
            // 分配失败，回退位图标记
            newfs_super.map_data[byte_cursor] &= ~(0x1 << bit_cursor);
            return -NEWFS_ERROR_NOSPACE;
        }
    }

    return NEWFS_ERROR_NONE;
}

int newfs_drop_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == dentry) {
        inode->dentrys = dentry->brother;
    }
    else {
        struct newfs_dentry* dentry_cursor = inode->dentrys;
        while (dentry_cursor) {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    inode->dir_cnt--;
    free(dentry);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 删除内存中的一个inode及其对应的dentry和data
 * 
 * @param inode 要删除的inode
 * @return int 成功返回NEWFS_ERROR_NONE，失败返回错误码
 */
int newfs_drop_inode(struct newfs_inode* inode) {
    struct newfs_dentry* dentry_cursor;
    struct newfs_dentry* dentry_to_free;
    int byte_cursor = 0;
    int bit_cursor = 0;
    int ino_cursor = 0;
    boolean is_find = FALSE;

    if (inode == NULL) {
        return NEWFS_ERROR_NONE;
    }

    if (NEWFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
        /* 递归删除目录下的所有目录项 */
        while (dentry_cursor) {
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            newfs_drop_inode(dentry_to_free->inode);
            free(dentry_to_free);
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        /* 释放文件对应的数据块 */
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
            if (inode->block_pointer[i]) {
                /* 清除数据块位图 */
                int blk_byte = inode->block_pointer[i] / UINT8_BITS;
                int blk_bit = inode->block_pointer[i] % UINT8_BITS;
                newfs_super.map_data[blk_byte] &= ~(0x1 << blk_bit);
                
                /* 释放数据块内存 */
                if (inode->data[i]) {
                    free(inode->data[i]);
                    inode->data[i] = NULL;
                }
            }
        }
    }

    /* 清除inode位图中对应的位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); byte_cursor++) {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if (ino_cursor == inode->ino) {
                newfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                is_find = TRUE;
                break;
            }
            ino_cursor++;
        }
        if (is_find) {
            break;
        }
    }

    /* 释放inode内存 */
    free(inode);

    return NEWFS_ERROR_NONE;
}

