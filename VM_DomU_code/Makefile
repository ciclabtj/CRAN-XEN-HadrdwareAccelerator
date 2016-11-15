obj-m += xen-chrfront.o
KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) SUBDIRS=$(PWD) clean
