/**
 * @file IxPerfProfSymbols.c
 *
 * @author Intel Corporation
 * @date 30-May-2003
 *
 * @brief This file declares exported symbols for linux kernel module builds.
 *
 * 
 * @par
 * IXP400 SW Release version 2.1
 * 
 * -- Copyright Notice --
 * 
 * @par
 * Copyright (c) 2001-2005, Intel Corporation.
 * All rights reserved.
 * 
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * 
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * 
 * @par
 * -- End of Copyright Notice --
 */

#ifdef __linux

#include <linux/module.h>
#include <IxPerfProfAcc.h>

EXPORT_SYMBOL(ixPerfProfAccXscalePmuEventCountStart);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuEventCountStop);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuTimeSampStart);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuTimeSampStop);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuEventSampStart);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuEventSampStop);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuResultsGet);
EXPORT_SYMBOL(ixPerfProfAccBusPmuStart);
EXPORT_SYMBOL(ixPerfProfAccBusPmuStop);
EXPORT_SYMBOL(ixPerfProfAccBusPmuResultsGet);
EXPORT_SYMBOL(ixPerfProfAccBusPmuPMSRGet);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuTimeSampCreateProcFile);
EXPORT_SYMBOL(ixPerfProfAccXscalePmuEventSampCreateProcFile);

#endif /* __linux */
