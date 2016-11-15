#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <xen/interface/xen.h>
#include <xen/xen.h>
#include <asm/xen/hypercall.h>
#include <xen/events.h>
#include <linux/init.h>
#include <xen/page.h>
#include <linux/spinlock.h>
#include<linux/sched.h>
#include<linux/time.h>

#include "xen-dom.h"
#include "pdma-ioctl.h"
MODULE_LICENSE("GPL");

static int xen_chrif_reqs = 64;
module_param_named(reqs, xen_chrif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of chrback requests to allocate");
mm_segment_t old_fs;
static DEFINE_MUTEX(pdma_lock);

//time count
struct timespec timestar;
struct timespec timend;


struct pending_req {
	struct xen_chrif	*chrif;
	struct list_head	free_list;
};

struct xen_chrbk {
	struct pending_req	*pending_reqs;
	/* List of all 'pending_req' available */
	struct list_head	pending_free;
	/* And its spinlock. */
	spinlock_t		pending_free_lock;
	wait_queue_head_t	pending_free_wq;
	wait_queue_head_t   write_pdma_wq;
	int   write_pdma_ws;
	/* The list of all pages that are available. */
	struct page		**pending_pages;
	/* And the grant handles that are available. */
	grant_handle_t		*pending_grant_handles;
	struct   file *chrif_filp;
	/*the mark to operate pdma*/
	atomic_t   open_cnt;
	int wting_domu_id;
	unsigned int writed_sz;
	unsigned int readed_sz;
};

static struct xen_chrbk *chrbk;

/*
 * Retrieve from the 'pending_reqs' a free pending_req structure to be used.
 */
static struct pending_req *alloc_req(void)
{
	struct pending_req *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&chrbk->pending_free_lock, flags);
	if (!list_empty(&chrbk->pending_free)) {
		req = list_entry(chrbk->pending_free.next, struct pending_req,
				 free_list);
		list_del(&req->free_list);
	}
	spin_unlock_irqrestore(&chrbk->pending_free_lock, flags);
	return req;
}

/*
 * Return the 'pending_req' structure back to the freepool. 
 * wake up the thread if it was waiting for a free page.
 */
static void free_req(struct pending_req *req)
{
	unsigned long flags;
	int was_empty;

	spin_lock_irqsave(&chrbk->pending_free_lock, flags);
	was_empty = list_empty(&chrbk->pending_free);
	list_add(&req->free_list, &chrbk->pending_free);
	spin_unlock_irqrestore(&chrbk->pending_free_lock, flags);
	if (was_empty)
		wake_up(&chrbk->pending_free_wq);
}

/*
 * map fronted wr_pages
 */
static int xen_chrbk_map(struct chrif_request *request, struct xen_chrif *xen_chrif)
{
	int i, count;
//    unsigned long *map_page;
    struct vm_struct *vm_area;
	struct chrif_request *req = request;
	struct xen_chrif *chrif = xen_chrif;
	struct gnttab_map_grant_ref map_ops[MAX_GREF];
	chrif->nr_pages = req->nr_pages;
    count = req->nr_pages;
    printk(KERN_DEBUG "\nxen: dom0: page_size->%x", PAGE_SIZE);

    vm_area = alloc_vm_area(chrif->nr_pages * PAGE_SIZE, NULL);
	for(i = 0; i < count; i++){
		//map_page = &chrif->map_pages[i];
		chrif->map_pages[i] = (unsigned long)(vm_area->addr + i*PAGE_SIZE);
        gnttab_set_map_op(&map_ops[i], (unsigned long)chrif->map_pages[i], 
			GNTMAP_host_map, req->op_gref[i], chrif->domid);
   
		if(HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, &map_ops[i], 1)){
       		printk(KERN_DEBUG "\nxen: dom0: map pages failed");
			return -EFAULT;
		}

		if (map_ops[i].status) {
			struct gnttab_unmap_grant_ref unmap_op;
			gnttab_set_unmap_op(&unmap_op, (unsigned long)chrif->map_pages[i], 
				GNTMAP_host_map, map_ops[i].handle);
			HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &unmap_op, 1);
			printk(KERN_DEBUG "\nxen: dom0: map_op.status fail");
			return map_ops[i].status;
		}

       	printk(KERN_DEBUG "\nxen: dom0: vm_area->addr[%d]: 0x%x ", 
        i, vm_area->addr+i*PAGE_SIZE);
       	printk(KERN_DEBUG "\nxen: dom0: chrif->map_pages[%d]: 0x%x ",
        i, chrif->map_pages[i]);
