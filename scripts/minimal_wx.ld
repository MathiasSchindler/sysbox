/* Minimal W^X linker script for sysbox static ELF binaries.
 * Targets Linux x86_64; produces non-PIE ET_EXEC.
 *
 * Compared to scripts/minimal.ld, this splits RX and RW into separate PT_LOADs.
 * This avoids RWX warnings, at the cost of an extra program header and
 * potentially a little more padding depending on the toolchain.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
	text PT_LOAD FILEHDR PHDRS FLAGS(5);   /* PF_R | PF_X */
	data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
	stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
	. = 0x400000 + SIZEOF_HEADERS;

	.text : {
		*(.text .text.*)
		*(.rodata .rodata.*)
	} :text

	/* IMPORTANT: Start RW segment on a new page to avoid PT_LOAD overlap. */
	. = ALIGN(0x1000);

	/* Keep RW data separate to preserve W^X. */
	.data : {
		*(.data .data.*)
		*(.sdata .sdata.*)
	} :data

	.bss (NOLOAD) : ALIGN(8) {
		*(.bss .bss.*)
		*(COMMON)
	} :data

	/DISCARD/ : {
		*(.note*)
		*(.comment)
		*(.eh_frame*)
		*(.gcc_except_table*)
		*(.gnu*)
	}
}
