/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * vmbackdoor.c --
 *
 *      This is a tiny and self-contained client implementation of
 *      VMware's backdoor protocols for VMMouse, logging, and time.
 */

#include "vmbackdoor.h"
#include "backdoor_def.h"
#include "vmmouse_defs.h"
#include "intr.h"

#define BACKDOOR_VARS() \
   uint32 eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0; \

#define BACKDOOR_ASM(op, port) \
   { \
      eax = BDOOR_MAGIC; \
      edx = (edx & 0xFFFF0000) | port; \
      asm volatile (op : "+a" (eax), "+b" (ebx), \
                    "+c" (ecx), "+d" (edx), "+S" (esi), "+D" (edi)); \
   }

#define BACKDOOR_ASM_IN()       BACKDOOR_ASM("in %%dx, %0", BDOOR_PORT)
#define BACKDOOR_ASM_HB_OUT()   BACKDOOR_ASM("cld; rep; outsb", BDOORHB_PORT)
#define BACKDOOR_ASM_HB_IN()    BACKDOOR_ASM("cld; rep; insb", BDOORHB_PORT)


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MouseInit --
 *
 *      Initialize the backdoor VMMouse device. This is the virtualized
 *      mouse device that all modern versions of VMware Tools use.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Puts the mouse in absolute or relative mode.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_MouseInit(Bool absolute)  // IN
{
   BACKDOOR_VARS()

   ebx = VMMOUSE_CMD_READ_ID;
   ecx = BDOOR_CMD_ABSPOINTER_COMMAND;
   BACKDOOR_ASM_IN()

   ebx = 0;
   ecx = BDOOR_CMD_ABSPOINTER_STATUS;
   BACKDOOR_ASM_IN()

   ebx = 1;
   ecx = BDOOR_CMD_ABSPOINTER_DATA;
   BACKDOOR_ASM_IN()

   ebx = absolute ? VMMOUSE_CMD_REQUEST_ABSOLUTE : VMMOUSE_CMD_REQUEST_RELATIVE;
   ecx = BDOOR_CMD_ABSPOINTER_COMMAND;
   BACKDOOR_ASM_IN()
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MouseGetPacket --
 *
 *      Poll for VMMouse packets.
 *
 * Results:
 *      If a packet is available, returns TRUE and copies it to 'packet'.
 *      Returns FALSE if no packet is available.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
VMBackdoor_MouseGetPacket(VMMousePacket *packet)  // OUT
{
   const uint32 wordsToRead = 4;
   BACKDOOR_VARS()

   ebx = 0;
   ecx = BDOOR_CMD_ABSPOINTER_STATUS;
   BACKDOOR_ASM_IN()

   /* Low word of 'status' is the number of DWORDs in the device's FIFO */
   if ((eax & 0x0000ffff) < wordsToRead) {
      return FALSE;
   }

   ebx = wordsToRead;
   ecx = BDOOR_CMD_ABSPOINTER_DATA;
   BACKDOOR_ASM_IN()

   packet->x = (int32)ebx;
   packet->y = (int32)ecx;
   packet->z = (int32)edx;
   packet->flags = eax >> 16;
   packet->buttons = eax & 0xFFFF;

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_GetTime --
 *
 *      Read the host's real-time clock, with microsecond precision.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_GetTime(VMTime *time)  // OUT
{
   BACKDOOR_VARS()

   ebx = 0;
   ecx = BDOOR_CMD_GETTIMEFULL;
   BACKDOOR_ASM_IN()

   time->usecs = ebx;
   time->maxTimeLag = ecx;
   time->secsLow = edx;
   time->secsHigh = esi;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_TimeDiffUS --
 *
 *      Compute the differene, in microseconds, of two time values in VMTime format.
 *
 * Results:
 *      Number of microseconds.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int32
VMBackdoor_TimeDiffUS(VMTime *first,   // IN
                      VMTime *second)  // IN
{
   int32 secs = second->secsLow - first->secsLow;
   int32 usec = second->usecs - first->usecs;

   return (secs * 1000000) + usec;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MsgOpen --
 *
 *      Open a backdoor message channel.
 *
 * Results:
 *      Initializes 'channel'.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_MsgOpen(VMMessageChannel *channel,  // OUT
                   uint32 proto)               // IN
{
   BACKDOOR_VARS()

   ecx = BDOOR_CMD_MESSAGE | 0x00000000;  /* Open */
   ebx = proto;
   BACKDOOR_ASM_IN()

   if ((ecx & 0x00010000) == 0) {
      Intr_Break();
   }

   channel->proto = proto;
   channel->id = edx >> 16;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MsgClose --
 *
 *      Close a backdoor message channel.
 *
 * Results:
 *      void.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_MsgClose(VMMessageChannel *channel)  // OUT
{
   BACKDOOR_VARS()

   ecx = BDOOR_CMD_MESSAGE | 0x00060000;  /* Close */
   ebx = 0;
   edx = channel->id << 16;
   BACKDOOR_ASM_IN()
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MsgSend --
 *
 *      Send a message over a VMMessageChannel.
 *
 * Results:
 *      void.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_MsgSend(VMMessageChannel *channel,  // IN
                   const void *buf,            // IN
                   uint32 size)                // IN
{
   BACKDOOR_VARS()

   ecx = BDOOR_CMD_MESSAGE | 0x00010000;  /* Send size */
   ebx = size;
   edx = channel->id << 16;
   BACKDOOR_ASM_IN()

   /* We only support the high-bandwidth backdoor port. */
   if (((ecx >> 16) & 0x0081) != 0x0081) {
      Intr_Break();
   }

   ebx = 0x00010000 | BDOORHB_CMD_MESSAGE;
   ecx = size;
   edx = channel->id << 16;
   esi = (uint32)buf;
   BACKDOOR_ASM_HB_OUT()

   /* Success? */
   if (!(ebx & 0x00010000)) {
      Intr_Break();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_MsgReceive --
 *
 *      Receive a message waiting on a VMMessageChannel.
 *
 * Results:
 *      Returns the number of bytes received.
 *
 * Side effects:
 *      Intr_Break on protocol error or buffer overflow.
 *
 *-----------------------------------------------------------------------------
 */

uint32
VMBackdoor_MsgReceive(VMMessageChannel *channel,  // IN
                      void *buf,                  // IN
                      uint32 bufSize)             // IN
{
   uint32 size;
   BACKDOOR_VARS()

   ecx = BDOOR_CMD_MESSAGE | 0x00030000;  /* Receive size */
   edx = channel->id << 16;
   BACKDOOR_ASM_IN()

   /*
    * Check for success, and make sure a message is waiting.
    * The host must have just sent us a SENDSIZE request.
    * Also make sure the host supports high-bandwidth transfers.
    */
   if (((ecx >> 16) & 0x0083) != 0x0083 ||
       (edx >> 16) != 0x0001) {
      Intr_Break();
   }

   size = ebx;
   if (size > bufSize) {
      Intr_Break();
   }

   /* Receive payload */
   ebx = BDOORHB_CMD_MESSAGE | 0x00010000;
   ecx = size;
   edx = channel->id << 16;
   edi = (uint32)buf;
   BACKDOOR_ASM_HB_IN()

   /* Success? */
   if (!(ebx & 0x00010000)) {
      Intr_Break();
   }

   /* Acknowledge status */
   ecx = BDOOR_CMD_MESSAGE | 0x00050000;
   ebx = 0x0001;
   edx = channel->id << 16;
   BACKDOOR_ASM_IN()

   return size;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_GetRPCIChannel --
 *
 *      Return the channel to use for RPCI messages.
 *
 * Results:
 *      Always returns a VMMessageChannel.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *
 *-----------------------------------------------------------------------------
 */

VMMessageChannel *
VMBackdoor_GetRPCIChannel(void)
{
   static VMMessageChannel channel;
   static Bool initialized;

   if (!initialized) {
      VMBackdoor_MsgOpen(&channel, 0x49435052);  /* 'RPCI' */
      initialized = TRUE;
   }

   return &channel;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_RPCI --
 *
 *      Synchronously deliver an RPCI message and collect its response.
 *
 * Results:
 *      Returns the number of response bytes.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *
 *-----------------------------------------------------------------------------
 */

uint32
VMBackdoor_RPCI(const void *request,    // IN
                uint32 reqSize,         // IN
                void *replyBuffer,      // OUT
                uint32 replyBufferLen)  // IN
{
   VMMessageChannel *channel = VMBackdoor_GetRPCIChannel();
   VMBackdoor_MsgSend(channel, request, reqSize);
   return VMBackdoor_MsgReceive(channel, replyBuffer, replyBufferLen);
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_RPCIOut --
 *
 *      Synchronously deliver an RPCI message, and expect a status
 *      response ("1" on success).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *      Intr_Break on error.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_RPCIOut(const void *request,    // IN
                   uint32 reqSize)         // IN
{
   uint8 replyBuf[16];
   uint32 replyLen = VMBackdoor_RPCI(request, reqSize, replyBuf, sizeof replyBuf);
   if (replyLen < 1 || replyBuf[0] != '1') {
      Intr_Break();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_VGAScreenshot --
 *
 *      Log a screenshot of the VGA framebuffer over the backdoor.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *      Intr_Break on error.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_VGAScreenshot(void)
{
   int x, y;
   const uint8 *fb = (void*)0xB8000;
   static const char prefix[] = "log VGA: [00] ";
   char lineBuf[81 + sizeof prefix];
   char *linePtr;
   uint32 lineLen;

   memcpy(lineBuf, prefix, sizeof prefix);

   for (y = 0; y < 25; y++) {
      linePtr = lineBuf + sizeof prefix - 1;
      lineLen = 0;

      lineBuf[10] = '0' + y / 10;
      lineBuf[11] = '0' + y % 10;

      for (x = 0; x < 80; x++) {
         *linePtr = *fb;
         linePtr++;
         if (*fb != ' ') {
            lineLen = linePtr - lineBuf;
         }
         fb += 2;
      }

      if (lineLen > 0) {
         VMBackdoor_RPCIOut(lineBuf, lineLen);
      }
   }
}
