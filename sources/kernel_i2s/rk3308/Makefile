snd-soc-rockchip-i2s-tdm-objs := rockchip_i2s_tdm.o
obj-m := snd-soc-rockchip-i2s-tdm.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo cp snd-soc-rockchip-i2s-tdm.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/rockchip/
	sudo depmod -a
