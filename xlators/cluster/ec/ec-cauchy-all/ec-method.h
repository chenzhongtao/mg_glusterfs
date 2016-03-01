/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_METHOD_H__
#define __EC_METHOD_H__

#include "iobuf.h"

#define EC_GF_BITS 8

#define EC_METHOD_MAX_FRAGMENTS 16

#define EC_METHOD_WORD_SIZE 8

#define EC_METHOD_CHUNK_SIZE 512

#define EC_METHOD_PACKAGE_SIZE  sizeof(long)

size_t ec_method_encode(uint32_t k, uint32_t m, int32_t **smart,
                        uint8_t * in, struct iobuf ** code_bufs, size_t size);
                        
size_t ec_method_decode(size_t size, uint32_t k, int32_t *matrix, uint32_t * rows,
                        uint8_t ** in, uint8_t * out);
#endif /* __EC_METHOD_H__ */
