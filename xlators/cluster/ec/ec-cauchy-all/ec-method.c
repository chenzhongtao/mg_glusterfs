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

#include <string.h>
#include <inttypes.h>

#include "ec-gf.h"
#include "ec-method.h"

static uint32_t GfPow[EC_GF_SIZE << 1];//复制多一份数据，可以避免求余运算
static uint32_t GfLog[EC_GF_SIZE << 1];

/*
w=4
i          0  1  2  3  4  5  6  7   8  9   10  11  12  13  14  15 
GfLog[i]   -- 0  1  4  2  5  8  10  3  14  9   7   6   13  11  12
GfPow[i]   1  2  4  8  3  6  12 11  5  10  7   14  15  13  9   --
GfLog[GfPow[i]] = i
*/
/*迦罗瓦域指数表和对数表初始化*/
void ec_method_initialize(void)
{
    uint32_t i;

    GfPow[0] = 1;
    GfLog[0] = EC_GF_SIZE;
    for (i = 1; i < EC_GF_SIZE; i++)
    {
        GfPow[i] = GfPow[i - 1] << 1;
        if (GfPow[i] >= EC_GF_SIZE)
        {
            GfPow[i] ^= EC_GF_MOD;
        }
        GfPow[i + EC_GF_SIZE - 1] = GfPow[i];
        GfLog[GfPow[i] + EC_GF_SIZE - 1] = GfLog[GfPow[i]] = i;
    }
}

static uint32_t ec_method_mul(uint32_t a, uint32_t b)
{
    if (a && b)
    {
        return GfPow[GfLog[a] + GfLog[b]];//不用求余
    }
    return 0;
}

static uint32_t ec_method_div(uint32_t a, uint32_t b)
{
    if (b)
    {
        if (a)
        {
            return GfPow[EC_GF_SIZE - 1 + GfLog[a] - GfLog[b]];//不用求余
        }
        return 0;
    }
    return EC_GF_SIZE;
}

/*4+2纠删码
调试信息:size=32768, columns=4, row=0, in=0x7f3984ee8000 "PK\003\004\024", out=0x7f3984fee000 ""
循环6次，row从0到5
*/
size_t ec_method_encode(size_t size, uint32_t columns, uint32_t row,
                        uint8_t * in, uint8_t * out)
{
    uint32_t i, j;

    size /= EC_METHOD_CHUNK_SIZE * columns;//EC_METHOD_CHUNK_SIZE=64*8,size=16
    row++;
    for (j = 0; j < size; j++)
    {
        ec_gf_muladd[0](out, in, EC_METHOD_WIDTH);//EC_METHOD_WIDTH=64/8
        in += EC_METHOD_CHUNK_SIZE;
        for (i = 1; i < columns; i++)
        {
            ec_gf_muladd[row](out, in, EC_METHOD_WIDTH);
            in += EC_METHOD_CHUNK_SIZE;
        }
        out += EC_METHOD_CHUNK_SIZE;
    }

    return size * EC_METHOD_CHUNK_SIZE;
}

size_t ec_method_decode(size_t size, uint32_t columns, uint32_t * rows,
                        uint8_t ** in, uint8_t * out)
{
    uint32_t i, j, k, off, last, value;
    uint32_t f;
    uint8_t inv[EC_METHOD_MAX_FRAGMENTS][EC_METHOD_MAX_FRAGMENTS + 1];
    uint8_t mtx[EC_METHOD_MAX_FRAGMENTS][EC_METHOD_MAX_FRAGMENTS];
    uint8_t dummy[EC_METHOD_CHUNK_SIZE];

    size /= EC_METHOD_CHUNK_SIZE;

    memset(inv, 0, sizeof(inv));
    memset(mtx, 0, sizeof(mtx));
    memset(dummy, 0, sizeof(dummy));
    for (i = 0; i < columns; i++)
    {
        inv[i][i] = 1;
        inv[i][columns] = 1;
    }
    for (i = 0; i < columns; i++)
    {
        mtx[i][columns - 1] = 1;
        for (j = columns - 1; j > 0; j--)
        {
            mtx[i][j - 1] = ec_method_mul(mtx[i][j], rows[i] + 1);
        }
    }

    for (i = 0; i < columns; i++)
    {
        f = mtx[i][i];
        for (j = 0; j < columns; j++)
        {
            mtx[i][j] = ec_method_div(mtx[i][j], f);
            inv[i][j] = ec_method_div(inv[i][j], f);
        }
        for (j = 0; j < columns; j++)
        {
            if (i != j)
            {
                f = mtx[j][i];
                for (k = 0; k < columns; k++)
                {
                    mtx[j][k] ^= ec_method_mul(mtx[i][k], f);
                    inv[j][k] ^= ec_method_mul(inv[i][k], f);
                }
            }
        }
    }
    off = 0;
    for (f = 0; f < size; f++)
    {
        for (i = 0; i < columns; i++)
        {
            last = 0;
            j = 0;
            do
            {
                while (inv[i][j] == 0)
                {
                    j++;
                }
                if (j < columns)
                {
                    value = ec_method_div(last, inv[i][j]);
                    last = inv[i][j];
                    ec_gf_muladd[value](out, in[j] + off, EC_METHOD_WIDTH);
                    j++;
                }
            } while (j < columns);
            ec_gf_muladd[last](out, dummy, EC_METHOD_WIDTH);
            out += EC_METHOD_CHUNK_SIZE;
        }
        off += EC_METHOD_CHUNK_SIZE;
    }

    return size * EC_METHOD_CHUNK_SIZE * columns;
}
