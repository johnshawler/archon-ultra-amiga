"""Load addresses of the three LE objects inside the x86 window (x86 linear address == offset in the window).
The low 1 MB is left free for DOS-memory style allocations (real-mode segments) and the VGA aperture at 0xA0000."""
OBJ_BASES = (0x110000, 0x150000, 0x160000)
