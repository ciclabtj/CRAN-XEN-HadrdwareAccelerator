#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <linux/fs.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/xenbus.h>
#include <xen/evtchn.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/platform_pci.h>

#include <xen/interface/xen.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/protocols.h>

#include <asm/pgtable.h>
#include <asm/sync_bitops.h>
#include <asm/xen/page.h>
#include <asm/uaccess.h>
#include <asm/device.h>
#include <asm/xen/hypervisor.h>

#include "pdma-ioctl.h"
#include "xen-dom.h"

enum chrif_state {
    CHRIF_STATE_DISCONNECTED,
    CHRIF_STATE_CONNECTED,
    CHRIF_STATE_SUSPENDED,
};


#define GRANT_INVALID_REF   0
#define DOM0_ID 0
#define DEV_NAME "fpdma"   /* name in /dev */
static struct class *domt_class;
dev_t dev_no = 0;   
int reqid = 0;
int read_len = 0;

struct completion comp_ioctl;
struct completion comp_write;


//time count
struct timespec timestar;
struct timespec timend;

// #define CHR_RING_SIZE __CONST_RING_SIZE(chrif, PAGE_SIZE)

struct chrfront_info {
    struct chrif_front_ring ring;
    grant_ref_t ring_ref;
    struct xenbus_device *xbdev;
    enum chrif_state connected;
	grant_ref_t op_gref[MAX_GREF];
	unsigned long *op_pages;
	struct file *chrif_filp;
    unsigned int irq;
    unsigned int evtchn;
    struct request_queue *rq;
    unsigned int flush_op;
    struct cdev cdev;
	struct pdma_info pdma_info;
	struct pdma_stat stat;
	struct pdma_rw_reg ctrl;
    int share_memory_pn;
};


// static const struct character_device_operations vcd_character_fops;
static int create_chrdev(struct  chrfront_info* info);

static void chrif_free(struct chrfront_info *info, int suspend)
{
	printk(KERN_DEBUG "\nxen: DomU: chrif_free()- entry");

    /* Free resources associated with old device channel. */
    if (info->ring_ref != GRANT_INVALID_REF) {
        gnttab_end_foreign_access(info->ring_ref, 0,(unsigned long)info->ring.sring);
        info->ring_ref = GRANT_INVALID_REF;
        info->ring.sring = NULL;
    }
    if (info->irq)
        unbind_from_irqhandler(info->irq, info);
    info->evtchn = info->irq = 0;
}

static int chrfront_remove(struct xenbus_device *xbdev)
{
    struct chrfront_info *info = dev_get_drvdata(&xbdev->dev);
    struct block_device *bdev = NULL;
	printk(KERN_DEBUG "\nxen: DomU: chrfront_remove()- entry");

    dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

    if (!bdev) {
        kfree(info);
        return 0;
    }
    return 0;
}

static inline void flush_requests(struct chrfront_info *info)
{
    int notify;
    //put request on the ring and check wether or not notify domo 
    printk(KERN_DEBUG "\nxen: DomU: flush_requests() entry");
    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->ring, notify);
    //notify equal 1 means there is no requset on ring,needing to wake up dom0
    if (notify)
    {
        notify_remote_via_irq(info->irq);
        printk(KERN_DEBUG "\nxen: DomU: notify  to Dom0");
    } else 
    {
        notify_remote_via_irq(info->irq);
        printk(KERN_DEBUG "\nxen:DomU: No notify to Dom0");
    }
}


/*
 * do_chrif_request
 * request is in a request queue
 */
static void do_chrif_request(struct chrif_request *ring_req,struct chrfront_info *info)
{
    printk(KERN_INFO "\n----------------------------start request------------------------------");
    printk(KERN_DEBUG "\nxen:DomU: Fill in IDX-%d",info->ring.req_prod_pvt);

    info->ring.req_prod_pvt += 1;
    printk(KERN_DEBUG "\nxen: DomU: start run flush_requests");
    if (info->ring.req_prod_pvt != 0)
        flush_requests(info);
}

