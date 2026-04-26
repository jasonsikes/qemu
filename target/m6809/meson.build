gen = [
  decodetree.process('insn.decode', extra_args: [ '--static-decode', 'decode_insn',
                                                  '--varinsnwidth', '32' ])
]

m6809_ss = ss.source_set()
m6809_common_system_ss = ss.source_set()

m6809_ss.add(gen)
m6809_ss.add(files(
  'cpu.c',
  'helper.c',
  'translate.c',
  'disas.c',
))

m6809_common_system_ss.add(files(
  'machine.c',
))

target_arch += {'m6809': m6809_ss}
target_common_system_arch += {'m6809': m6809_common_system_ss}
