/****************************************************************************
 * drivers/segger/syslog_rtt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/segger/rtt.h>

#include <SEGGER_RTT.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int syslog_rtt_putc(FAR syslog_channel_t *channel, int ch)
{
  SEGGER_RTT_BLOCK_IF_FIFO_FULL(CONFIG_SYSLOG_RTT_CHANNEL);
  SEGGER_RTT_PutChar(CONFIG_SYSLOG_RTT_CHANNEL, ch);
  return ch;
}

ssize_t syslog_rtt_write(FAR syslog_channel_t *channel,
                         FAR const char *buffer, size_t buflen)
{
  SEGGER_RTT_BLOCK_IF_FIFO_FULL(CONFIG_SYSLOG_RTT_CHANNEL);
  return SEGGER_RTT_Write(CONFIG_SYSLOG_RTT_CHANNEL, buffer, buflen);
}