/*		printk(KERN_DEBUG "\nxen: dom0: vm_area->data[%d]: 0x%d ", 
        i, *((int *)vm_area->addr));
		printk(KERN_DEBUG "\nxen: dom0: chrif->data[%d]: 0x%d ", 
        i, *((int *)chrif->map_pages[i]));
*/
		chrif->pages_handle[i] = map_ops[i].handle;
       	printk(KERN_DEBUG "\nxen: dom0: map %d page finished, handle: %d", 
            i, chrif->pages_handle[i]);
	}

	return 0;
}

/*
 * unmap fronted wt_pages
 */
void xen_chrbk_unmap(struct xen_chrif *chrif)
{
    int i;
    unsigned long map_page;
	struct gnttab_unmap_grant_ref unmap_ops[MAX_GREF];
	int count = chrif->nr_pages;

	for(i = 0; i < count; i++){
        map_page = chrif->map_pages[i];
		gnttab_set_unmap_op(&unmap_ops[i], (unsigned long)map_page, 
				GNTMAP_host_map, chrif->pages_handle[i]);
		HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, &unmap_ops[i], 1);
	}

	//free_vm_area(chrif->map_pages);
}

/*
 * Notification from the guest OS.
 */
static void chrif_notify_request(struct xen_chrif  *chrif)
{
	chrif->waiting_reqs =1;
	wake_up(&chrif->wq);
}

irqreturn_t chrif_int(int irq, void *dev_id)
{
	chrif_notify_request(dev_id);
    printk(KERN_DEBUG "\n------------------------------start response-------------------------------------");
    printk(KERN_DEBUG "\nxen: Dom0: chrif_int called with dev_id=%x ", (unsigned int)dev_id);

	return IRQ_HANDLED;
}

static void make_response(struct xen_chrif *xen_chrif, struct chrif_request *req, struct chrif_response *response)
{
	struct xen_chrif  *chrif = xen_chrif;
	struct chrif_response *resp = response;
	unsigned long flags;
	int notify;

	resp->id = req->id;
    resp->operation = req->operation;
    resp->status = req->status + 1;
	
    spin_lock_irqsave(&chrif->chr_ring_lock, flags);
    
	memcpy(RING_GET_RESPONSE(&chrif->chr_ring, chrif->chr_ring.rsp_prod_pvt), resp, sizeof(*resp));
    chrif->chr_ring.rsp_prod_pvt++;
    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&chrif->chr_ring, notify);
	spin_unlock_irqrestore(&chrif->chr_ring_lock, flags);
       
    printk(KERN_DEBUG "\nxen:Dom0: make response id = %d, op=%s, status=%d",
		resp->id, op_name(resp->operation), resp->status);
	
    if (notify) {
		printk(KERN_DEBUG "\nxen:dom0:send notify to domu");
        notify_remote_via_irq(chrif->irq);
     }
}

