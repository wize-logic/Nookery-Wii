/* fixNES's core (pc/lib/fixnes: the CPU, the PPU, the APU, the mapper and
 * memory glue) as one translation unit with the loop that runs a frame of
 * it. The loop calls each part once per CPU cycle, about 30,000 times a
 * frame; built apart (as on PC) none of it is inlined, and a frame took
 * longer than the Wii's 16.7 ms. pc/src/pc_nes_fixnes.c calls this in its
 * own loop's place. */

#include "cpu.c"
#include "ppu.c"
#include "apu.c"
#include "mapper.c"
#include "mem.c"

/* One NES frame: 1 when the PPU finished it, 0 when the CPU halted */
__attribute__((flatten)) int nk_fixnes_run_frame(void)
{
    for (;;) {
        if (!cpuCycle()) {
            return 0;
        }
        ppuCycle();
        apuCycle();
        mapperCycle();
        if (ppuDrawDone()) {
            return 1;
        }
    }
}
