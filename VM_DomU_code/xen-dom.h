/*
* xen-dom.h
* domU header
*/

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/rbtree.h>
#include <asm/setup.h>
#include <asm/pgalloc.h>
#include <asm/hypervisor.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/io/protocols.h>
#include "pdma-ioctl.h"

#define CHRIF_OP_WRITE 0
#define CHRIF_OP_IOCTL 1
#define CHRIF_OP_MAP 2
#define CHRIF_OP_READ 3
#define MAX_GREF 16
#define OFFSET_LENGTH sizeof(unsigned int)

/*two .c file use it, so declare it here*/
#define DEVICE_PATH "/dev/pdma"


struct  chrif_request {
    unsigned int id;
	grant_ref_t op_gref[MAX_GREF];
    unsigned int status;
    struct pdma_stat stat;
	unsigned int operation;
	struct file *chrif_filp;
	unsigned int length;
};

struct chrif_response {
    unsigned int id;
    unsigned int status;
	struct pdma_stat stat;
    unsigned int operation;
    struct file *chrif_filp;
	unsigned int length;
};

typedef struct chrif_request chrif_request_t;
typedef struct chrif_response chrif_response_t;

DEFINE_RING_TYPES(chrif, struct chrif_request, struct chrif_response);
typedef struct chrif_sring chrif_sring_t;
typedef struct chrif_front_ring chrif_front_ring_t;
typedef struct chrif_back_ring chrif_back_ring_t;

static const char *op_name(int op)
{
    static const char *const names[] = {
        [CHRIF_OP_WRITE] = "write",
		[CHRIF_OP_IOCTL] = "ioctl",
		[CHRIF_OP_MAP] = "mapages",
		[CHRIF_OP_READ] = "read"};

        if (op < 0 || op >= ARRAY_SIZE(names))
            return "unknown";
        if (!names[op])
            return "reserved";
        return names[op];
}


