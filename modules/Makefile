# Copyright (C) 2012 Alexander F. Mayer
obj-y := testsubsys0/ ttyhub/
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

