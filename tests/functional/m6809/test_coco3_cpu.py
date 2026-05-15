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

    def test_gime_bitmap(self):
        # 320×192×16 (VMODE BP, VRES LPF=192 HRES=160B CRES=16).
        # MMU off: CPU $0000 is physical $70000, so $FF9D:$FF9E = $E000.
        # -bios reset sets INIT0.COCO; clear it for GIME scanout.
        code = (b'\x86\x00\xb7\xff\x90'
                b'\x86\x00\xb7\xff\xb0'
                b'\x86\x20\xb7\xff\xb1'
                b'\x86\xe0\xb7\xff\x9d'
                b'\x86\x00\xb7\xff\x9e'
                b'\x86\x80\xb7\xff\x98'
                b'\x86\x1e\xb7\xff\x99'
                b'\x86\x01\xb7\x00\x00'
                b'\x86\x10\xb7\x00\xa0'
                b'\xb6\xff\x98\xf6\xff\xb1')
        path = self.scratch_file('gime_bitmap.rom')
        with open(path, 'wb') as stream:
            stream.write(build_rom(code + BRA_SELF))
        dump_path = self.scratch_file('gime_bitmap.ppm')
        vm = self.get_vm(name='gime_bitmap')
        vm.add_args('-bios', path, '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                registers = wait_info_registers(
                    vm, f'PC={ROM_BASE + len(code):04x}',
                    timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'gime_bitmap: timed out\n{err}')
            self.assertIn('A=80', registers)
            self.assertIn('B=20', registers)
            dump = vm.cmd('human-monitor-command',
                          command_line='x/1xb 0x0000')
            self.assertEqual([0x01], mem_bytes(dump), dump)
            dump = vm.cmd('human-monitor-command',
                          command_line='x/1xb 0x00a0')
            self.assertEqual([0x10], mem_bytes(dump), dump)
            vm.cmd('screendump', filename=dump_path)
        finally:
            vm.shutdown()

        with open(dump_path, 'rb') as stream:
            magic = stream.readline()
            self.assertEqual(magic.strip(), b'P6')
            size = stream.readline()
            while size.startswith(b'#'):
                size = stream.readline()
            width, height = (int(v) for v in size.split())
            self.assertEqual((width, height), (640, 240))
            self.assertEqual(stream.readline().strip(), b'255')
            pixels = stream.read()
        self.assertEqual(len(pixels), 640 * 240 * 3)

        def rgb(x, y):
            i = (y * 640 + x) * 3
            return tuple(pixels[i:i + 3])

        # 320×192 is centered in the 640×240 window.
        x0, y0 = (640 - 320) // 2, (240 - 192) // 2
        self.assertEqual(rgb(0, 0), (0x00, 0x00, 0x00))
        self.assertEqual(rgb(x0, y0), (0x00, 0x00, 0x00))
        self.assertEqual(rgb(x0 + 1, y0), (0xaa, 0x00, 0x00))
        self.assertEqual(rgb(x0, y0 + 1), (0xaa, 0x00, 0x00))
        self.assertEqual(rgb(x0 + 1, y0 + 1), (0x00, 0x00, 0x00))

    def test_gime_text(self):
        # 80- and 40-column GIME text, LPR=8, attributes, video at $70000.
        # "OK" plus an underlined space; fg palette 8 is red, bg palette 0 black.
        font_o = (0x1c, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x00)
        font_k = (0x67, 0x66, 0x36, 0x1e, 0x36, 0x66, 0x67, 0x00)
        red, black = (0xaa, 0x00, 0x00), (0x00, 0x00, 0x00)
        setup = (b'\x86\x00\xb7\xff\x90'
                 b'\x86\x00\xb7\xff\xb0'
                 b'\x86\x20\xb7\xff\xb8'
                 b'\x86\xe0\xb7\xff\x9d'
                 b'\x86\x00\xb7\xff\x9e'
                 b'\x86\x03\xb7\xff\x98'
                 b'\x86\x4f\xb7\x00\x00'
                 b'\x86\x00\xb7\x00\x01'
                 b'\x86\x4b\xb7\x00\x02'
                 b'\x86\x00\xb7\x00\x03'
                 b'\x86\x20\xb7\x00\x04'
                 b'\x86\x40\xb7\x00\x05')

        def ppm_rgb(path):
            with open(path, 'rb') as stream:
                magic = stream.readline()
                self.assertEqual(magic.strip(), b'P6')
                size = stream.readline()
                while size.startswith(b'#'):
                    size = stream.readline()
                width, height = (int(v) for v in size.split())
                self.assertEqual((width, height), (640, 240))
                self.assertEqual(stream.readline().strip(), b'255')
                pixels = stream.read()
            self.assertEqual(len(pixels), 640 * 240 * 3)

            def rgb(x, y):
                i = (y * 640 + x) * 3
                return tuple(pixels[i:i + 3])

            return rgb

        def run_text(name, vres, xscale):
            code = setup + bytes((0x86, vres, 0xb7, 0xff, 0x99))
            path = self.scratch_file(f'{name}.rom')
            with open(path, 'wb') as stream:
                stream.write(build_rom(code + BRA_SELF))
            dump_path = self.scratch_file(f'{name}.ppm')
            vm = self.get_vm(name=name)
            vm.add_args('-bios', path, '-display', 'none',
                        '-accel', 'tcg,one-insn-per-tb=on')
            vm.launch()
            try:
                try:
                    wait_info_registers(
                        vm, f'PC={ROM_BASE + len(code):04x}',
                        timeout=self.timeout)
                except TimeoutError as err:
                    self.fail(f'{name}: timed out\n{err}')
                dump = vm.cmd('human-monitor-command',
                              command_line='x/6xb 0x0000')
                self.assertEqual([0x4f, 0x00, 0x4b, 0x00, 0x20, 0x40],
                                 mem_bytes(dump), dump)
                vm.cmd('screendump', filename=dump_path)
            finally:
                vm.shutdown()

            rgb = ppm_rgb(dump_path)
            y0 = (240 - 192) // 2
            self.assertEqual(rgb(0, 0), black)
            cell = 8 * xscale
            for row in range(8):
                for bit in range(8):
                    expect = red if (font_o[row] >> bit) & 1 else black
                    x = bit * xscale
                    self.assertEqual(rgb(x, y0 + row), expect,
                                     f'{name}: O row {row} bit {bit}')
                    expect = red if (font_k[row] >> bit) & 1 else black
                    x = cell + bit * xscale
                    self.assertEqual(rgb(x, y0 + row), expect,
                                     f'{name}: K row {row} bit {bit}')
            # Underlined space: last scan of the cell is foreground.
            for bit in range(8):
                x = 2 * cell + bit * xscale
                self.assertEqual(rgb(x, y0 + 6), black, f'{name}: ul row 6')
                self.assertEqual(rgb(x, y0 + 7), red, f'{name}: ul row 7')

        with self.subTest('col80'):
            run_text('gime_text_80', 0x15, 1)
        with self.subTest('col40'):
            run_text('gime_text_40', 0x05, 2)

    def test_gime_window_colors(self):
        # CoWin text windows pick fg/bg palette registers via attributes.
        # Same RGB palette, different attrs: W1 white bg (reg 0), W2 blue (reg 1).
        white, blue, black = (0xff, 0xff, 0xff), (0x00, 0x00, 0xff), (0x00, 0x00, 0x00)
        code = (b'\x86\x00\xb7\xff\x90'
                b'\x86\x3f\xb7\xff\xb0'
                b'\x86\x09\xb7\xff\xb1'
                b'\x86\xe0\xb7\xff\x9d'
                b'\x86\x00\xb7\xff\x9e'
                b'\x86\x03\xb7\xff\x98'
                b'\x86\x20\xb7\x00\x00'
                b'\x86\x00\xb7\x00\x01'
                b'\x86\x20\xb7\x00\x02'
                b'\x86\x01\xb7\x00\x03'
                b'\x86\x15\xb7\xff\x99')
        path = self.scratch_file('gime_window_colors.rom')
        with open(path, 'wb') as stream:
            stream.write(build_rom(code + BRA_SELF))
        dump_path = self.scratch_file('gime_window_colors.ppm')
        vm = self.get_vm(name='gime_window_colors')
        vm.add_args('-bios', path, '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                wait_info_registers(
                    vm, f'PC={ROM_BASE + len(code):04x}',
                    timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'gime_window_colors: timed out\n{err}')
            vm.cmd('screendump', filename=dump_path)
        finally:
            vm.shutdown()

        with open(dump_path, 'rb') as stream:
            magic = stream.readline()
            self.assertEqual(magic.strip(), b'P6')
            size = stream.readline()
            while size.startswith(b'#'):
                size = stream.readline()
            width, height = (int(v) for v in size.split())
            self.assertEqual((width, height), (640, 240))
            self.assertEqual(stream.readline().strip(), b'255')
            pixels = stream.read()

        def rgb(x, y):
            i = (y * 640 + x) * 3
            return tuple(pixels[i:i + 3])

        y0 = (240 - 192) // 2
        self.assertEqual(rgb(0, 0), black)
        self.assertEqual(rgb(0, y0), white)
        self.assertEqual(rgb(8, y0), blue)

    def test_gime_gfx_200(self):
        # CoWin type 8 on a 25-line screen: 320×200×16 (VRES LPF=200 HRES=160B).
        code = (b'\x86\x00\xb7\xff\x90'
                b'\x86\x00\xb7\xff\xb0'
                b'\x86\x20\xb7\xff\xb1'
                b'\x86\xe0\xb7\xff\x9d'
                b'\x86\x00\xb7\xff\x9e'
                b'\x86\x80\xb7\xff\x98'
                b'\x86\x3e\xb7\xff\x99'
                b'\x86\x01\xb7\x00\x00')
        path = self.scratch_file('gime_gfx_200.rom')
        with open(path, 'wb') as stream:
            stream.write(build_rom(code + BRA_SELF))
        dump_path = self.scratch_file('gime_gfx_200.ppm')
        vm = self.get_vm(name='gime_gfx_200')
        vm.add_args('-bios', path, '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                wait_info_registers(
                    vm, f'PC={ROM_BASE + len(code):04x}',
                    timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'gime_gfx_200: timed out\n{err}')
            vm.cmd('screendump', filename=dump_path)
        finally:
            vm.shutdown()

        with open(dump_path, 'rb') as stream:
            magic = stream.readline()
            self.assertEqual(magic.strip(), b'P6')
            size = stream.readline()
            while size.startswith(b'#'):
                size = stream.readline()
            width, height = (int(v) for v in size.split())
            self.assertEqual((width, height), (640, 240))
            self.assertEqual(stream.readline().strip(), b'255')
            pixels = stream.read()

        def rgb(x, y):
            i = (y * 640 + x) * 3
            return tuple(pixels[i:i + 3])

        x0, y0 = (640 - 320) // 2, (240 - 200) // 2
        self.assertEqual(rgb(0, 0), (0x00, 0x00, 0x00))
        self.assertEqual(rgb(x0, y0 - 1), (0x00, 0x00, 0x00))
        self.assertEqual(rgb(x0, y0), (0x00, 0x00, 0x00))
        self.assertEqual(rgb(x0 + 1, y0), (0xaa, 0x00, 0x00))

    def test_vdg_text(self):
        # INIT0.COCO, SAM V=0, F=2 ($0400), $FF9D=$E0 → CPU $0400.
        # VDG "OK" (codes $0F/$0B); palette 12/13 black/red; 8×12 cells.
        font_o = (0x1c, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1c, 0x00)
        font_k = (0x67, 0x66, 0x36, 0x1e, 0x36, 0x66, 0x67, 0x00)
        red, black = (0xaa, 0x00, 0x00), (0x00, 0x00, 0x00)
        code = (b'\xb6\xff\x90\x1f\x89'
                b'\x86\x00\xb7\xff\xbc'
                b'\x86\x20\xb7\xff\xbd'
                b'\x86\xe0\xb7\xff\x9d'
                b'\xb7\xff\xc9'
                b'\x86\x80\xb7\xff\x90'
                b'\x86\x0f\xb7\x04\x00'
                b'\x86\x0b\xb7\x04\x01')
        path = self.scratch_file('vdg_text.rom')
        with open(path, 'wb') as stream:
            stream.write(build_rom(code + BRA_SELF))
        dump_path = self.scratch_file('vdg_text.ppm')
        vm = self.get_vm(name='vdg_text')
        vm.add_args('-bios', path, '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                registers = wait_info_registers(
                    vm, f'PC={ROM_BASE + len(code):04x}',
                    timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'vdg_text: timed out\n{err}')
            self.assertIn('B=80', registers)
            dump = vm.cmd('human-monitor-command',
                          command_line='x/2xb 0x0400')
            self.assertEqual([0x0f, 0x0b], mem_bytes(dump), dump)
            vm.cmd('screendump', filename=dump_path)
        finally:
            vm.shutdown()

        with open(dump_path, 'rb') as stream:
            self.assertEqual(stream.readline().strip(), b'P6')
            size = stream.readline()
            while size.startswith(b'#'):
                size = stream.readline()
            width, height = (int(v) for v in size.split())
            self.assertEqual((width, height), (640, 240))
            self.assertEqual(stream.readline().strip(), b'255')
            pixels = stream.read()
        self.assertEqual(len(pixels), 640 * 240 * 3)

        def rgb(x, y):
            i = (y * 640 + x) * 3
            return tuple(pixels[i:i + 3])

        x0, y0 = (640 - 512) // 2, (240 - 192) // 2
        self.assertEqual(rgb(0, 0), black)
        for row in range(8):
            for bit in range(8):
                expect = red if (font_o[row] >> bit) & 1 else black
                self.assertEqual(rgb(x0 + bit * 2, y0 + 1 + row), expect,
                                 f'O row {row} bit {bit}')
                expect = red if (font_k[row] >> bit) & 1 else black
                self.assertEqual(rgb(x0 + 16 + bit * 2, y0 + 1 + row), expect,
                                 f'K row {row} bit {bit}')

    def test_pmode4(self):
        # INIT0.COCO, SAM V=6, F=0, $FF9D=$E0, $FF22 PMODE 4.
        # 256×192×2 at CPU $0000; palette 8/9 black/red; pixels doubled.
        red, black = (0xaa, 0x00, 0x00), (0x00, 0x00, 0x00)
        code = (b'\x86\x00\xb7\xff\xb8'
                b'\x86\x20\xb7\xff\xb9'
                b'\x86\xe0\xb7\xff\x9d'
                b'\x86\x80\xb7\xff\x90'
                b'\xb7\xff\xc3'
                b'\xb7\xff\xc5'
                b'\x86\xff\xb7\xff\x22'
                b'\x86\x04\xb7\xff\x23'
                b'\x86\xf0\xb7\xff\x22'
                b'\x86\x80\xb7\x00\x00'
                b'\x86\x40\xb7\x00\x20')
        path = self.scratch_file('pmode4.rom')
        with open(path, 'wb') as stream:
            stream.write(build_rom(code + BRA_SELF))
        dump_path = self.scratch_file('pmode4.ppm')
        vm = self.get_vm(name='pmode4')
        vm.add_args('-bios', path, '-display', 'none',
                    '-accel', 'tcg,one-insn-per-tb=on')
        vm.launch()
        try:
            try:
                wait_info_registers(
                    vm, f'PC={ROM_BASE + len(code):04x}',
                    timeout=self.timeout)
            except TimeoutError as err:
                self.fail(f'pmode4: timed out\n{err}')
            dump = vm.cmd('human-monitor-command',
                          command_line='x/1xb 0x0000')
            self.assertEqual([0x80], mem_bytes(dump), dump)
            dump = vm.cmd('human-monitor-command',
                          command_line='x/1xb 0x0020')
            self.assertEqual([0x40], mem_bytes(dump), dump)
            vm.cmd('screendump', filename=dump_path)
        finally:
            vm.shutdown()

        with open(dump_path, 'rb') as stream:
            self.assertEqual(stream.readline().strip(), b'P6')
            size = stream.readline()
            while size.startswith(b'#'):
                size = stream.readline()
            width, height = (int(v) for v in size.split())
            self.assertEqual((width, height), (640, 240))
            self.assertEqual(stream.readline().strip(), b'255')
            pixels = stream.read()
        self.assertEqual(len(pixels), 640 * 240 * 3)

        def rgb(x, y):
            i = (y * 640 + x) * 3
            return tuple(pixels[i:i + 3])

        x0, y0 = (640 - 512) // 2, (240 - 192) // 2
        self.assertEqual(rgb(0, 0), (0x00, 0xff, 0x00))  # CSS=0 graphics border
        self.assertEqual(rgb(x0, y0), red)
        self.assertEqual(rgb(x0 + 1, y0), red)
        self.assertEqual(rgb(x0 + 2, y0), black)
        self.assertEqual(rgb(x0, y0 + 1), black)
        self.assertEqual(rgb(x0 + 2, y0 + 1), red)


if __name__ == '__main__':
    QemuSystemTest.main()
