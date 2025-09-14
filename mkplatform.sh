#!/bin/bash
C=`pwd`
A=../armbian
BRANCH=edge
BOARD=$1
V=v25.08
D=${BOARD}-armbian
RELEASE=bookworm

case $BOARD in
'nanopineo' | 'nanopiair' | 'orangepipc')
  FAMILY="sunxi"
  SOC="sun8i-h3"
  BOOT_SCRIPT=boot-sunxi.cmd
  ;;
'nanopineo2' | 'nanopineoplus2' | 'nanopineo2black')
  FAMILY="sunxi64"
  SOC="sun50i-h5"
  BOOT_SCRIPT=boot-sun50i-next.cmd
  ;;
'cubietruck')
  FAMILY="sunxi"
  SOC="sun7i-a20"
  BOOT_SCRIPT=boot-sunxi.cmd
  ;;
'nanopineo3')
  FAMILY="rockchip64"
  SOC="rk3328"
  BOOT_SCRIPT=boot-rockchip64.cmd
  ;;
'rockpi-s')
  FAMILY="rockchip64"
  SOC="rk3308"
  BOOT_SCRIPT=boot-rockchip64-ttyS0.cmd
  ;;
*)
  echo "Please set known board name as script parameter"
  exit 1
  ;;
esac

case $FAMILY in
'sunxi' | 'sunxi64')
  LINUX_CONGIG_NAME="linux-${FAMILY}-${BRANCH}.config"
  PATCHES_SRC_DIR=${C}/patches/kernel/sunxi
  PATCHES_DST_DIR=${C}/${A}/userpatches/kernel/archive/sunxi-6.6
  ;;
'rockchip64')
  LINUX_CONGIG_NAME="linux-${FAMILY}-${BRANCH}.config"
  PATCHES_SRC_DIR=${C}/patches/kernel/${FAMILY}/${SOC}
  PATCHES_DST_DIR=${C}/${A}/userpatches/kernel/archive/rockchip64-6.16
  ;;
esac

echo "-----Build SOC files for ${BOARD} - ${SOC}-----"

if [ ! -d ${A} ]; then
  echo "Armbian folder not exists"
  echo "Clone Armbian repository to folder ${A}"
  git clone https://github.com/armbian/build ${A}
else
  echo "Armbian folder already exists - keeping it"
fi

cd ${A}
CUR_BRANCH=`git branch --show-current`
echo "-----Current armbian branch is ${CUR_BRANCH}-----"
if [ "${CUR_BRANCH}" != "${V}" ];
then
  echo "Armbian branch will changed from ${CUR_BRANCH} to ${V}"
  git fetch
  git switch ${V} && touch .ignore_changes
fi

cd ${C}
echo "Clean old kernel config and patches..."
rm -rf ${C}/${A}/userpatches

echo "Copy kernel config and patches..."
mkdir -p ${PATCHES_DST_DIR}

cp ./${A}/config/kernel/${LINUX_CONGIG_NAME} ${C}/${A}/userpatches/

echo "Enable PCM5102A and custom codec drivers in kernel config..."
case $SOC in
'sun8i-h3' | 'sun7i-a20')
  sed -i "s/# CONFIG_SND_SOC_PCM5102A is not set/CONFIG_SND_SOC_PCM5102A=m\nCONFIG_SND_SOC_I2S_CODEC=m/1" ./${A}/userpatches/${LINUX_CONGIG_NAME}
  ;;
'sun50i-h5' | 'rk3328'  | 'rk3308')
  sed -i "s/CONFIG_SND_SOC_PCM5102A=m/CONFIG_SND_SOC_PCM5102A=m\nCONFIG_SND_SOC_I2S_CODEC=m/1" ./${A}/userpatches/${LINUX_CONGIG_NAME}
  ;;
esac

