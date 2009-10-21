/*
 * "Null" SVGA example.
 *
 * This is a trivial SVGA test which just switches video modes and
 * halts the CPU.
 */

#include "intr.h"
#include "svga.h"

int
main(void)
{
   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   SVGA_SetMode(1024, 768, 32);

   return 0;
}
