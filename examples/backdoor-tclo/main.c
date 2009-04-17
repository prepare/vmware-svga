/*
 * Example for receiving TCLO commands, polled from the host.
 * These are used for asynchronous host-to-guest messages,
 * like resolution changes and clipboard synchronization.
 * 
 * (These commands are actually part of VMware Tools, not the
 * SVGA device. They're documented and implemented far better
 * in the open-vm-tools project. However, we include a very tiny
 * implemnentation here in order to test features which involve
 * collaboration between Tools and SVGA, like Unity or Fit-Guest.)
 *
 * This sample just writes all incoming and outgoing messages to the
 * console.
 */

#include "intr.h"
#include "console_vga.h"
#include "vmbackdoor.h"
#include "timer.h"
#include "svga.h"

void
sendCapabilities(void)
{
   VMBackdoor_RPCString("tools.capability.resolution_set 1");
   VMBackdoor_RPCString("tools.capability.resolution_server toolbox 1");
   VMBackdoor_RPCString("tools.capability.display_topology_set 1");
   VMBackdoor_RPCString("tools.capability.color_depth_set 1");
   VMBackdoor_RPCString("tools.capability.resolution_min 0 0");
   VMBackdoor_RPCString("tools.capability.unity 1");
}

int
main(void)
{
   static VMTCLOState tclo;

   Intr_Init();
   Intr_SetFaultHandlers(Console_UnhandledFault);
   ConsoleVGA_Init();

   /* Use the PIT to set TCLO polling rate. */
   Timer_InitPIT(PIT_HZ / 30);
   Intr_SetMask(0, TRUE);

   sendCapabilities();

   while (1) {
      Intr_Halt();

      if (VMBackdoor_PollTCLO(&tclo, TRUE)) {
         VMBackdoor_ReplyTCLO(&tclo, TCLO_UNKNOWN_CMD);
      }
   }

   return 0;
}
