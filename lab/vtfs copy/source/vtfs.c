#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#define MODULE_NAME "vtfs"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-dev");
MODULE_DESCRIPTION("A simple FS kernel module");

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

// Прототип функции vtfs_lookup
struct dentry* vtfs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag);
int vtfs_mkdir(struct inode* parent_inode, struct dentry* child_dentry, umode_t mode);
int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry);

struct inode* vtfs_get_inode(
  struct super_block* sb,
  const struct inode* dir,
  umode_t mode,
  int i_ino
);
struct inode_operations vtfs_inode_ops;

int next_ino = 100;

struct vtfs_inode {
  int ino;
  umode_t mode;
  size_t i_size;
  char i_data[1024];
};

struct vtfs_dentry {
  struct dentry* d_dentry;
  struct vtfs_inode* d_inode;
  char d_name[255];
  ino_t d_parent_ino;
  struct list_head list;
};

static struct {
  struct super_block* sb;       
  struct list_head dentries; 
} vtfs_sb;

// struct vtfs_dentry* vtfs_find_dentry(ino_t parent_inode_number, const char* name) {
//     struct vtfs_dentry* current_vtfs_dentry;
//     struct list_head* current_list_entry;

//     // Перебираем список vtfs_sb.dentries
//     list_for_each(current_list_entry, &vtfs_sb.dentries) {
//         current_vtfs_dentry = list_entry(current_list_entry, struct vtfs_dentry, list);

//         // Сравниваем номер родительского inode и имя
//         if (current_vtfs_dentry->d_parent_ino == parent_inode_number &&
//             (!name || strcmp(current_vtfs_dentry->d_name, name) == 0)) {
//             return current_vtfs_dentry; 
//         }
//     }
//     return NULL; 
// }


struct dentry* vtfs_lookup(
    struct inode* parent_inode, 
    struct dentry* child_dentry, 
    unsigned int flag
) {
    struct vtfs_dentry* dentry;
    struct list_head* pos;
    struct inode* inode;

    list_for_each(pos, &vtfs_sb.dentries) {
      dentry = list_entry(pos, struct vtfs_dentry, list);

      if (dentry->d_parent_ino == parent_inode->i_ino &&
          strcmp(dentry->d_name, child_dentry->d_name.name) == 0) {
        inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, dentry->d_inode->mode, dentry->d_inode->ino);
        d_add(child_dentry, inode);
        return NULL;
      }
    }
    return NULL;  
};


int vtfs_iterate(struct file* file, struct dir_context* ctx) {
  struct inode* parent_inode = file->f_path.dentry->d_inode;
  struct vtfs_dentry* current_vtfs_dentry;
  struct list_head* pos;
  struct inode* dir_inode = file->f_path.dentry->d_inode;
  unsigned char file_type;

  if (!dir_emit_dots(file, ctx)) {
      return 0;
  }
  if (ctx->pos >= 3) {
    return ctx->pos;
  }
  // int entry_found = 0;

    // Сравниваем номер родительского inode
  list_for_each(pos, &vtfs_sb.dentries) {
    current_vtfs_dentry = list_entry(pos, struct vtfs_dentry, list);
    
      // Определяем тип файла
    if (S_ISDIR(current_vtfs_dentry->d_inode->mode)) {
        file_type = DT_DIR;
    } else if (S_ISREG(current_vtfs_dentry->d_inode->mode)) {
        file_type = DT_REG;
    } else {
        file_type = DT_UNKNOWN;
    }

        // Передаём информацию в dir_emit
    if (current_vtfs_dentry->d_parent_ino == dir_inode->i_ino &&
        !dir_emit(ctx, current_vtfs_dentry->d_name, strlen(current_vtfs_dentry->d_name), current_vtfs_dentry->d_inode->ino, file_type)) {
      return -ENOMEM;
    }
    ctx->pos += 1;
  }

  return ctx->pos;
}



int vtfs_create(
    struct inode* parent_inode, 
    struct dentry* child_dentry, 
    umode_t mode, 
    bool b
) {
    struct inode* new_inode;
    struct vtfs_dentry* new_vtfs_dentry;
    struct vtfs_inode* new_vtfs_inode;
    ino_t parent_inode_number = parent_inode->i_ino;

    new_inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, mode, next_ino++);
    if (!new_inode) {
        return -ENOMEM; 
    }

    // Выделяем память для новой записи dentry
    new_vtfs_dentry = kmalloc(sizeof(struct vtfs_dentry), GFP_KERNEL);
    if (!new_vtfs_dentry) {
        iput(new_inode); 
        return -ENOMEM;
    }

    // Выделяем память для структуры нового inode
    new_vtfs_inode = kmalloc(sizeof(struct vtfs_inode), GFP_KERNEL);
    if (!new_vtfs_inode) {
        kfree(new_vtfs_dentry);  
        iput(new_inode);         // Освобождаем inode
        return -ENOMEM;
    }
    // Инициализация inode
    new_vtfs_inode->ino = new_inode->i_ino;
    new_vtfs_inode->mode = mode;
    new_vtfs_inode->i_size = 0; 

    // Инициализация dentry
    new_vtfs_dentry->d_dentry = child_dentry;
    strcpy(new_vtfs_dentry->d_name, child_dentry->d_name.name);
    new_vtfs_dentry->d_parent_ino = parent_inode_number;
    new_vtfs_dentry->d_inode = new_vtfs_inode;

    list_add(&new_vtfs_dentry->list, &vtfs_sb.dentries);

    // Привязываем новый inode к dentry
    d_add(child_dentry, new_inode);

    printk(KERN_INFO "Created new file or directory: %s\n", child_dentry->d_name.name);

    return 0;
}


