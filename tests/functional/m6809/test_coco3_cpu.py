#!/usr/bin/env python3
#
# Functional tests for the Color Computer 3 machine (GIME, virt I/O).
#
# SPDX-License-Identifier: GPL-2.0-or-later

import socket

from qemu_test import QemuSystemTest
from m6809.hexprog import (
    BRA_SELF,
    ROM_BASE,
    build_rom,
    mem_bytes,
    run_bios_case,
    wait_info_registers,
)


class Coco3MachineTest(QemuSystemTest):

    timeout = 30

    def setUp(self):
        super().setUp()
        self.set_machine('coco3')
        self.require_accelerator('tcg')

    def test_flat_ram(self):
        run_bios_case(self, 'flat_ram',
                  # lda $0000; tfr a,dp; ldb $7fff
                  b'\xb6\x00\x00\x1f\x8b\xf6\x7f\xff',
                  {'DP': '5a', 'B': 'a5'},
                  load=((0x0000, 0x5a), (0x7fff, 0xa5)))

    def test_gime_mmu(self):
        with self.subTest('remap_page0'):
            run_bios_case(self,
                'remap_page0',
                # lda #$aa; sta $0000; $ffa0=0; INIT0 MMUEN; ldb $0000
                # lda #$55; sta $0000; $ffa0=$38; lda $0000
                b'\x86\xaa\xb7\x00\x00'
                b'\x86\x00\xb7\xff\xa0'
                b'\x86\x40\xb7\xff\x90'
                b'\xf6\x00\x00'
                b'\x86\x55\xb7\x00\x00'
                b'\x86\x38\xb7\xff\xa0'
                b'\xb6\x00\x00',
                {'A': 'aa', 'B': '00'},
                mem=((0x0000, (0xaa,)),),
            )

        with self.subTest('task_switch'):
            run_bios_case(self,
                'task_switch',
                # $ffa0=0 $ffa8=1; INIT0 MMUEN; lda #$11; sta $0000; INIT1 TR; ldb $0000
                # lda #$22; sta $0000; INIT1=0; lda $0000
                b'\x86\x00\xb7\xff\xa0'
                b'\x86\x01\xb7\xff\xa8'
                b'\x86\x40\xb7\xff\x90'
                b'\x86\x11\xb7\x00\x00'
                b'\x86\x01\xb7\xff\x91'
                b'\xf6\x00\x00'
                b'\x86\x22\xb7\x00\x00'
                b'\x86\x00\xb7\xff\x91'
                b'\xb6\x00\x00',
                {'A': '11', 'B': '00'},
            )

        with self.subTest('fexx_constant'):
            run_bios_case(self,
                'fexx_constant',
                # lda #$a5; sta $fe00; $ffa7=0; INIT0 MMUEN+MC3; ldb $fe00; lda $e000
                b'\x86\xa5\xb7\xfe\x00'
                b'\x86\x00\xb7\xff\xa7'
                b'\x86\x48\xb7\xff\x90'
                b'\xf6\xfe\x00'
                b'\xb6\xe0\x00',
                {'A': '00', 'B': 'a5'},
            )

        # $FC00/$FE00 TLB split under post-increment stores.
        with self.subTest('postinc_fc00_fe00'):
            run_bios_case(self,
                'postinc_fc00_fe00',
                # ldy #$fc00; sta ,y+ ×4; ldy #$fe00; sta ,y+ ×2
                b'\x10\x8e\xfc\x00'
                b'\x86\x11\xa7\xa0\x86\x22\xa7\xa0'
                b'\x86\x33\xa7\xa0\x86\x44\xa7\xa0'
                b'\x10\x8e\xfe\x00'
                b'\x86\xaa\xa7\xa0\x86\xbb\xa7\xa0',
                {'Y': 'fe02'},
                mem=((0xfc00, (0x11, 0x22, 0x33, 0x44)),
                     (0xfe00, (0xaa, 0xbb))),
                accel='tcg',
            )

        # SAM TY unmaps ROM at $8000; copy payload to RAM first.
        sam_ty_body = (
            # $ffa1=$3c; INIT0 MMUEN; sta $ffdf; ldb #$5a; stb $2000; bra *
            b'\x86\x3c\xb7\xff\xa1'
            b'\x86\x40\xb7\xff\x90'
            b'\xb7\xff\xdf'
            b'\xc6\x5a\xf7\x20\x00'
            b'\x20\xfe'
        )
        sam_ty_copy = (
            # ldx #$8013; ldy #0; ldb #len; lda ,x+ / sta ,y+; jmp $0000
            b'\x8e\x80\x13'
            b'\x10\x8e\x00\x00'
            b'\xc6' + bytes((len(sam_ty_body),)) +
            b'\xa6\x80\xa7\xa0\x5a\x26\xf9'
            b'\x7e\x00\x00'
        )
        self.assertEqual(len(sam_ty_copy), 0x13)
        with self.subTest('sam_ty_block3c'):
            run_bios_case(self,
                'sam_ty_block3c',
                sam_ty_copy + sam_ty_body,
                {'B': '5a'},
                mem=((0x2000, (0x5a,)),),
                spin_pc=len(sam_ty_body) - 2,
            )

        with self.subTest('distinct_512k_blocks'):
            run_bios_case(self,
                'distinct_512k_blocks',
                # $ffa0=0 $ffa1=$20; INIT0 MMUEN; sta $0000/$2000; lda $0000; ldb $2000
                b'\x86\x00\xb7\xff\xa0'
                b'\x86\x20\xb7\xff\xa1'
                b'\x86\x40\xb7\xff\x90'
                b'\x86\xa1\xb7\x00\x00'
                b'\x86\xa2\xb7\x20\x00'
                b'\xb6\x00\x00\xf6\x20\x00',
                {'A': 'a1', 'B': 'a2'},
            )

    def test_os9_boottrack_load(self):
        track = bytearray(18 * 256)
        track[0:4] = b'OS\x20\xfe'  # fcc /OS/; bra *
        path = self.scratch_file('boottrack.bin')
        with open(path, 'wb') as stream:
            stream.write(track)

        vm = self.get_vm(name='boottrack')
        vm.add_args('-kernel', path,
                    '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                wait_info_registers(vm, 'PC=2602', timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'boottrack: timed out\n{err}')
            dump = vm.cmd('human-monitor-command',
                          command_line='x/4xb 0x2600')
            self.assertEqual([0x4f, 0x53, 0x20, 0xfe], mem_bytes(dump),
                             f'boottrack: memory at 0x2600\n{dump}')
        finally:
            vm.shutdown()

    def test_virt_disk(self):
        with self.subTest('vdisk_probe'):
            run_bios_case(self,
                'vdisk_probe',
                # lda $ff30/$ff31; sta $2000; lda #1; sta $ff32; lda $ff33
                b'\xb6\xff\x30\xb7\x20\x00'
                b'\xb6\xff\x31\xb7\x20\x01'
                b'\x86\x01\xb7\xff\x32\xb6\xff\x33',
                {'A': 'f6'},
                mem=((0x2000, (0x51, 0x01)),),
            )

        image = bytearray(512)
        image[0:2] = b'OS'
        image[255] = 0xee
        image_path = self.scratch_file('vdisk.img')
        with open(image_path, 'wb') as stream:
            stream.write(image)
        drive = ('-drive', f'if=none,file={image_path},format=raw')

        with self.subTest('vdisk_read'):
            run_bios_case(self,
                'vdisk_read',
                # clr Q.DRV/LSN; ldy #$2000; sty Q.BUF; lda #1; sta Q.CMD; lda Q.STAT
                b'\x7f\xff\x34\x7f\xff\x35'
                b'\xcc\x00\x00\xfd\xff\x36'
                b'\x10\x8e\x20\x00\x10\xbf\xff\x38'
                b'\x86\x01\xb7\xff\x32\xb6\xff\x33',
                {'A': '00'},
                extra=drive,
                mem=((0x2000, (0x4f, 0x53)), (0x20ff, (0xee,))),
            )

        with self.subTest('vdisk_unit'):
            run_bios_case(self,
                'vdisk_unit',
                # lda #1; sta Q.DRV; LSN=0; sty Q.BUF #$2000; Q.CMD read; lda Q.STAT
                b'\x86\x01\xb7\xff\x34'
                b'\x7f\xff\x35'
                b'\xcc\x00\x00\xfd\xff\x36'
                b'\x10\x8e\x20\x00\x10\xbf\xff\x38'
                b'\x86\x01\xb7\xff\x32\xb6\xff\x33',
                {'A': 'f1'},
                extra=drive,
            )

        with self.subTest('vdisk_write'):
            run_bios_case(self,
                'vdisk_write',
                # ldx #$2000; lda #$a5; sta ,x+ until $2100; write LSN 1
                # sta Q.STAT $1f00; clr $2000..; read LSN 1
                b'\x8e\x20\x00\x86\xa5\xa7\x80'
                b'\x8c\x21\x00\x26\xf9'
                b'\x7f\xff\x34\x7f\xff\x35'
                b'\xcc\x00\x01\xfd\xff\x36'
                b'\x10\x8e\x20\x00\x10\xbf\xff\x38'
                b'\x86\x02\xb7\xff\x32\xb6\xff\x33'
                b'\xb7\x1f\x00'
                b'\x8e\x20\x00\x6f\x80'
                b'\x8c\x21\x00\x26\xfa'
                b'\x86\x01\xb7\xff\x32\xb6\xff\x33',
                {'A': '00'},
                extra=drive,
                mem=((0x1f00, (0x00,)), (0x2000, (0xa5, 0xa5)),
                     (0x20ff, (0xa5,))),
            )

    def test_virt_console(self):
        with self.subTest('vcons_probe'):
            run_bios_case(self,
                'vcons_probe',
                # lda $ff10/$ff11; sta $2000; lda $ff13
                b'\xb6\xff\x10\xb7\x20\x00'
                b'\xb6\xff\x11\xb7\x20\x01'
                b'\xb6\xff\x13',
                {'A': '00'},
                mem=((0x2000, (0x51, 0x01)),),
            )

        tx_path = self.scratch_file('vcons.out')
        with self.subTest('vcons_tx'):
            run_bios_case(self,
                'vcons_tx',
                # lda #'X'; sta $ff12
                b'\x86\x58\xb7\xff\x12',
                {'A': '58'},
                extra=('-serial', f'file:{tx_path}'),
            )
            with open(tx_path, 'rb') as stream:
                self.assertEqual(stream.read(), b'X')

        # lda $ff13; bita #1; beq *; lda $ff12; sta $2000; lda $ff13; sta $2001
        program = (
            b'\xb6\xff\x13\x85\x01\x27\xf9'
            b'\xb6\xff\x12\xb7\x20\x00'
            b'\xb6\xff\x13\xb7\x20\x01'
        )
        spin_pc = ROM_BASE + len(program)
        rom_path = self.scratch_file('vcons_rx.rom')
        with open(rom_path, 'wb') as stream:
            stream.write(build_rom(program + BRA_SELF))
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        vm = self.get_vm(name='vcons_rx')
        vm.add_args('-bios', rom_path,
                    '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on',
                    '-chardev', f'socket,id=cons,host=127.0.0.1,port={port}',
                    '-serial', 'chardev:cons')
        vm.launch()
        try:
            listener.settimeout(self.timeout)
            conn, _ = listener.accept()
            try:
                conn.sendall(b'\n')
                try:
                    wait_info_registers(vm, f'PC={spin_pc:04x}',
                                        timeout=self.timeout)
                except TimeoutError as err:
                    self.fail(f'vcons_rx: timed out\n{err}')
            finally:
                conn.close()
            dump = vm.cmd('human-monitor-command',
                          command_line='x/2xb 0x2000')
            self.assertEqual([0x0d, 0x00], mem_bytes(dump),
                             f'vcons_rx: memory at 0x2000\n{dump}')
        finally:
            listener.close()
            vm.shutdown()


if __name__ == '__main__':
    QemuSystemTest.main()
