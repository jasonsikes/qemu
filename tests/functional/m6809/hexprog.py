# coco3 scratch-ROM and HMP register helpers.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import time

ROM_SIZE = 32 * 1024
ROM_BASE = 0x8000
BRA_SELF = b'\x20\xfe'


def mem_bytes(dump):
    got = []
    for token in dump.split(':', 1)[-1].split():
        if token.startswith('0x'):
            got.append(int(token, 16))
    return got


def loader_args(values):
    args = []
    for address, value in values:
        args.extend(('-device',
                     f'loader,addr={address:#x},data={value:#x},data-len=1'))
    return tuple(args)


def build_rom(code):
    rom = bytearray(ROM_SIZE)
    rom[:len(code)] = code
    rom[-2:] = ROM_BASE.to_bytes(2, 'big')
    return bytes(rom)


def wait_info_registers(vm, *needles, timeout):
    """Poll HMP 'info registers' until every needle appears.

    Returns the register dump. Raises TimeoutError with the last dump.
    """
    registers = ''
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        registers = vm.cmd('human-monitor-command',
                           command_line='info registers')
        if all(needle in registers for needle in needles):
            return registers
        time.sleep(0.01)
    raise TimeoutError(registers)


def assert_regs_mem(test, vm, name, registers, regs, mem=()):
    for register, value in regs.items():
        test.assertIn(f'{register}={value}', registers,
                      f'{name}: unexpected {register}\n{registers}')
    for address, values in mem:
        dump = vm.cmd('human-monitor-command',
                      command_line=f'x/{len(values)}xb {address:#x}')
        test.assertEqual(list(values), mem_bytes(dump),
                         f'{name}: memory at {address:#x}\n{dump}')


def run_bios_case(test, name, code, regs, *,
                  load=(), mem=(), extra=(), accel='tcg,one-insn-per-tb=on',
                  spin_pc=None, timeout=None):
    if spin_pc is None:
        spin_pc = ROM_BASE + len(code)
    if timeout is None:
        timeout = test.timeout
    path = test.scratch_file(f'{name}.rom')
    with open(path, 'wb') as stream:
        stream.write(build_rom(code + BRA_SELF))
    vm = test.get_vm(name=name)
    vm.add_args('-bios', path, '-display', 'none', '-accel', accel,
                *extra, *loader_args(load))
    vm.launch()
    try:
        try:
            registers = wait_info_registers(vm, f'PC={spin_pc:04x}',
                                            timeout=timeout)
        except TimeoutError as err:
            test.fail(f'{name}: timed out\n{err}')
        assert_regs_mem(test, vm, name, registers, regs, mem)
    finally:
        vm.shutdown()
