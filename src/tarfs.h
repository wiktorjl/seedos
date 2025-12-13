#ifndef TARFS_H
#define TARFS_H

#include "types.h"
#include "vfs.h"

struct vnode *tarfs_open(const char *path);

#endif /* TARFS_H */