#copy kernel patches
cp ${PATCHES_SRC_DIR}/*.patch ${PATCHES_DST_DIR}/

cd ${A}

echo "Clean old debs"
rm -rf ./${A}/output/debs
#echo "U-Boot & kernel compile for ${BOARD}"
#./compile.sh BUILD_ONLY="u-boot'kernel,armbian-firmware" SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes RELEASE=${RELEASE} KERNEL_CONFIGURE=no KERNELSOURCE="https://github.com/torvalds/linux" KERNELBRANCH="tag:v6.15"
#./compile.sh BUILD_ONLY="u-boot'kernel,armbian-firmware" SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes RELEASE=${RELEASE} KERNEL_CONFIGURE=no
#echo "U-Boot compile for ${BOARD}"
#./compile.sh uboot SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes
#echo "Firmware compile for ${BOARD}"
#./compile.sh firmware SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes
#echo "Kernel compile for ${BOARD}"
#./compile.sh kernel SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes KERNEL_CONFIGURE=no
./compile.sh BUILD_ONLY="u-boot'kernel,armbian-firmware" SHARE_LOG=no ARTIFACT_IGNORE_CACHE='yes' BOARD=${BOARD} BRANCH=${BRANCH} BUILD_MINIMAL=yes RELEASE=${RELEASE} KERNEL_CONFIGURE=no
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Error compile!"
    exit $retVal
fi

cd ${C}
rm -rf ./${D}
mkdir ./${D}
mkdir ./${D}/u-boot
mkdir -p ./${D}/usr/sbin

echo "Install packages for ${BOARD}"
dpkg-deb -x ./${A}/output/debs/linux-dtb-* ${D}
dpkg-deb -x ./${A}/output/debs/linux-image-* ${D}
dpkg-deb -x ./${A}/output/debs/linux-u-boot-* ${D}
dpkg-deb -x ./${A}/output/debs/armbian-firmware_* ${D}

echo "Copy U-Boot"
if [ "$FAMILY" = "rockchip64" ]; then
  cp ./${D}/usr/lib/linux-u-boot-${BRANCH}-*/*.* ./${D}/u-boot
else
  cp ./${D}/usr/lib/linux-u-boot-${BRANCH}-*/u-boot-sunxi-with-spl.bin ./${D}/u-boot
fi

rm -rf ./${D}/usr ./${D}/etc
mv ./${D}/boot/dtb* ./${D}/boot/dtb

case $FAMILY in
'sunxi64' | 'rockchip64')
  mv ./${D}/boot/vmlinuz* ./${D}/boot/Image
  ;;
*)
  mv ./${D}/boot/vmlinuz* ./${D}/boot/zImage
  ;;
esac

echo "Copy overlays for ${BOARD}"
OVERLAYS_DIR=./${D}/boot/overlay-user
mkdir ${OVERLAYS_DIR}
cp ${C}/sources/overlays/${FAMILY}/${SOC}/*.dts ${OVERLAYS_DIR}

echo "Compile overlays for ${BOARD}"
for dts in "${OVERLAYS_DIR}"/*dts; do
  dts_file=${dts%.*}
  if [ -s "${dts_file}.dts" ]
  then
    echo "Compiling ${dts_file}"
    dtc -O dtb -o "${dts_file}.dtbo" "${dts_file}.dts"
#    cp "${dts_file}.dtbo" "${P}"/boot/overlay-user
  fi
done

cp ./${A}/config/bootscripts/${BOOT_SCRIPT} ./${D}/boot/boot.cmd

mkimage -c none -A arm -T script -d ./${D}/boot/boot.cmd ./${D}/boot/boot.scr
touch ./${D}/boot/.next

echo "Create armbianEnv.txt"
cp ${C}/sources/bootparams/${BOARD}/armbianEnv.txt ./${D}/boot/

# experimental plugin install area
echo "Create folder for Volumio plugins"
mkdir $D/volumio
mkdir $D/volumio/volumio-plugin
echo "Copy prebuilt Yandex Music Plugin"
tar -xf ${C}/volumio/volumio-plugin/yandex_music.tar.xz -C $D/volumio/volumio-plugin

if [ "$SOC" = "rk3308" ]; then
  echo "Create folder for ALSA plugin"
  mkdir $D/volumio/s2mono
  echo "Copy ALSA and Volumio plugins"
  cp -r ${C}/volumio/s2mono/* $D/volumio/s2mono
  tar -xf ${C}/volumio/volumio-plugin/s2mono.tar.xz -C $D/volumio/volumio-plugin
fi
# end of experimental plugin install area

echo "Create $D.tar.xz"
rm $D.tar.xz
tar cJf $D.tar.xz $D
