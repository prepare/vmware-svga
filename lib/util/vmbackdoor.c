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
#include "console.h"

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
      Console_Panic("VMBackDoor: Failed to open message channel 0x%08x\n",
                    proto);
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

   if (size == 0) {
      return;
   }

   if (((ecx >> 16) & 0x0081) != 0x0081) {
      Console_Panic("VMBackdoor: Only the high-bandwidth backdoor port is supported.");
   }

   ebx = 0x00010000 | BDOORHB_CMD_MESSAGE;
   ecx = size;
   edx = channel->id << 16;
   esi = (uint32)buf;
   BACKDOOR_ASM_HB_OUT()

   /* Success? */
   if (!(ebx & 0x00010000)) {
      Console_Panic("VMBackdoor: Failed to send %d byte message:\n%s", size, buf);
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
 *      Returns the number of bytes received, or 0 if no
 *      message is available.
 *
 * Side effects:
 *      Console_Panic on protocol error or buffer overflow.
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

   if ((edx >> 16) != 0x0001) {
      Console_Panic("VMBackdoor: Error receiving message size.");
   }

   size = ebx;
   if (size > bufSize) {
      Console_Panic("VMBackdoor: Receive buffer overflow.");
   }
   if (size == 0) {
      return 0;
   }

   if (((ecx >> 16) & 0x0083) != 0x0083) {
      Console_Panic("VMBackdoor: Only the high-bandwidth backdoor port is supported.");
   }

   /* Receive payload */
   ebx = BDOORHB_CMD_MESSAGE | 0x00010000;
   ecx = size;
   edx = channel->id << 16;
   edi = (uint32)buf;
   BACKDOOR_ASM_HB_IN()

   /* Success? */
   if (!(ebx & 0x00010000)) {
      Console_Panic("VMBackdoor: Failed to receive %d byte message.", size);
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
 * VMBackdoor_GetTCLOChannel --
 *
 *      Return the channel to use for TCLO messages.
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
VMBackdoor_GetTCLOChannel(void)
{
   static VMMessageChannel channel;
   static Bool initialized;

   if (!initialized) {
      VMBackdoor_MsgOpen(&channel, 0x4f4c4354);  /* 'TCLO' */
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
 * VMBackdoor_CheckedRPCI --
 *
 *      Synchronously deliver an RPCI message, and expect a status
 *      response ("1" on success).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *      Console_Panic on error.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_CheckedRPCI(const void *request,    // IN
                       uint32 reqSize)         // IN
{
   uint8 replyBuf[16];
   uint32 replyLen = VMBackdoor_RPCI(request, reqSize, replyBuf, sizeof replyBuf);
   if (replyLen < 1 || replyBuf[0] != '1') {
      Console_Panic("VMBackdoor: RPCI response invalid (%s)\n", replyBuf);
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
 *      Console_Panic on error.
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
         VMBackdoor_CheckedRPCI(lineBuf, lineLen);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_PollTCLO --
 *
 *      Poll for incoming commands from the TCLO channel, and flush
 *      the last reply if we had one. We internally handle the 'ping'
 *      and 'reset' commands.
 *
 *      If 'verbose' is set, we print all incoming and outgoing
 *      messages to the console.
 *
 * Results:
 *      Sets replyLen to zero, populates the 'command' buffer and sets
 *      commandLen.
 *
 *      Returns TRUE if there was a command.
 *
 * Side effects:
 *      Opens the channel if necessary.
 *
 *-----------------------------------------------------------------------------
 */

Bool
VMBackdoor_PollTCLO(VMTCLOState *state,  // IN/OUT
                    Bool verbose)        // IN
{
   VMMessageChannel *channel = VMBackdoor_GetTCLOChannel();

   while (1) {

      if (state->replyLen && verbose) {
         Console_Format("[TCLO OUT] '%s'\n", state->reply);
      }

      VMBackdoor_MsgSend(channel, state->reply, state->replyLen);
      state->replyLen = 0;

      if (verbose) {
         memset(state->reply, 0, sizeof state->reply);
         memset(state->command, 0, sizeof state->command);
      }

      state->commandLen = VMBackdoor_MsgReceive(channel, state->command,
                                                sizeof state->command);

      if (state->commandLen == 0) {
         return FALSE;
      }

      if (verbose) {
         Console_Format("[TCLO IN ] '%s'\n", state->command);
      }

      if (VMBackdoor_CheckPrefixTCLO(state, "reset")) {
         /* Send "Answer To Reply" */
         VMBackdoor_ReplyTCLO(state, TCLO_SUCCESS "ATR toolbox");

      } else if (VMBackdoor_CheckPrefixTCLO(state, "ping")) {
         VMBackdoor_ReplyTCLO(state, TCLO_SUCCESS);

      } else {
         return TRUE;
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_CheckPrefixTCLO --
 *
 *      Check for a particular TCLO command, by examining the command prefix.
 *
 * Results:
 *      TRUE if 'prefix' matches, FALSE if not.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
VMBackdoor_CheckPrefixTCLO(VMTCLOState *state,  // IN
                           const char *prefix)  // IN
{
   char *cmd = (char*) state->command;
   char *cmdEnd = (char*) state->command + state->commandLen;

   while (cmd < cmdEnd) {
      if (*prefix == '\0') {
         /* End of prefix. Matched! */
         return TRUE;
      }
      if (*cmd != *prefix) {
         /* Not matched */
         return FALSE;
      }
      cmd++;
      prefix++;
   }

   /*
    * Command length is <= prefix length. We only matched successfully
    * if this is also the end of the prefix.
    */
   return *prefix == '\0';
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_IntParamTCLO --
 *
 *      Parse an integer parameter out of a TCLO command.
 *      "index" is the zero-based index of the command token to start
 *      at. Zero is the command prefix, one is the first space-separated
 *      token after the prefix, etc.
 *
 * Results:
 *      Returns the parsed integer.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int32
VMBackdoor_IntParamTCLO(VMTCLOState *state,  // IN
                        int index)           // IN
{
   char *reply = (char*) state->command;
   char *replyEnd = (char*) state->command + state->commandLen;
   int32 result = 0;
   Bool sign = FALSE;

   while (reply < replyEnd && index > 0) {
      if (*reply == ' ') {
         index--;
      }
      reply++;
   }

   while (reply < replyEnd) {
      char c = *reply;

      if (c == '-') {
         sign = !sign;
      } else if (c >= '0' && c <= '9') {
         result *= 10;
         result += c - '0';
      } else {
         break;
      }

      reply++;
   }

   return sign ? -result : result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBackdoor_ReplyTCLO --
 *
 *      Copy a reply string to the current TCLO state.
 *
 * Results:
 *      Modifies the reply buffer.
 *
 * Side effects:
 *      Panic on buffer overflow.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBackdoor_ReplyTCLO(VMTCLOState *state,  // IN/OUT
                     const char *reply)   // IN
{
   state->replyLen = 0;

   while (*reply) {
      if (state->replyLen >= sizeof state->reply) {
         Console_Panic("VMBackdoor: TCLO reply buffer overflow");
      }

      state->reply[state->replyLen] = *reply;

      state->replyLen++;
      reply++;
   }
}
