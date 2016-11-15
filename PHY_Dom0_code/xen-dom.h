/*
* xen-dom.h
* dom0 header
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

#define CHRIF_OP_OPEN 0
#define CHRIF_OP_READ 1
#define CHRIF_OP_WRITE 2
#define CHRIF_OP_IOCTL 3
#define CHRIF_OP_CLOSE 4
#define CHRIF_OP_MAP 5
#define MAX_GREF 10

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
	int nr_pages;
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

struct xen_chrif{
    domid_t domid;
    unsigned int handle;
	unsigned int pages_handle[MAX_GREF];
    unsigned int irq;

	/* shared ring map handle */
    grant_handle_t shmem_handle;

	/* shared ring map ref */
    grant_ref_t shmem_ref;

    struct chrif_back_ring chr_ring;
	spinlock_t chr_ring_lock;
    struct vm_struct *comms_area;
	unsigned long *map_pages;

    struct backend_info *be;  
    wait_queue_head_t wq;
    struct file *filp;
    wait_queue_head_t waiting_to_free;

	/* One thread per one chrif. */
	struct task_struct	*xenchrd;
	unsigned int		waiting_reqs;

	/*write buffer*/
	char *block_buf;
    /*number of mapped pages*/
    int nr_pages;
    /*the block info about pdma*/
    struct pdma_info pdma_info;
};

static const char *op_name(int op)
{
    static const char *const names[] = {
        [CHRIF_OP_OPEN] = "open",
        [CHRIF_OP_READ] = "read",
        [CHRIF_OP_WRITE] = "write",
		[CHRIF_OP_IOCTL] = "ioctl",
		[CHRIF_OP_MAP] = "mapages",
		[CHRIF_OP_CLOSE] = "close" };

        if (op < 0 || op >= ARRAY_SIZE(names))
            return "unknown";
        if (!names[op])
            return "reserved";
        return names[op];
}

int xen_chrif_schedule(void *arg);

int xen_chrif_xenbus_init(void);

irqreturn_t chrif_int(int irq, void *dev_id);

void xen_chrbk_unmap(struct xen_chrif *chrif);

void xen_chrif_xenbus_exit(void);
