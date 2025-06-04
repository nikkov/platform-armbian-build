snd-soc-i2s_codec-objs := i2s_codec.o
obj-m := snd-soc-i2s_codec.o

PWD := $(CURDIR)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
	sudo cp snd-soc-i2s_codec.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	sudo depmod -a