static int map_page_to_backend(struct  chrfront_info* info)
{
    // struct page *granted_page;
    struct chrif_request *ring_req ;
    char * page;
    unsigned long *vtm;
    int i;
    printk(KERN_DEBUG "\nxen: DomU: map_page_to_backend() entry");

	//get request and fill it
    if (reqid == 1000)  reqid = 0;
    ring_req = RING_GET_REQUEST(&(info->ring), info->ring.req_prod_pvt);
    ring_req->id = reqid;
    reqid ++;
    ring_req->operation = CHRIF_OP_MAP;
    ring_req->status = 0;
    // ring_req->length = length;
    //send write request to dom0 

    printk(KERN_DEBUG "\nxen: DomU:  %d map_pages", info->share_memory_pn);

    page = kmalloc(info->share_memory_pn * PAGE_SIZE, GFP_KERNEL);
    for(i = 0; i < info->share_memory_pn; i++){
        //assign rw operation page to dom0
        info->op_pages[i] = (unsigned long)page;
        printk(KERN_INFO "\nxen: DomU: info->op_pages[%d]: 0x%x", 
        i, (char *)info->op_pages[i]);
               
        if (page == 0) {
            printk(KERN_DEBUG "\nxen: DomU: could not get free page");
            return -1;
        }
		
        vtm = (unsigned long*)virt_to_mfn(page);
        ring_req->op_gref[i] = gnttab_grant_foreign_access(DOM0_ID, (unsigned long)vtm, 0);
        if(ring_req->op_gref[i] < 0) {
            printk(KERN_DEBUG "\nxen: DomU: could not grant page for foreign access");
            free_page((unsigned long)page);
            return 0;
        }
           
		//serve grant ref
		info->op_gref[i] =  ring_req->op_gref[i];
        printk(KERN_DEBUG "\nxen: DomU: req->op_gref[%d] : %d", i , ring_req->op_gref[i]);
        page += PAGE_SIZE;
    }
    
    memset((char *)info->op_pages[0], 0, info->share_memory_pn * PAGE_SIZE);
    do_chrif_request(ring_req, info);
    
    return 0;
}

static  int chrif_open(struct inode *inode, struct file *filp)
{
    struct chrfront_info *info;
    printk(KERN_DEBUG "\nxen: DomU: chrif_open() entry");
    info = container_of(inode->i_cdev, struct chrfront_info, cdev);
    filp->private_data = info;

    printk(KERN_INFO "\nxen: DOmU: open op finished");
    return 0;   
}

/**
*@func -intercept user read request, read data from shared pages to user
*@parm len - the length need to read, including the space byte.
*
**/
static ssize_t chrif_read(struct file *filp, char *buf, size_t len, loff_t *off)
{
    struct chrfront_info *info = filp->private_data;
	printk(KERN_DEBUG "\nxen: DomU: chrif_read()- entry");
	
	wait_for_completion_interruptible(&comp_write);
	if(__copy_to_user(buf, (char *)info->op_pages[8], len))
        return -EFAULT;
    
    return len;
}

/**
*@func - intercept the request, then write data to shared pages
*@parm len - encodelen
*
**/
static ssize_t chrif_write(struct file *filp, const char *buf, size_t len, loff_t *off)
{
    struct chrif_request *ring_req;
    struct chrfront_info *info = filp->private_data;

	getnstimeofday(&timestar);
    printk(KERN_DEBUG "\nxen: DomU: chrif_write() entry"); 
    
    memset((void *)info->op_pages[0], 0, 8 * PAGE_SIZE);
    //write data from user to shared pages ,first four byte for length
    if (copy_from_user((char *)info->op_pages[0] , buf, len)){
        printk(KERN_DEBUG "\nxen: DomU: write to shared buffer error"); 
        return -EFAULT;
    }
        
    //get request and fill it   
	if (reqid == 1000)	 reqid = 0;
	ring_req = RING_GET_REQUEST(&(info->ring), info->ring.req_prod_pvt);
	ring_req->id = reqid;
	reqid ++;
	ring_req->operation = CHRIF_OP_WRITE;
	ring_req->status = 0;    
	ring_req->length = len;
	//send write request to dom0 
	do_chrif_request(ring_req,info);
	//wait for response from dom0
	

	//time count
/*    getnstimeofday(&timend);
    printk("\nxen: Dom0: use sec-%d, nsec-%d\n", 
			timend.tv_sec-timestar.tv_sec, 
			timend.tv_nsec-timestar.tv_nsec);
*/	
    return len; //timend.tv_nsec-timestar.tv_nsec;   //read_len;
}


static int chrif_release(struct inode *inode, struct file *filp)
{
   printk(KERN_DEBUG "\nxen: DomU: start close");
    return 0;
}
/*
 * The ioctl() implementation, it could be deleted
 */