static int do_read_resp(struct xen_chrif *xen_chrif, struct chrif_request *req, struct chrif_response *resp)
{
	int err, i, k;
	struct xen_chrif *chrif = xen_chrif;
	unsigned long map_page;
	struct pdma_rw_reg ctrl;
	int count = chrif->nr_pages;
	if(chrbk->readed_sz == 0){
        ctrl.type = 0; //read
		ctrl.addr  = 0;
		old_fs = get_fs();
		set_fs(get_ds());
		err = chrbk->chrif_filp->f_op->unlocked_ioctl(chrbk->chrif_filp, PDMA_IOC_RW_REG, (unsigned long)&ctrl);	
		set_fs(old_fs);
	    printk(KERN_DEBUG "\nxen: Dom0: read form reg: %d", ctrl.val);
		if(err){
			printk(KERN_DEBUG "\nxen: Dom0: rw-reg ioctl failed");
		}
	}

    /* send all data to frontend, every time send a blcok */
	old_fs = get_fs();
	set_fs(get_ds());
	//printk(KERN_DEBUG "\nxen: Dom0: read position 3, chrif->pdma_info.wt_block_sz: %d", chrif->pdma_info.wt_block_sz);
	err = chrbk->chrif_filp->f_op->read(chrbk->chrif_filp, chrif->map_pages[0], chrif->pdma_info.wt_block_sz, &chrbk->chrif_filp->f_pos);	
	//printk(KERN_DEBUG "\nxen: Dom0: read position 4, chrif->pdma_info.wt_block_sz: %d", chrif->pdma_info.wt_block_sz);
	set_fs(old_fs);
	if(err < 0){
		printk(KERN_DEBUG "\nxen: Dom0: read %u bytes error", req->length);
		return err;   //modify
	}
	printk(KERN_DEBUG "\nxen: Dom0: read data from pdma");
    
    //time count
    getnstimeofday(&timend);
    printk(KERN_INFO "\nxen: Dom0: use sec-%d, nsec-%d\n", timend.tv_sec-timestar.tv_sec, timend.tv_nsec-timestar.tv_nsec);

	printk(KERN_DEBUG "\nxen: Dom0: put read data into page");
	
	//now don't know the length, so use req->length
	resp->length = ctrl.val;
	//resp->length = req->length;
	make_response(chrif, req, resp);  //modify
	printk(KERN_DEBUG "\nxen: Dom0: response read");

	chrbk->readed_sz += chrif->pdma_info.wt_block_sz;
	printk(KERN_DEBUG "\nxen: Dom0: readed_sz:%d", chrbk->readed_sz);
	printk(KERN_DEBUG "\nxen: Dom0: resp->length:%d", resp->length);
	//finish read
	if(chrbk->readed_sz >= resp->length){
		chrbk->readed_sz = 0;
		chrbk->wting_domu_id = -1;
		if(waitqueue_active(&chrbk->write_pdma_wq)){
            chrbk->write_pdma_ws = 1;
		    wake_up(&chrbk->write_pdma_wq);
	        printk(KERN_DEBUG "\nxen: Dom0: wake up waitqueue");
		}

        //reset reg 
	    ctrl.type = 1; //write
	    ctrl.addr  = 0;
	    ctrl.val = 1;
	    old_fs = get_fs();
	    set_fs(get_ds());
	    err = chrbk->chrif_filp->f_op->unlocked_ioctl(chrbk->chrif_filp, PDMA_IOC_RW_REG, (unsigned long)&ctrl);	
	    set_fs(old_fs);
	    if(err){
		    printk(KERN_DEBUG "\nxen: Dom0: rw-reg ioctl failed");
	    }	
	}

	return 0;
}

static int do_write_resp(struct xen_chrif *xen_chrif, struct chrif_request *req, struct chrif_response *resp)
{
	int i, j, err, code_len;
//  int k;
	unsigned long map_page;
	struct pdma_rw_reg ctrl;
    struct xen_chrif *chrif = xen_chrif;
	int count = chrif->nr_pages;
    struct timespec timecur;

	printk(KERN_DEBUG "\nxen: Dom0: write: nr-pages %d", count);

	/*the first time to write data, alloc buffer*/
	if(chrbk->writed_sz == 0){
        //reset reg 
	    ctrl.type = 1; //write
	    ctrl.addr  = 0;
	    ctrl.val = 1;
	    old_fs = get_fs();
	    set_fs(get_ds());
	    err = chrbk->chrif_filp->f_op->unlocked_ioctl(chrbk->chrif_filp, PDMA_IOC_RW_REG, (unsigned long)&ctrl);	
		//printk(KERN_DEBUG "\nxen: Dom0: reset reg------------");
	    set_fs(old_fs);
	    if(err){
		    printk(KERN_DEBUG "\nxen: Dom0: rw-reg ioctl failed");
	    }
	//	schedule_timeout_uninterruptible(500);
	}
    
    getnstimeofday(&timestar);
        
	//interval some time from reset reg
    if(chrbk->writed_sz == 0){
        //write reg len
	    code_len = (req->length/8) << 19;
	    ctrl.val = code_len;
	    old_fs = get_fs();
	    set_fs(get_ds());
	    err = chrbk->chrif_filp->f_op->unlocked_ioctl(chrbk->chrif_filp, PDMA_IOC_RW_REG, (unsigned long)&ctrl);	
	    set_fs(old_fs);
		printk(KERN_INFO "\nxen: Dom0: write reg :%x, byte: %d------------", ctrl.val, (req->length/8-4)/8);
	    if(err){
		    printk(KERN_DEBUG "\nxen: Dom0: rw-reg ioctl failed");
	    }
	//	schedule_timeout_uninterruptible(500);
    }

    /* write data from buffer to  pdma device */ 
    getnstimeofday(&timecur);
    printk(KERN_DEBUG "\nxen: Dom0: write time--->%ds, %dns", timecur.tv_sec, timecur.tv_nsec);
	
    old_fs = get_fs();
	set_fs(get_ds()); 
	err = chrbk->chrif_filp->f_op->write(chrbk->chrif_filp, chrif->map_pages[0], 
							chrif->pdma_info.wt_block_sz, &chrbk->chrif_filp->f_pos);	
	set_fs(old_fs);
    if(err < 0){
		printk(KERN_DEBUG "\nxen: Dom0: write %u bytes error", chrif->pdma_info.wt_block_sz);
		return err;
	}
    memset(chrif->map_pages[0], 0, PAGE_SIZE*count);
	chrbk->writed_sz += chrif->pdma_info.wt_block_sz;
	//printk(KERN_DEBUG "\nxen: Dom0: write: writed into pdma");
	 		
	/*after finishing all write, read data from pdma device, send to frontend */
    //response write
    make_response(chrif, req, resp);
	//printk(KERN_DEBUG "\nxen: dom0: response write, and writed_sz: %u", chrbk->writed_sz);
  
    if(chrbk->writed_sz >= req->length){ 
		chrbk->writed_sz = 0;       
        printk(KERN_DEBUG "\nxen: Dom0: write finished, code_len: %08x", code_len);
	}

	return 0;
}

