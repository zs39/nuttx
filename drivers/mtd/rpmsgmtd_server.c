/****************************************************************************
 * drivers/mtd/rpmsgmtd_server.c
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

#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "rpmsgmtd.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsgmtd_server_s
{
  struct rpmsg_endpoint ept;
  FAR struct mtd_dev_s *dev;
  FAR struct inode     *mtdnode;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Functions handle the messages from the client cpu */

static int rpmsgmtd_erase_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv);
static int rpmsgmtd_bread_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv);
static int rpmsgmtd_bwrite_handler(FAR struct rpmsg_endpoint *ept,
                                   FAR void *data, size_t len,
                                   uint32_t src, FAR void *priv);
static int rpmsgmtd_read_handler(FAR struct rpmsg_endpoint *ept,
                                 FAR void *data, size_t len,
                                 uint32_t src, FAR void *priv);
static int rpmsgmtd_write_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv);
static int rpmsgmtd_geometry_handler(FAR struct rpmsg_endpoint *ept,
                                     FAR void *data, size_t len,
                                     uint32_t src, FAR void *priv);
static int rpmsgmtd_ioctl_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv);

/* Functions for creating communication with client cpu */

static bool rpmsgmtd_ns_match(FAR struct rpmsg_device *rdev,
                              FAR void *priv, FAR const char *name,
                              uint32_t dest);
static void rpmsgmtd_ns_bind(FAR struct rpmsg_device *rdev,
                             FAR void *priv, FAR const char *name,
                             uint32_t dest);
