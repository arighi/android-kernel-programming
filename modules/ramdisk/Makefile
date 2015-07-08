ifndef KERNELRELEASE
ifndef KDIR
KDIR:=/lib/modules/`uname -r`/build
endif

PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -f *.o *.ko *.mod.* .*.cmd Module.symvers modules.order
	rm -rf .tmp_versions

install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules_install
else
     obj-m := ramdisk.o
endif