static int do_ioctl_resp(struct xen_chrif *chrif, struct chrif_request *req, struct chrif_response *resp)
{
	int err = 0;
	struct pdma_stat pdma_stat;
	
	old_fs = get_fs();
	set_fs(get_ds());
	err = chrbk->chrif_filp->f_op->unlocked_ioctl(chrbk->chrif_filp, PDMA_IOC_STAT, (unsigned long)&pdma_stat);	
	set_fs(old_fs);
	if(err){
		printk(KERN_DEBUG "\nxen: Dom0: stat ioctl failed");
	}else  printk(KERN_DEBUG "\nxen: Dom0: stat ioctl success");
	resp->stat = pdma_stat;	

	make_response(chrif, req, resp);
	printk(KERN_INFO "\nxen: Dom0: response ioctl");

	return err;
}


static int __do_char_io_op(struct xen_chrif *xen_chrif)
{
	struct xen_chrif *chrif = xen_chrif;
	struct pending_req  *pending_req;
    struct chrif_request  *req;
	chrif_response_t  *resp;
	RING_IDX  rc, rp;    
	int err;
	int more_to_do = 0;
    resp = kmalloc(sizeof(*resp), GFP_KERNEL);
 
	rc = chrif->chr_ring.req_cons;
    rp = chrif->chr_ring.sring->req_prod;
	rmb();
    printk(KERN_DEBUG "\nxen: Dom0: rc = %d rp = %d", rc, rp);

	while (rc != rp) {
       if (RING_REQUEST_CONS_OVERFLOW(&chrif->chr_ring, rc))
           break;
       
	   if(kthread_should_stop()){
		   more_to_do = 1;
		   break;
	   }

	   pending_req = alloc_req();
	   if (NULL == pending_req) {
			more_to_do = 1;
			break;
		}

	   //memcpy(req, RING_GET_REQUEST(&chrif->chr_ring, rc), sizeof(*req));
       req = RING_GET_REQUEST(&chrif->chr_ring, rc);
       chrif->chr_ring.req_cons = ++rc;
      
       barrier();
       printk(KERN_DEBUG "\nxen:Dom0:Recvd at IDX-%d: id = %d, op=%s, status=%d", 
		   rc, req->id, op_name(req->operation), req->status);
	   
		switch(req->operation) {
				case CHRIF_OP_OPEN:{
					if (atomic_inc_return(&chrbk->open_cnt) == 1)
						chrbk->chrif_filp = filp_open(DEVICE_PATH, O_RDWR, 0);
					make_response(chrif, req, resp);
					printk(KERN_DEBUG "\nxen: dom0: response open");
					break;
				 }
			
			  case CHRIF_OP_WRITE:{
					printk(KERN_DEBUG "\nxen: Dom0: domu_id: %d", chrbk->wting_domu_id);
		    		mutex_lock(&pdma_lock);
					if(chrbk->wting_domu_id == -1){
						chrbk->wting_domu_id = chrif->domid;
						printk(KERN_DEBUG "\nxen: Dom0: ==domu_id: %d", chrbk->wting_domu_id);
					}
					if(chrbk->wting_domu_id != chrif->domid){
						printk(KERN_DEBUG "\nxen: Dom0: !=domu_id: %d", chrbk->wting_domu_id);
						chrbk->write_pdma_ws = 0;
						mutex_unlock(&pdma_lock);
						printk(KERN_DEBUG "\nxen: Dom0: add to waitqueue");
						wait_event_interruptible(chrbk->write_pdma_wq, chrbk->write_pdma_ws);
						chrbk->wting_domu_id = chrif->domid;
					}
                    //just for test
					//schedule_timeout_uninterruptible(500);

					if(do_write_resp(chrif, req, resp))
						printk(KERN_DEBUG "\nxen: dom0: response write fail");
					else printk(KERN_DEBUG "\nxen: dom0: response write finished");

					mutex_unlock(&pdma_lock);
					break;
			 }
			case CHRIF_OP_READ:
				printk(KERN_DEBUG "\nxen: dom0: CHRIF_OP_READ");
				do_read_resp(chrif, req, resp);
				break;

			case CHRIF_OP_IOCTL:
				mutex_lock(&pdma_lock);
				if (do_ioctl_resp(chrif, req, resp))
					req->status-=1;
				mutex_unlock(&pdma_lock);
				break;

			case CHRIF_OP_CLOSE:{
				if (atomic_dec_and_test(&chrbk->open_cnt)) //test == 0 return true;
					filp_close(chrbk->chrif_filp, NULL);

				make_response(chrif, req, resp);
				printk(KERN_DEBUG "\nxen: Dom0: response close");
				break;
			}
			
			case CHRIF_OP_MAP:
				if(xen_chrbk_map(req, chrif))
					printk(KERN_INFO "\nxen: Dom0: map fronted pages fail");
				else printk(KERN_INFO "\nxen: Dom0: map fronted pages finished");
				break;
			
			default:
				printk(KERN_DEBUG "\nxen: Dom0: unknow the operation");
				req->status-=1;
				make_response(chrif, req, resp);
				break;
		}
	   free_req(pending_req);      
	   
	}

    return more_to_do;
}