// #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
long chrif_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
// #else
// int chrif_ioctl(struct inode *inode, struct file *filp,unsigned int cmd, unsigned long arg)
// #endif
{
    struct chrif_request *ring_req;
    struct chrfront_info *info = filp->private_data;
    int ret = 0;
    printk(KERN_DEBUG "\nxen: DomU: chrif_release() entry");
    
    if (_IOC_TYPE(cmd) != PDMA_IOC_MAGIC) { 
            return -ENOTTY;
        }
    if (_IOC_NR(cmd) > PDMA_IOC_MAXNR) {
            return -ENOTTY;
        }
    
    ring_req = RING_GET_REQUEST(&(info->ring), info->ring.req_prod_pvt);
    
    if (reqid == 1000)  reqid = 0;
    ring_req->id = reqid;
    reqid ++;

    ring_req->operation = CHRIF_OP_IOCTL;
    ring_req->status = 0;
 
    switch(cmd) {
		/*pdma dma*/
		case PDMA_IOC_START_DMA : 
		case PDMA_IOC_STOP_DMA:
			return 0;
			
		/*pdma info*/
		case PDMA_IOC_INFO:{
			if (copy_to_user((void *)arg, &info->pdma_info, sizeof(struct pdma_info))) {
                ret = -EFAULT;
            }
			return ret;
		}
		
        /* read/write register */
        case PDMA_IOC_RW_REG: {     
            if (copy_from_user(&info->ctrl, (void *)arg, sizeof(struct pdma_rw_reg))) {
                ret = -EFAULT;
            }
			info->ctrl.val = 0;
			/*read */
			if(info->ctrl.type == 0){
				if (copy_to_user((void *)arg, &info->ctrl, sizeof(struct pdma_rw_reg))) {
                ret = -EFAULT;
				}
			}
			return ret;
        }

        /* pdma stat */
        case PDMA_IOC_STAT: {
            //send request to dom0
			do_chrif_request(ring_req,info);
			printk(KERN_INFO "\nxen: DOmU: send ioctl request to backend");
        break;
        }

        default:  /* redundant, as cmd was checked against MAXNR */
        break;
    }
     
    //wait for response from dom0,then go on
    wait_for_completion_interruptible(&comp_ioctl);
    printk(KERN_INFO "\nxen: DOmU: ioctl op finished");
    
    /* pdma stat */
    if (copy_to_user((void *)arg, &info->stat, sizeof(struct pdma_stat))) {
        ret = -EFAULT;
    }

    return ret;
}


static irqreturn_t chrif_interrupt(int irq, void *dev_id)
{
    // struct request *req;
    // struct chrif_response *cret;
    // RING_IDX i, rp;
    // unsigned long flags;
    struct chrfront_info *info = (struct chrfront_info *)dev_id;
    // int error;
    RING_IDX i, rp;
    struct chrif_response *ring_resp;
	printk(KERN_DEBUG "\nxen: DomU: chrif_interrupt()- entry");
    printk(KERN_NOTICE "\n--------------------------get  response------------------------------");
again:
    rp = info->ring.sring->rsp_prod;
    printk(KERN_DEBUG "\nxen: DomU: ring pointers %d to %d", info->ring.rsp_cons, rp);
    for (i = info->ring.rsp_cons; i != rp; i++) {
        unsigned long id;
        ring_resp = RING_GET_RESPONSE(&(info->ring), i);
        printk(KERN_DEBUG "\nxen: DomU: Recvd in IDX-%d, with id=%d, op=%d, st=%d", i, ring_resp->id, ring_resp->operation, ring_resp->status);
        id = ring_resp->id;
 
        printk(KERN_DEBUG "\nxen: DomU: operation:  %s", op_name(ring_resp->operation));        
        switch(ring_resp->operation) {
            case CHRIF_OP_IOCTL:{
                if(ring_resp->status == 1)
                    printk(KERN_INFO "\nxen: DomU: ioctl response success");
                else printk(KERN_DEBUG "\nxen: DomU: ioctl response fail");
                info->stat = ring_resp->stat;                                    
                complete(&comp_ioctl);
                break;
            }

            case CHRIF_OP_WRITE:
                if(ring_resp->status == 1)
                    printk(KERN_INFO "\nxen: DomU: write response success");
                else printk(KERN_DEBUG "\nxen: DomU: write response fail");
                complete(&comp_write);
                break;
                
            default:
                printk(KERN_DEBUG "\nxen: DomU: unknow the operation");
                break;
        }
    }
    info->ring.rsp_cons = i;
    if (i != info->ring.req_prod_pvt) {
        int more_to_do;
        RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
        if (more_to_do)
            goto again;
    } else
        info->ring.sring->rsp_event = i + 1;
    printk(KERN_INFO "\nxen: DomU: chrif_interrupt is called");
    return IRQ_HANDLED;
}

