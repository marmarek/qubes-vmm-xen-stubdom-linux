Linux-based Xen stubdomain for hosting QEMU
===========================================

Building
--------

1. Download auxiliary sources:

    make get-sources

2. Install build dependencies (package names for Fedora, may differ for others):

   - python
   - zlib-devel
   - xen-devel
   - xen-devel
   - glib2-devel
   - autoconf
   - automake
   - edk2-tools
   - libtool
   - libseccomp-devel >= 2.3.0
   - pixman-devel
   - xen-devel

   - bc
   - gcc-plugin-devel

   - xen-runtime
   - busybox
   - dracut

2. Build the thing:

    make -f Makefile.stubdom

The stubdomain consists of two files:

  - kernel: build/linux/arch/x86/boot/bzImage
  - ramdisk: build/rootfs/stubdom-linux-rootfs

Hacking
-------

The most interesting things are in rootfs dir:
  - gen - script to generate ramdisk, you can add/remove extra binaries here
  - init - wrapper script calling qemu and handling interaction with it