static int do_char_io_op(struct xen_chrif *xen_chrif)
{
	struct xen_chrif *chrif = xen_chrif;
	int more_to_do;

	do {
		more_to_do = __do_char_io_op(chrif);
		if (more_to_do)
			break;

		RING_FINAL_CHECK_FOR_REQUESTS(&chrif->chr_ring, more_to_do);
	} while (more_to_do);

	return more_to_do;
}

/*
*the entry function of thread
*/
int xen_chrif_schedule(void *arg)
{
	struct xen_chrif *chrif = arg;
	while(!kthread_should_stop()){
		wait_event_interruptible(chrif->wq, 
					            chrif->waiting_reqs||
                                kthread_should_stop());
		wait_event_interruptible(chrbk->pending_free_wq,
								!list_empty(&chrbk->pending_free)||
								kthread_should_stop());
		chrif->waiting_reqs = 0;
		smp_mb();

		if(do_char_io_op(chrif))
			chrif->waiting_reqs = 1;
	}

	chrif->xenchrd = NULL;

	return 0;
}

static int dom_init(void) 
{
    int i;
	int err = 0;
    
    chrbk = kzalloc(sizeof(struct xen_chrbk), GFP_KERNEL);
    if(!chrbk){
        printk(KERN_DEBUG "\nxen: dom0: out of memoty when alloc chrbk");
    }

	chrbk->pending_reqs = kzalloc(sizeof(chrbk->pending_reqs[0]) *xen_chrif_reqs, GFP_KERNEL);

	INIT_LIST_HEAD(&chrbk->pending_free);
	spin_lock_init(&chrbk->pending_free_lock);
	init_waitqueue_head(&chrbk->pending_free_wq);
	init_waitqueue_head(&chrbk->write_pdma_wq);

	atomic_set(&chrbk->open_cnt, 0);
	chrbk->writed_sz = 0;
	chrbk->readed_sz = 0;
	chrbk->wting_domu_id = -1;

	for (i = 0; i < xen_chrif_reqs; i++)
		list_add_tail(&chrbk->pending_reqs[i].free_list, &chrbk->pending_free);
    
	err = xen_chrif_xenbus_init();
    if(err)
		goto failed_init;

	return 0;

failed_init:
	kfree(chrbk->pending_reqs);
	kfree(chrbk);
	chrbk = NULL;
	return err;
}

static void dom_exit(void)
{
    xen_chrif_xenbus_exit( );
}

module_init(dom_init);
module_exit(dom_exit);
 
