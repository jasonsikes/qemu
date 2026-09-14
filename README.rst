=========================================
QEMU for NitrOS-9 and Turbo9/M6809/HD6309
=========================================

*Turbo9* is a 16-bit CPU in development that “balances high performance against
a small silicon area and low power consumption. The instruction set is a proper superset
of the 6809 instruction set. The documentation can be found at
`<https://github.com/turbo9team/turbo9>`_.

The hardware reference platform is the Color Computer 3,
since it is the 6809 system I am most familiar with. I have implemented most of
the hardware for the Color Computer 3 in QEMU. A major exception is
the WD1773 floppy disk controller. Instead, I have implemented a more efficient
QEMU-specific disk controller virtual device.

*NitrOS-9* is a community-based distribution of the Microware OS-9 operating
system for the 6809 CPU. NitrOS-9 can be found at
`<https://github.com/n6il/nitros9>`_.

BUILDING AND RUNNING QEMU FOR NitrOS-9 AND TURBO9
=================================================

To build and run QEMU for Turbo9, you need to
acquire system software for the target machine.

First, you need to download and build my fork of QEMU.

.. code-block:: shell

  $ git clone https://github.com/jasonsikes/qemu.git
  $ cd qemu
  $ ./configure --target-list=m6809-softmmu
  $ ninja -C build qemu-system-m6809

Second, you will need to download either the NitrOS-9 operating system (highly recommended)
or the Color Computer 3 system software ROMs (much less recommended).

BUILDING NitrOS-9 AND RUNNING IT IN QEMU
========================================

To build NitrOS-9 for qemu-system-m6809, you need to use my fork of NitrOS-9 which
has the drivers for QEMU's disk, clock, mouse, and serial terminal.

.. code-block:: shell

  $ cd .. # Leave QEMU directory
  $ git clone https://github.com/jasonsikes/nitros9.git
  $ cd nitros9
  $ export NITROS9DIR="$PWD"
  $ less README.md    # Particularly the section on "Building" which explains how to install LWTOOLS and ToolShed.
  $ cd recipes/coco3/floppy_qemu
  $ make

This will leave you with a bootable floppy disk image, `l2_coco3_qemu.dsk` in the
`$NITROS9DIR/recipes/coco3/floppy_qemu` directory.

Now we can use it to boot our virtual machine.

.. code-block:: shell

  $ cd ../qemu # Back to the QEMU directory, wherever that may be.
  $ build/qemu-system-m6809 -M coco3 \
    -drive "file=$NITROS9DIR/recipes/coco3/floppy_qemu/l2_coco3_qemu.dsk,format=raw" \
    -serial stdio

You should see a QEMU window where NitrOS-9 boots up and presents a shell prompt. You can exit
QEMU by typing Ctrl-C in the terminal where you started QEMU.

You can also spawn a shell prompt in the terminal where you started QEMU by typing in the
graphical CoCo3 guest window:

.. code-block:: shell

  shell i=/t0&

Again, be aware that typing Ctrl-C in the terminal where you started QEMU will exit QEMU.

Bonus command-line options:

* -drive:  use this to add a hard drive image to the virtual machine. The first "-drive" argument is the floppy
image (/D0), the second "-drive" argument is the hard drive image (/H0). Remember that the floppy image must
be bootable.
* -cpu:  use this to specify the CPU type. The default is "m6809". You can also specify "hd6309" or "turbo9".

Finally, the "Clear" key is F12.

RUNNING COCO3 ROM IN QEMU
=========================

To run the Color Computer 3 system software ROM in QEMU, you will need to download
the Color Computer 3 system software ROM images. You can find them at
`<https://colorcomputerarchive.com/repo/ROMs/MAME-MESS/coco3.zip>`_.

Unzip the file and you will see the following file:

* `coco3.rom`     # Color BASIC and Extended Color BASIC ROM

.. code-block:: shell

  $ build/qemu-system-m6809 -M coco3 \
   -bios /path/to/coco3.rom


Special Thanks
==============

A great big THANK-YOU to Florian Göhler for his write-up on `How to add a new architecture to QEMU <https://fgoehler.com/blog/adding-a-new-architecture-to-qemu-01/>`_.
I don't know how I would have done this without his documentation.

That's it for the M6809-specific README portion. Now for the QEMU README.

===========
QEMU README
===========

QEMU is a generic and open source machine & userspace emulator and
virtualizer.

QEMU is capable of emulating a complete machine in software without any
need for hardware virtualization support. By using dynamic translation,
it achieves very good performance. QEMU can also integrate with the Xen
and KVM hypervisors to provide emulated hardware while allowing the
hypervisor to manage the CPU. With hypervisor support, QEMU can achieve
near native performance for CPUs. When QEMU emulates CPUs directly it is
capable of running operating systems made for one machine (e.g. an ARMv7
board) on a different machine (e.g. an x86_64 PC board).

