# Copyright (C) 2012 Alexander F. Mayer
obj-m := testsubsys0.o
ccflags-y := -I$(src)/../include
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