//init shared ring and fronred ring,grant foreign access,bind event channle
static void chrfront_connect(struct chrfront_info *info)
{
    struct xenbus_transaction xbt;
	printk(KERN_DEBUG "\nxen: DomU: chrfont_connect()- entry");
    // int err;
    // struct evtchn_alloc_unbound alloc_unbound;
    // printk(KERN_INFO "\nxen: DomU: chrif_init is called");
    switch (info->connected) {
    case CHRIF_STATE_CONNECTED:

        return;

    case CHRIF_STATE_SUSPENDED:
        /*
         * If we are recovering from suspension, we need to wait
         * for the backend to announce it's features before
         * reconnecting, we need to know if the backend supports
         * persistent grants.
         */
        // chrif_recover(info);
        return;

    default:
        break;
    }
    
    xenbus_transaction_start(&xbt);
    xenbus_scanf(xbt, info->xbdev->otherend, "share_memory_pn", "%u", &info->share_memory_pn);
/*    xenbus_scanf(xbt, info->xbdev->otherend, "wt_block_sz", "%u", &info->pdma_info.wt_block_sz);
	xenbus_scanf(xbt, info->xbdev->otherend, "rd_block_sz", "%u", &info->pdma_info.rd_block_sz);
	xenbus_scanf(xbt, info->xbdev->otherend, "wt_pool_sz", "%lu", &info->pdma_info.wt_pool_sz);
	xenbus_scanf(xbt, info->xbdev->otherend, "rd_pool_sz", "%lu", &info->pdma_info.rd_pool_sz);
 */  
	xenbus_transaction_end(xbt, 0);
    printk(KERN_INFO "\nxen: DomU: share_memory_pn: %d", info->share_memory_pn);

	//serve mapped page,so that could  end foreign access
	info->op_pages = kmalloc(sizeof(unsigned long) * info->share_memory_pn, GFP_KERNEL);

    xenbus_switch_state(info->xbdev, XenbusStateConnected);
    printk(KERN_INFO "\nxen: DomU: state changed to connected");
    // spin_lock_irq(&info->io_lock);
    info->connected = CHRIF_STATE_CONNECTED;
    // spin_unlock_irq(&info->io_lock);
    // printk(KERN_INFO "\nxen: DomU: chrif state changed to connected");
	map_page_to_backend(info);
}

static void chrfront_closing(struct chrfront_info *info)
{
    struct xenbus_device *xbdev = info->xbdev;

    printk(KERN_INFO "\nxen: DomU: chrfront_closing() entry");
    xenbus_frontend_closed(xbdev);
    // xenbus_switch_state(xbdev, XenbusStateClosing);

}

static int setup_chrring(struct xenbus_device *dev, struct chrfront_info *info)
{
    int err;
    struct chrif_sring *sring;
    info->ring_ref = GRANT_INVALID_REF;
	printk(KERN_DEBUG "\nxen: DomU: setup_chrring()- entry");

    // sring = (struct chrif_sring *)__get_free_page(GFP_NOIO | __GFP_HIGH);
    sring =  (struct chrif_sring *)__get_free_pages(GFP_KERNEL, 0);
    if (!sring) {
        printk(KERN_DEBUG "\nxen: DomU: could not alloc shared ring");
        xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
        return -ENOMEM;
    }

    SHARED_RING_INIT(sring);
    FRONT_RING_INIT(&(info->ring), sring, PAGE_SIZE);
  
    err = xenbus_grant_ring(dev, virt_to_mfn(info->ring.sring));
    if(err < 0) {
        printk(KERN_DEBUG "\nxen: DomU: could not grant ring_page for foreign access");
        free_page((unsigned long)sring);
        info->ring.sring = NULL;
        goto fail;
    }
    info->ring_ref = err;
    // printk(KERN_DEBUG "\nxen:DomU: ring_ref = %d", info->ring_ref);
    
    err = xenbus_alloc_evtchn(dev, &info->evtchn);
    if(err)
        goto fail;

    err = bind_evtchn_to_irqhandler(info->evtchn, chrif_interrupt, 0, "chrif", info);
    if(err <= 0){
        printk(KERN_DEBUG "\nxen:DomU: bind evtchon to irqhandler failed ");
        xenbus_dev_fatal(dev, err,"bind_evtchn_to_irqhandler failed");
        goto fail;
    }
    info->irq = err;
    printk(KERN_INFO "\nxen: DomU: setup ring finished");
    return 0;

fail:
    chrif_free(info, 0);
    return err;
}

