obj-m := xen-chrback.o 

xen-chrback-y := data_transfer.o dom_connect.o

KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) clean
