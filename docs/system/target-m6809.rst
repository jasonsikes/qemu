.. _M6809-System-emulator:

M6809 System emulator
---------------------

Use the executable ``qemu-system-m6809`` to emulate a Motorola 6809, Turbo9, or
Hitachi 6809 CPU. The only board is ``coco3`` (Tandy/Radio Shack Color Computer 3).

The ``coco3`` machine includes:

- Motorola 6809 CPU
- 512 KiB RAM (fixed) and GIME MMU (eight 8 KiB pages)
- Two MC6821 PIAs at ``$FF00`` and ``$FF20``
- Keyboard matrix on PIA0 (US-QWERTY glyphs: A is A, Shift-2 is @,
  Break is Esc, Clear is F12; each press is held for two 60 Hz frames)
- Analog joysticks: 6-bit DAC on PIA1 ``$FF20`` bits 2–7, mux on PIA0
  CA2/CB2, comparator on PIA0 PA7. Fire buttons on PA0–PA3. The host
  pointer is an absolute tablet on the right stick (left/right buttons
  are the right-stick fire buttons). The left stick stays centered.
- Virt console at ``$FF10`` (first ``-serial``; NitrOS-9 ``/T0``)
- Virt disk at ``$FF30`` (first ``-drive``)
- Virt RTC at ``$FF50`` (host time; ``-rtc``; NitrOS-9 ``Clock2``)
- Graphic console: fixed 640×240 window. GIME graphics (2/4/16 color at
  the documented HRES/CRES pairs, 192/200/225 lines) and 32/40/64/80-column
  text (8×8 glyphs). With ``INIT0.COCO`` (set on ``-bios``
  reset), VDG-compat 32×16 text and PMODE 4 (256×192×2) use SAM F0–F6 /
  V0–V2 and PIA1 ``$FF22``.
- 60 Hz GIME vertical border interrupt

``-kernel`` loads a boot track at ``$2600`` and starts at ``$2602``.
``-bios`` loads a 32 KiB ROM. The two options cannot be used together.

The first ``-drive`` is the virt disk. ``-serial`` is ``/T0``.
The virt RTC follows ``-rtc`` (default ``base=utc``).

.. code-block:: bash

   qemu-system-m6809 -M coco3 \
       -kernel build/os9/boottrack.bin \
       -drive file=build/os9/qemu.raw,format=raw