/* Common code used when first setting up, and when resuming. */
static int talk_to_chrback(struct xenbus_device *dev, struct chrfront_info *info)
{
    const char *message = NULL;
    int err;
	struct xenbus_transaction xbt;
    /* Create shared ring, alloc event channel. */
    err = setup_chrring(dev, info);
	printk(KERN_DEBUG "\nxen: DomU: talk_to_chrback()- entry");
    printk(KERN_INFO "\nxen: DomU: ring_ref:%d,evtchn:%d, irq:%d",info->ring_ref,info->evtchn,info->irq);

    if(err)
        goto out;

again:
    err = xenbus_transaction_start(&xbt);

    if (err) {
        xenbus_dev_fatal(dev, err, "starting transaction");
        goto destroy_chrring;
    }

    err = xenbus_printf(xbt, dev->nodename,"ring-ref", "%u", info->ring_ref);
    if (err) {
        message = "writing ring-ref";
        goto abort_transaction;
    }
    err = xenbus_printf(xbt, dev->nodename,"event-channel", "%u", info->evtchn);
    if (err) {
        message = "writing event-channel";
        goto abort_transaction;
    }
/*    err = xenbus_printf(xbt, dev->nodename, "protocol", "%s",XEN_IO_PROTO_ABI_NATIVE);
    if (err) {
        message = "writing protocol";
        goto abort_transaction;
    }
    err = xenbus_printf(xbt, dev->nodename,"feature-persistent", "%u", 1);
    if (err)
        dev_warn(&dev->dev,"writing persistent grants feature to xenbus");
*/
    err = xenbus_transaction_end(xbt, 0);
    if (err) {
        if (err == -EAGAIN)
            goto again;
        xenbus_dev_fatal(dev, err, "completing transaction");
        goto destroy_chrring;
    }
    //change the state to initialised
    xenbus_switch_state(dev, XenbusStateInitialised);
    printk(KERN_INFO "\nxen: DomU: talk to backend finished");
    printk(KERN_INFO "\nxen: DomU: state changed to xenbusinitalised");
	return 0;
abort_transaction:
    xenbus_transaction_end(xbt, 1);
    if (message)
        xenbus_dev_fatal(dev, err, "%s", message);
destroy_chrring:
    chrif_free(info, 0);
out:
    return err;
}


static int chrfront_probe(struct xenbus_device* dev, const struct xenbus_device_id* id)
{
    int err;
	struct chrfront_info *info;
	printk(KERN_DEBUG "\nxen: DomU: chrfront_probe()- entry");
    
    //kmalloc & kzalloc
    info = kmalloc(sizeof(*info), GFP_KERNEL);
    if (!info) {
        xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
        return -ENOMEM;
    }
    create_chrdev(info);

    info->xbdev = dev;
    dev_set_drvdata(&dev->dev, info);

    err = talk_to_chrback(dev, info);
    if(err){
        printk(KERN_INFO "\nxen: DomU: talk to chrback fail");
        kfree(info);
        dev_set_drvdata(&dev->dev, NULL);
        return err;
    }
    printk(KERN_INFO "\nxen: DomU: Probe finished");
    return 0;
}

/**
 * Callback received when the backend's state changes.
 */
static void chrback_changed(struct xenbus_device *dev,
                enum xenbus_state backend_state)
{
    struct chrfront_info *info = dev_get_drvdata(&dev->dev);
    printk(KERN_DEBUG "\nxen: DomU: chrback_changed()- entry");

    // dev_dbg(&dev->dev, "chrfront:chrback_changed to state %d.\n", backend_state);

