/*
 * resolution-set --
 * 
 * A sample for implementing the "Fit Guest" functionality in VMware
 * products. This functionality is actually a collaboration between
 * VMware Tools (in userlevel) and the video driver. So this isn't
 * how you'd normally implement it in a real operating system- but
 * for example and testing purposes, it's handy to show how a minimal
 * version of this functionality could be implemented.
 *
 * For a more comprehensive example of the VMware Tools protocol, see
 * the open-vm-tools project.
 */

#include "intr.h"
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

void
resize(int width, int height)
{
   /*
    * To make it obvious that we're switching modes, alternate colors
    * each time.
    */
   static uint32 color = 0x888888;

   color ^= 0x004000;

   SVGA_SyncToFence(SVGA_InsertFence());

   SVGA_SetMode(width, height, 32);
   memset32(gSVGA.fbMem, color, width * height);
   SVGA_Update(0, 0, width, height);
}

int
main(void)
{
   static VMTCLOState tclo;
   Bool resendCapabilities = FALSE;

   Intr_Init();
   Intr_SetFaultHandlers(SVGA_DefaultFaultHandler);
   SVGA_Init();
   SVGA_SetMode(640, 480, 32);

   /* Use the PIT to set TCLO polling rate. */
   Timer_InitPIT(PIT_HZ / 30);
   Intr_SetMask(0, TRUE);

   sendCapabilities();

   while (1) {
      Intr_Halt();

      if (!VMBackdoor_PollTCLO(&tclo, FALSE)) {

         if (resendCapabilities) {
            resendCapabilities = FALSE;
            sendCapabilities();
         }
         continue;
      }

      if (VMBackdoor_CheckPrefixTCLO(&tclo, "Capabilities_Register")) {
         /* Send the capabilities after we get a chance to send the reply. */
         resendCapabilities = TRUE;
         VMBackdoor_ReplyTCLO(&tclo, TCLO_SUCCESS);

      } else if (VMBackdoor_CheckPrefixTCLO(&tclo, "Resolution_Set")) {
         int width = VMBackdoor_IntParamTCLO(&tclo, 1);
         int height = VMBackdoor_IntParamTCLO(&tclo, 2);

         resize(width, height);

         VMBackdoor_ReplyTCLO(&tclo, TCLO_SUCCESS);

      } else {
         /* Unknown command */
         VMBackdoor_ReplyTCLO(&tclo, TCLO_UNKNOWN_CMD);
      }
   }

   return 0;
}
