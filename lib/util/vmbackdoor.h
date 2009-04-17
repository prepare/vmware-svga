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
 * vmbackdoor.h --
 *
 *      This is a tiny and self-contained client implementation of
 *      VMware's backdoor protocols for VMMouse, logging, time, and
 *      HGFS.
 */

#ifndef __VMBACKDOOR_H__
#define __VMBACKDOOR_H__

#include "types.h"
#include "vmmouse_defs.h"

typedef struct {
   uint32 flags;
   uint32 buttons;
   int x, y, z;
} VMMousePacket;

typedef struct {
   union {
      uint64 secs;
      struct {
         uint32 secsLow;
         uint32 secsHigh;
      };
   };
   uint32 usecs;
   uint32 maxTimeLag;
} VMTime;

typedef struct {
   uint32 proto;
   uint16 id;
} VMMessageChannel;

typedef struct {
   uint8 command[1024];
   uint32 commandLen;
   uint8  reply[1024];
   uint32 replyLen;
} VMTCLOState;


void VMBackdoor_MouseInit(Bool absolute);
Bool VMBackdoor_MouseGetPacket(VMMousePacket *packet);

void VMBackdoor_GetTime(VMTime *time);
int32 VMBackdoor_TimeDiffUS(VMTime *first, VMTime *second);

void VMBackdoor_MsgOpen(VMMessageChannel *channel, uint32 proto);
void VMBackdoor_MsgClose(VMMessageChannel *channel);
void VMBackdoor_MsgSend(VMMessageChannel *channel, const void *buf, uint32 size);
uint32 VMBackdoor_MsgReceive(VMMessageChannel *channel, void *buf, uint32 bufSize);

VMMessageChannel *VMBackdoor_GetRPCIChannel(void);
VMMessageChannel *VMBackdoor_GetTCLOChannel(void);

uint32 VMBackdoor_RPCI(const void *request, uint32 reqSize,
                       void *replyBuffer, uint32 replyBufferLen);
void VMBackdoor_CheckedRPCI(const void *request, uint32 reqSize);

Bool VMBackdoor_PollTCLO(VMTCLOState *state, Bool verbose);
Bool VMBackdoor_CheckPrefixTCLO(VMTCLOState *state, const char *prefix);
int32 VMBackdoor_IntParamTCLO(VMTCLOState *state, int index);
void VMBackdoor_ReplyTCLO(VMTCLOState *state, const char *reply);

/* Response codes for ReplyTCLO */
#define TCLO_SUCCESS       "OK "
#define TCLO_UNKNOWN_CMD   "ERROR Unknown command"

void VMBackdoor_VGAScreenshot(void);

#define VMBackdoor_Log(s)        VMBackdoor_CheckedRPCI(("log " s), 4 + sizeof(s))
#define VMBackdoor_RPCString(s)  VMBackdoor_CheckedRPCI((s), sizeof(s))

#endif /* __VMBACKDOOR_H__ */
