# Root File System
## Exercise7 Slide
[OSC2026_lab7_exercise](https://docs.google.com/presentation/d/1lBmtptTsUn0ePogB_aNFTJAf1sfB-aJxriyNbe7ORTM/edit?slide=id.p1#slide=id.p1)
## Introduction
In Lab 7, you will implement the core of a **Virtual File System** — the layer inside every OS kernel that lets you call open(), read(), write() without caring which filesystem is underneath (ext4, FAT32, tmpfs, etc.).   
In this exercise, we focus on **tmpfs**.
## Workflow
Take `read` as an example: 
```
┌─────────────────┐    ┌─────────────────┐        ┌─────────────────┐
│     Process     │    |       VFS       |        |      tmpfs      |
|                 |    |                 |        |                 |
|     my_read ────┼────┼──► vfs_read ────┼────────┼──► tmpfs_read   |
|                 |    |                 |        |                 |
└─────────────────┘    └─────────────────┘        └─────────────────┘
                 vfs_read             file->f_ops->read
```
## Virtual Node
- A node in a VFS tree
- `v_ops` & `f_ops`: Function pointers mapping to the specific operations supported by the underlying file system.
- `internal`: (void*) designed to decouple the VFS layer from the concrete file system implementation. It acts as a hook to attach the underlying, filesystem-specific data structure (e.g., tmpfs_vnode), regardless of the storage medium.
## tmpfs_vnode
- The concrete, internal data structure representing an inode exclusively for the tmpfs.
- `name`: The component name of the file or directory.
- `type`: directory (FS_DIR) or a regular file (FS_FILE).
- `entry`: An array of struct vnode* pointers. If the node is a directory, this array acts as the directory entries, holding references to its child vnode objects.
- `data` & `size`: memory address and size
## File Handle
- `vnode`: Points to the file entity that is currently being operated on in this state.
- `f_pos`: The position of the read/write cursor. 
## File Descriptor Table (FDT)
- A per-process data structure utilized to bookkeep currently opened file handles.
## TODOs
### tmpfs_create_vnode
```c
struct vnode* tmpfs_create_vnode(enum fsnode_type type)
```
Allocate and fully initialize one vnode.  
``` 
┌──────────────────┐         ┌──────────────────────┐   
│   struct vnode   │         │  struct tmpfs_vnode  │   
│                  │         │                      │    
│  mount = NULL    │         │  type = FS_DIR/FILE  │   
│  internal ───────┼────────►│  name = ""           │   
│                  │         │  entry[] = {0}       │   
│                  │         │  data = NULL         │   
│                  │         │  size = 0            │   
|                  |         └──────────────────────┘   
│  v_ops ──────────┼─────────►  tmpfs_vnode_ops           
│  f_ops ──────────┼─────────►  tmpfs_file_ops             
└──────────────────┘ 
```

### tmpfs_create
```c
int tmpfs_create(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name)
```
1. Create a new vnode (FS_FILE) using tmpfs_create_vnode
2. Name it (copy component_name into inode->name)
3. Register it in the parent directory's entry[] array
4. Give the caller a pointer to the new vnode

### tmpfs_write
```c
int tmpfs_write(struct file* file, const void* buf, size_t len)
```
1. Get the tmpfs_vnode
2. Allocate data buffer if it doesn't exist yet (first write)
3. Copy buf into data at the current f_pos cursor
4. Advance f_pos by len
5. Return how many bytes were written

### tmpfs_read
```c
int tmpfs_read(struct file* file, void* buf, size_t len)
```
1. Get the tmpfs_vnode
2. Calculate how many bytes are actually readable
3. Copy from data + f_pos into buf
4. Advance f_pos by len
5. Return bytes read

## Expected Result
```
Test passed. Nice work!
```