int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry) {
    struct vtfs_dentry* found_dentry = NULL;
    // const char* target_name = child_dentry->d_name.name;
    struct list_head* pos;

    list_for_each(pos, &vtfs_sb.dentries) {
    found_dentry = list_entry(pos, struct vtfs_dentry, list);

    if (strcmp(found_dentry->d_name, child_dentry->d_name.name) == 0 &&
        found_dentry->d_parent_ino == parent_inode->i_ino) {
      list_del(&found_dentry->list);

      kfree(found_dentry);
      return 0;
    }
  }

  return -ENOENT;
}


int vtfs_mkdir(struct inode* parent_inode, struct dentry* child_dentry, umode_t mode) {
  struct inode* inode;
  struct vtfs_dentry* new_dentry;

  // Создаём новый inode для директории
  inode = vtfs_get_inode(vtfs_sb.sb, parent_inode, mode | S_IFDIR, next_ino++);
  if (!inode) {
      printk(KERN_ERR "Failed to allocate inode for directory %s\n", child_dentry->d_name.name);
      return -ENOMEM;
  }

  // Выделяем память для нового dentry
  new_dentry = kmalloc(sizeof(struct vtfs_dentry), GFP_KERNEL);
  if (!new_dentry) {
      printk(KERN_ERR "Failed to allocate memory for vtfs_dentry\n");
      iput(inode);  // Освобождаем inode, если dentry не удалось создать
      return -ENOMEM;
  }

  // Выделяем память для информации об inode в dentry
  new_dentry->d_inode = kmalloc(sizeof(struct vtfs_inode), GFP_KERNEL);
  if (!new_dentry->d_inode) {
      printk(KERN_ERR "Failed to allocate memory for vtfs_inode\n");
      kfree(new_dentry);  // Освобождаем dentry
      iput(inode);        // Освобождаем inode
      return -ENOMEM;
  }

  // Инициализируем dentry
  new_dentry->d_dentry = child_dentry;
  strcpy(new_dentry->d_name, child_dentry->d_name.name);
  new_dentry->d_parent_ino = parent_inode->i_ino;
  new_dentry->d_inode->ino = inode->i_ino;
  new_dentry->d_inode->mode = inode->i_mode;
  new_dentry->d_inode->i_size = 0;

  // Добавляем dentry в список dentries файловой системы
  list_add(&new_dentry->list, &vtfs_sb.dentries);

  // Добавляем новый inode в dentry ядра
  d_add(child_dentry, inode);

  printk(KERN_INFO "Directory %s created successfully (inode: %ld)\n", child_dentry->d_name.name, inode->i_ino);

  return 0;
}

int vtfs_rmdir(struct inode* parent_inode, struct dentry* child_dentry) {
    struct vtfs_dentry* target_dentry = NULL;
    struct list_head* list_pos;

    // Проходим по всем dentries, чтобы найти соответствующую директорию
    list_for_each(list_pos, &vtfs_sb.dentries) {
        target_dentry = list_entry(list_pos, struct vtfs_dentry, list);
        // Если нашли нужную директорию по имени и родительскому inode
        if (strcmp(target_dentry->d_name, child_dentry->d_name.name) == 0 &&
            target_dentry->d_parent_ino == parent_inode->i_ino) {

            list_del(&target_dentry->list);
            kfree(target_dentry);
            printk(KERN_INFO "Directory %s removed successfully\n", child_dentry->d_name.name);
            return 0;
        }
    }
    printk(KERN_ERR "Directory %s not found in parent directory\n", child_dentry->d_name.name);
    return -ENOENT;  
}

struct inode_operations vtfs_inode_ops = {
  .lookup = vtfs_lookup,
  .mkdir = vtfs_mkdir,
  .rmdir = vtfs_rmdir,
  .create = vtfs_create,
  .unlink = vtfs_unlink,
};

struct vtfs_read_context {
    struct vtfs_inode *inode_data;
    struct vtfs_dentry *dentry_entry;
    struct inode *file_inode;
    struct list_head *current_pos;
};