static int  rpmsgmtd_ept_cb(FAR struct rpmsg_endpoint *ept,
                            FAR void *data, size_t len, uint32_t src,
                            FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const rpmsg_ept_cb g_rpmsgmtd_handler[] =
{
  [RPMSGMTD_ERASE]    = rpmsgmtd_erase_handler,
  [RPMSGMTD_BREAD]    = rpmsgmtd_bread_handler,
  [RPMSGMTD_BWRITE]   = rpmsgmtd_bwrite_handler,
  [RPMSGMTD_READ]     = rpmsgmtd_read_handler,
  [RPMSGMTD_WRITE]    = rpmsgmtd_write_handler,
  [RPMSGMTD_GEOMETRY] = rpmsgmtd_geometry_handler,
  [RPMSGMTD_IOCTL]    = rpmsgmtd_ioctl_handler,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsgmtd_erase_handler
 ****************************************************************************/

static int rpmsgmtd_erase_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_erase_s *msg = data;

  msg->header.result = MTD_ERASE(server->dev, msg->startblock, msg->nblocks);

  return rpmsg_send(ept, msg, sizeof(*msg));
}

/****************************************************************************
 * Name: rpmsgmtd_bread_handler
 ****************************************************************************/

static int rpmsgmtd_bread_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_bread_s *msg = data;
  FAR struct rpmsgmtd_bread_s *rsp;
  int ret = -ENOENT;
  size_t read = 0;
  size_t nblocks;
  uint32_t space;

  while (read < msg->nblocks)
    {
      rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
      if (rsp == NULL)
        {
          ferr("get tx payload failed or no enough space\n");
          return -ENOMEM;
        }

      DEBUGASSERT(space >= sizeof(*msg) - 1 + msg->blocksize);

      *rsp = *msg;

      nblocks = (space - sizeof(*msg) + 1) / msg->blocksize;
      if (nblocks > msg->nblocks - read)
        {
          nblocks = msg->nblocks - read;
        }

      ret = MTD_BREAD(server->dev, msg->startblock + read, nblocks,
                      rsp->buf);

      rsp->header.result = ret;
      rpmsg_send_nocopy(ept, rsp, (ret < 0 ? 0 : ret * msg->blocksize) +
                        sizeof(*rsp) - 1);
      if (ret < 0)
        {
          rpmsg_release_tx_buffer(ept, rsp);
          ferr("mtd block read failed\n");
          break;
        }

      read += ret;
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsgmtd_bwrite_handler
 ****************************************************************************/

static int rpmsgmtd_bwrite_handler(FAR struct rpmsg_endpoint *ept,
                                   FAR void *data, size_t len,
                                   uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_bwrite_s *msg = data;
  int ret;

  ret = MTD_BWRITE(server->dev, msg->startblock, msg->nblocks, msg->buf);
  if (ret <= 0)
    {
      ferr("mtd block write failed\n");
    }

  /* cookie != 0 indicate the data has been sent complete, so send back
   * the total written blocks.
   */

  if (msg->header.cookie != 0)
    {
      msg->header.result = ret;
      rpmsg_send(ept, msg, sizeof(*msg) - 1);
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsgmtd_read_handler
 ****************************************************************************/

static int rpmsgmtd_read_handler(FAR struct rpmsg_endpoint *ept,
                                 FAR void *data, size_t len,
                                 uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_read_s *msg = data;
  FAR struct rpmsgmtd_read_s *rsp;
  int ret = -ENOENT;
  size_t read = 0;
  uint32_t space;

  while (read < msg->nbytes)
    {
      rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
      if (rsp == NULL)
        {
          ferr("get tx payload failed\n");
          return -ENOMEM;
        }

      *rsp = *msg;

      space -= sizeof(*msg) - 1;
      if (space > msg->nbytes - read)
        {
          space = msg->nbytes - read;
        }

      ret = MTD_READ(server->dev, msg->offset + read, space,
                     (FAR uint8_t *)rsp->buf);

      rsp->header.result = ret;
      rpmsg_send_nocopy(ept, rsp, (ret < 0 ? 0 : ret) + sizeof(*rsp) - 1);
      if (ret < 0)
        {
          rpmsg_release_tx_buffer(ept, rsp);
          break;
        }

      read += ret;
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsgmtd_write_handler
 ****************************************************************************/

static int rpmsgmtd_write_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_write_s *msg = data;
  int ret;

  ret = MTD_WRITE(server->dev, msg->offset, msg->nbytes, msg->buf);
  if (ret <= 0)
    {
      ferr("mtd write failed\n");
    }

  /* cookie != 0 indicate the data has been sent complete, so send back
   * the total written bytes.
   */

  if (msg->header.cookie != 0)
    {
      msg->header.result = ret;
      rpmsg_send(ept, msg, sizeof(*msg) - 1);
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsgmtd_geometry_handler
 ****************************************************************************/

static int rpmsgmtd_geometry_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_geometry_s *msg = data;
  struct mtd_geometry_s geo;

  msg->header.result = MTD_IOCTL(server->dev, MTDIOC_GEOMETRY,
                                 (unsigned long)&geo);

  if (msg->header.result < 0)
    {
      ferr("mtd get geometry result error:  %" PRId32 "\n",
            msg->header.result);
      goto send;
    }

  DEBUGASSERT(strlen(geo.model) <= RPMSGMTD_NAME_MAX);

  msg->blocksize = geo.blocksize;
  msg->erasesize = geo.erasesize;
  msg->neraseblocks = geo.neraseblocks;
  strlcpy(msg->model, geo.model, sizeof(msg->model));

send:
  return rpmsg_send(ept, msg, len);
}

/****************************************************************************
 * Name: rpmsgmtd_ioctl_handler
 ****************************************************************************/

static int rpmsgmtd_ioctl_handler(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len,
                                  uint32_t src, FAR void *priv)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;
  FAR struct rpmsgmtd_ioctl_s *msg = data;

  msg->header.result = MTD_IOCTL(server->dev, msg->request,
                                 msg->arglen > 0 ? (unsigned long)msg->buf :
                                 msg->arg);

  return rpmsg_send(ept, msg, len);
}

/****************************************************************************
 * Name: rpmsgmtd_ns_match
 ****************************************************************************/

static bool rpmsgmtd_ns_match(FAR struct rpmsg_device *rdev,
                              FAR void *priv, FAR const char *name,
                              uint32_t dest)
{
  return !strncmp(name, RPMSGMTD_NAME_PREFIX, RPMSGMTD_NAME_PREFIX_LEN);
}

/****************************************************************************
 * Name: rpmsgmtd_ept_release
 ****************************************************************************/

static void rpmsgmtd_ept_release(FAR struct rpmsg_endpoint *ept)
{
  FAR struct rpmsgmtd_server_s *server = ept->priv;

  close_mtddriver(server->mtdnode);
  kmm_free(server);
}

/****************************************************************************
 * Name: rpmsgmtd_ns_bind
 ****************************************************************************/

static void rpmsgmtd_ns_bind(FAR struct rpmsg_device *rdev,
                             FAR void *priv, FAR const char *name,
                             uint32_t dest)
{
  FAR struct rpmsgmtd_server_s *server;
  FAR struct inode *mtdnode;
  int ret;

  server = kmm_zalloc(sizeof(*server));
  if (server == NULL)
    {
      ferr("mtd server malloced failed\n");
      return;
    }

  ret = find_mtddriver(&name[RPMSGMTD_NAME_PREFIX_LEN], &mtdnode);
  if (ret < 0)
    {
      ferr("mtd device find failed, ret=%d\n", ret);
      goto errout;
    }

  server->ept.priv = server;
  server->ept.release_cb = rpmsgmtd_ept_release;
  server->mtdnode  = mtdnode;
  server->dev      = mtdnode->u.i_mtd;

  ret = rpmsg_create_ept(&server->ept, rdev, name,
                         RPMSG_ADDR_ANY, dest,
                         rpmsgmtd_ept_cb, rpmsg_destroy_ept);
  if (ret < 0)
    {
      ferr("endpoint create failed, ret=%d\n", ret);
      close_mtddriver(mtdnode);
      goto errout;
    }

  return;

errout:
  kmm_free(server);
}

/****************************************************************************
 * Name: rpmsgmtd_ept_cb
 ****************************************************************************/

static int rpmsgmtd_ept_cb(FAR struct rpmsg_endpoint *ept,
                           FAR void *data, size_t len, uint32_t src,
                           FAR void *priv)
{
  FAR struct rpmsgmtd_header_s *header = data;
  uint32_t command = header->command;

  if (command < nitems(g_rpmsgmtd_handler))
    {
      return g_rpmsgmtd_handler[command](ept, data, len, src, priv);
    }

  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsgmtd_server_init
 *
 * Description:
 *   Rpmsg-mtd server initialize function, the server cpu should call
 *   this function.
 *
 * Parameters:
 *   None
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int rpmsgmtd_server_init(void)
{
  return rpmsg_register_callback(NULL,
                                 NULL,
                                 NULL,
                                 rpmsgmtd_ns_match,
                                 rpmsgmtd_ns_bind);
}
