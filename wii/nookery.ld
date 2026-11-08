/* Nookery for the Wii: devkitPPC's rvl.ld, with the game's tables in MEM2.
 *
 * src/data's arrays (textures, palettes, vertices: about 11 MiB) are zeros
 * in the program and filled from the player's disc image at the start
 * (pc/src/pc_assets.c), so they need no room in the DOL: they go to MEM2,
 * which wii/src/ogc/main.c clears. The NES emulator's buffers (fixNES's
 * framebuffer and textures, libnk_fixnes.a) go there too. MEM1 keeps the
 * code, the other data and the game's heaps, as on a GameCube. The section is left out of the DOL
 * (cmake/Wii.cmake: objcopy -R .mem2bss), and libogc's MEM2 arena starts
 * after it. */
PHDRS { mem2 PT_LOAD; }
SECTIONS {
	/* SUBALIGN: every array (each its own section: -fdata-sections) on 32
	 * bytes, as the GPU reads textures and palettes and as the GameCube's
	 * linker left them */
	.mem2bss 0x90002000 (NOLOAD) : SUBALIGN(32) {
		__mem2bss_start = .;
		*libnk_data.a:*(.bss .bss.* COMMON)
		*libnk_fixnes.a:*(.bss .bss.* COMMON)
		. = ALIGN(32);
		__mem2bss_end = .;
	} :mem2
}
__Arena2Lo = (__mem2bss_end + 0x1f) & ~0x1f;
INCLUDE rvl.ld