ssize_t vtfs_read(struct file *file, char *buffer, size_t len, loff_t *offset) {
    struct vtfs_read_context read_ctx;
    ssize_t bytes_to_read;

    read_ctx.file_inode = file->f_inode;
    // Ищем файл в списке dentries
    list_for_each(read_ctx.current_pos, &vtfs_sb.dentries) {
        read_ctx.dentry_entry = list_entry(read_ctx.current_pos, struct vtfs_dentry, list);
        read_ctx.inode_data = read_ctx.dentry_entry->d_inode;

        // Если находим совпадение inode
        if (read_ctx.inode_data->ino == read_ctx.file_inode->i_ino) {
            if (*offset >= read_ctx.inode_data->i_size)
                return 0;

            // Определяем, сколько данных можно прочитать
            bytes_to_read = min(len, read_ctx.inode_data->i_size - *offset);

            // Копируем данные в пользовательскую память
            if (copy_to_user(buffer, read_ctx.inode_data->i_data + *offset, bytes_to_read))
                return -EFAULT;

            *offset += bytes_to_read;
            return bytes_to_read;
        }
    }
    return -ENOENT;
}

struct vtfs_write_context {
    struct vtfs_inode *inode_data;
    struct vtfs_dentry *dentry_entry;
    struct inode *file_inode;
    struct list_head *current_pos;
    void *new_data;
    ssize_t new_size;
};

ssize_t vtfs_write(struct file* file, const char* buffer, size_t len, loff_t* offset) {
    struct vtfs_write_context write_ctx;
    ssize_t bytes_written;

    // Инициализация контекста с файлом
    write_ctx.file_inode = file->f_inode;

    // Поиск файла в списке dentries
    list_for_each(write_ctx.current_pos, &vtfs_sb.dentries) {
        write_ctx.dentry_entry = list_entry(write_ctx.current_pos, struct vtfs_dentry, list);
        write_ctx.inode_data = write_ctx.dentry_entry->d_inode;

        // Проверка, если inode файла совпадает с данным
        if (write_ctx.inode_data->ino == write_ctx.file_inode->i_ino) {
            write_ctx.new_size = max(write_ctx.inode_data->i_size, *offset + len);

            // Копирование данных в inode
            if (copy_from_user(write_ctx.inode_data->i_data + *offset, buffer, len)) {
                return -EFAULT;
            }

            write_ctx.inode_data->i_size = write_ctx.new_size;
            *offset += len;

            // Возвращаем количество записанных байт
            bytes_written = len;
            return bytes_written;
        }
    }
    return -ENOENT;
}

struct file_operations vtfs_dir_ops = {
  .iterate = vtfs_iterate,
  .read = vtfs_read,
  .write = vtfs_write,
};


void vtfs_kill_sb(struct super_block* sb) {
  printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}

struct inode* vtfs_get_inode(
  struct super_block* sb,
  const struct inode* dir,
  umode_t mode,
  int i_ino
) {
  struct inode *inode = new_inode(sb);
  if (inode != NULL) {
    inode_init_owner(inode, dir, mode);
  }
  inode->i_ino = i_ino;
  inode->i_op = &vtfs_inode_ops;
  inode->i_mode = mode; 
  inode->i_fop = &vtfs_dir_ops; 
  inc_nlink(inode);
  return inode;
}

int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
  struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR | 0777, next_ino++);
  if (!inode) {
    printk(KERN_ERR "Failed to create a root inode");
    return -ENOMEM;
  }

  sb->s_root = d_make_root(inode);
  if (sb->s_root == NULL) {
    printk(KERN_ERR "Failed to create a root dentry");
    return -ENOMEM;
  }
  vtfs_sb.sb = sb;
  INIT_LIST_HEAD(&vtfs_sb.dentries);
  printk(KERN_INFO "return 0\n");

  return 0;
}

struct dentry* vtfs_mount(
  struct file_system_type* fs_type,
  int flags,
  const char* token,
  void* data
  ) {
  struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
  if (ret == NULL) {
    printk(KERN_ERR "Can't mount file system");
  } else {
    printk(KERN_INFO "Mounted successfuly");
  }
  return ret;
}



struct file_system_type vtfs_fs_type = {
  .name = "vtfs",
  .mount = vtfs_mount,
  .kill_sb = vtfs_kill_sb,
};

static int __init vtfs_init(void) {
  int ret = register_filesystem(&vtfs_fs_type);
  if (ret != 0) {
    LOG("Failed to register filesystem\n");
    return ret;
  }
  LOG("VTFS joined the kernel\n");
  return 0;
}

static void __exit vtfs_exit(void) {
  int ret = unregister_filesystem(&vtfs_fs_type);
  if (ret != 0) {
    LOG("Failed to unregister filesystem\n");
  }
  LOG("VTFS left the kernel\n");
}

module_init(vtfs_init);
module_exit(vtfs_exit);