    switch (backend_state) {
    case XenbusStateInitialising:
    case XenbusStateInitWait:
    case XenbusStateInitialised:
    case XenbusStateReconfiguring:
    case XenbusStateReconfigured:
    case XenbusStateUnknown:
        printk(KERN_INFO "\nxen: DomU: back state is NOT XenbusStateConnected");
        break;

    case XenbusStateConnected:
        printk(KERN_INFO "\nxen: DomU: back state is XenbusStateConnected");
        chrfront_connect(info);
        break;

    case XenbusStateClosed:
        printk(KERN_INFO "\nxen: DomU: back state is XenbusStateClosed");
        if (dev->state == XenbusStateClosed)
            break;
        /* Missed the backend's Closing state -- fallthrough */
    case XenbusStateClosing:
        chrfront_closing(info);
        break;
    }
}

struct file_operations chrif_fops=
{
    .open = chrif_open,
    .read = chrif_read,
    .write = chrif_write,
    .release = chrif_release,
    .owner = THIS_MODULE,
    // #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
    .unlocked_ioctl =  chrif_ioctl,
    // #else
    // .ioctl          =  chrif_ioctl,
    // #endif
};

static struct xenbus_device_id chrfront_ids[] =
{
    {"domtest"},
    {""},
};

// static struct DEFINE_XENBUS_DRIVER(chrfront, ,
//     .probe = chrfront_probe,
//     .remove = chrfront_remove,
//     .resume = chrfront_resume,
//     .otherend_changed = chrback_changed,
//     // .is_ready = chrfront_is_ready,
// );
static struct xenbus_driver chrfront_driver =
{
        .driver.name = "domtest",
        .driver.owner = THIS_MODULE,
        .ids = chrfront_ids,
        .remove = chrfront_remove,
        .otherend_changed = chrback_changed,
        .probe = chrfront_probe,
};



static int create_chrdev(struct  chrfront_info* info)
{
    int ret,err;
	printk(KERN_DEBUG "\nxen: DomU: create_chrdev()- entry");

    //allocate device number dynamically
    ret=alloc_chrdev_region(&dev_no, 0, 1, DEV_NAME);
    if(ret)
    {
        printk(KERN_DEBUG "\nxen: DomU: pdma register failure");
        unregister_chrdev_region(dev_no, 1);
        return ret;
    }
    else
    {
        printk(KERN_DEBUG "\nxen: DomU: pdma register success");
    }
    cdev_init(&info->cdev, &chrif_fops);
    info->cdev.owner = THIS_MODULE;
    info->cdev.ops = &chrif_fops;

    err = cdev_add(&info->cdev, dev_no, 1);
    if(err)
    {
        printk(KERN_DEBUG "\nxen: DomU: error %d adding pdma", err);
        unregister_chrdev_region(dev_no, 1);
        return err;
    }
    domt_class=class_create(THIS_MODULE, DEV_NAME);
    if(IS_ERR(domt_class))
    {
        printk(KERN_DEBUG "\nxen: DomU: ERR:cannot create a domt_class");  
        unregister_chrdev_region(dev_no, 1);
        return -1;
    }
    device_create(domt_class, NULL, dev_no, 0, DEV_NAME);
    return ret;
}

static int __init chrif_init(void)
{
    int ret;
	printk(KERN_DEBUG "\nxen: DomU: chrif_init()- entry");
	
    printk(KERN_DEBUG "\nxen: DomU: START init......................\n");
    if (!xen_domain())
        return -ENODEV;
    if (xen_hvm_domain() && !xen_platform_pci_unplug)
        return -ENODEV;
    
    // xenbus_register_frontend(&chrfront_driver);
   	ret = xenbus_register_frontend(&chrfront_driver);
    if (ret) {
        unregister_chrdev_region(dev_no, 1);//
        return ret;
    }
   	printk(KERN_ALERT"\nxen: DomU: driver register success");
    // create_domdev();
    init_completion(&comp_ioctl);
    init_completion(&comp_write);

   	return 0;
}
module_init(chrif_init);

static void __exit chrif_exit(void)
{
	printk(KERN_DEBUG "\nxen: DomU: chrif_exit()- entry");
	xenbus_unregister_driver(&chrfront_driver);
    device_destroy(domt_class, dev_no);
    class_destroy(domt_class);
    unregister_chrdev_region(dev_no,1);

    printk(KERN_DEBUG "\nxen: DomU: END exit......................\n");
 }
module_exit(chrif_exit);

MODULE_DESCRIPTION("Xen virtual character device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(XENVCD_MAJOR);
MODULE_ALIAS("xen:vcd");
MODULE_ALIAS("xenchr");
