m6809_ss = ss.source_set()
m6809_ss.add(when: 'CONFIG_MC6821', if_true: files(
  'mc6821.c',
))
m6809_ss.add(when: 'CONFIG_COCO3', if_true: files(
  'boot.c',
  'coco3.c',
  'coco3_video.c',
  'coco3_mouse.c',
  'virt_disk.c',
  'virt_cons.c',
  'virt_rtc.c',
  'm6809_coco3.c',
))

hw_arch += {'m6809': m6809_ss}