QEMU is also capable of providing userspace API virtualization for Linux
and BSD kernel interfaces. This allows binaries compiled against one
architecture ABI (e.g. the Linux PPC64 ABI) to be run on a host using a
different architecture ABI (e.g. the Linux x86_64 ABI). This does not
involve any hardware emulation, simply CPU and syscall emulation.

QEMU aims to fit into a variety of use cases. It can be invoked directly
by users wishing to have full control over its behaviour and settings.
It also aims to facilitate integration into higher level management
layers, by providing a stable command line interface and monitor API.
It is commonly invoked indirectly via the libvirt library when using
open source applications such as oVirt, OpenStack and virt-manager.

QEMU as a whole is released under the GNU General Public License,
version 2. For full licensing details, consult the LICENSE file.


Documentation
=============

Documentation can be found hosted online at
`<https://www.qemu.org/documentation/>`_. The documentation for the
current development version that is available at
`<https://www.qemu.org/docs/master/>`_ is generated from the ``docs/``
folder in the source tree, and is built by `Sphinx
<https://www.sphinx-doc.org/en/master/>`_.


Building
========

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. The simple steps to build QEMU are:


.. code-block:: shell

  mkdir build
  cd build
  ../configure
  make

Additional information can also be found online via the QEMU website:

* `<https://wiki.qemu.org/Hosts/Linux>`_
* `<https://wiki.qemu.org/Hosts/Mac>`_
* `<https://wiki.qemu.org/Hosts/W32>`_


Submitting patches
==================

The QEMU source code is maintained under the GIT version control system.

.. code-block:: shell

   git clone https://gitlab.com/qemu-project/qemu.git

When submitting patches, one common approach is to use 'git
format-patch' and/or 'git send-email' to format & send the mail to the
qemu-devel@nongnu.org mailing list. All patches submitted must contain
a 'Signed-off-by' line from the author. Patches should follow the
guidelines set out in the `style section
<https://www.qemu.org/docs/master/devel/style.html>`_ of
the Developers Guide.

Additional information on submitting patches can be found online via
the QEMU website:

* `<https://wiki.qemu.org/Contribute/SubmitAPatch>`_
* `<https://wiki.qemu.org/Contribute/TrivialPatches>`_

The QEMU website is also maintained under source control.

.. code-block:: shell

  git clone https://gitlab.com/qemu-project/qemu-web.git

* `<https://www.qemu.org/2017/02/04/the-new-qemu-website-is-up/>`_

A 'git-publish' utility was created to make above process less
cumbersome, and is highly recommended for making regular contributions,
or even just for sending consecutive patch series revisions. It also
requires a working 'git send-email' setup, and by default doesn't
automate everything, so you may want to go through the above steps
manually for once.

For installation instructions, please go to:

*  `<https://github.com/stefanha/git-publish>`_

The workflow with 'git-publish' is:

.. code-block:: shell

  $ git checkout master -b my-feature
  $ # work on new commits, add your 'Signed-off-by' lines to each
  $ git publish

Your patch series will be sent and tagged as my-feature-v1 if you need to refer
back to it in the future.

Sending v2:

.. code-block:: shell

  $ git checkout my-feature # same topic branch
  $ # making changes to the commits (using 'git rebase', for example)
  $ git publish

Your patch series will be sent with 'v2' tag in the subject and the git tip
will be tagged as my-feature-v2.

Bug reporting
=============

The QEMU project uses GitLab issues to track bugs. Bugs
found when running code built from QEMU git or upstream released sources
should be reported via:

* `<https://gitlab.com/qemu-project/qemu/-/issues>`_

If using QEMU via an operating system vendor pre-built binary package, it
is preferable to report bugs to the vendor's own bug tracker first. If
the bug is also known to affect latest upstream code, it can also be
reported via GitLab.

For additional information on bug reporting consult:

* `<https://wiki.qemu.org/Contribute/ReportABug>`_


ChangeLog
=========

For version history and release notes, please visit
`<https://wiki.qemu.org/ChangeLog/>`_ or look at the git history for
more detailed information.


Contact
=======

The QEMU community can be contacted in a number of ways, with the two
main methods being email and IRC:

* `<mailto:qemu-devel@nongnu.org>`_
* `<https://lists.nongnu.org/mailman/listinfo/qemu-devel>`_
* #qemu on irc.oftc.net

Information on additional methods of contacting the community can be
found online via the QEMU website:

* `<https://wiki.qemu.org/Contribute/StartHere>`_
