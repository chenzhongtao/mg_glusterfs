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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ec-method.h"
#include "erasure_code.h"

int ec_make_decoding_matrix(int k, uint8_t *matrix, uint8_t *decoding_matrix, uint32_t *rows)
{

  int i, j;
  uint8_t *tmpmat;
  
  tmpmat = talloc(uint8_t, k*k);
  if (tmpmat == NULL)
  { 
      return -1;
  }

  for (i = 0; i < k; i++) 
  {
      for (j = 0; j < k; j++) 
      {
        tmpmat[i*k+j] = matrix[rows[i]*k+j];
      }
  }
  
  i = gf_invert_matrix(tmpmat, decoding_matrix, k);
  
  free(tmpmat);
  
  return i;
}

size_t ec_method_encode(uint32_t k, uint32_t m, uint8_t *g_tbls, uint32_t idx,
                        uint8_t * in, uint8_t * out, size_t size)
{
    uint32_t i, j;
    uint8_t *data_ptr[k];
    uint8_t *sptr;
    
    sptr = in;
    size /= EC_METHOD_CHUNK_SIZE * k;
    
    for (i = 0; i < size; i++)
    {
        if (idx < k)
        {   
            memcpy(out, sptr+idx*EC_METHOD_CHUNK_SIZE, EC_METHOD_CHUNK_SIZE);
        }
        else
        {   
            for (j = 0; j < k; j++)
            {
                data_ptr[j] = sptr + j*EC_METHOD_CHUNK_SIZE;
            }
            
            gf_vect_dot_prod_sse(EC_METHOD_CHUNK_SIZE, k, g_tbls+(idx-k)*k*32, data_ptr, out);
        }
        
        out += EC_METHOD_CHUNK_SIZE;
        sptr += EC_METHOD_CHUNK_SIZE*k;
    }
    
    return  size*EC_METHOD_CHUNK_SIZE;
}

size_t ec_method_decode(size_t size, uint32_t k, uint8_t *matrix, uint32_t * rows,
                        uint8_t ** in, uint8_t * out)
{   
    uint32_t i, j;
    uint8_t *decoding_matrix = NULL;
    uint8_t *decoding_tables = NULL;
    
    size /= EC_METHOD_CHUNK_SIZE;
    
    /* 如果全部是源数据，直接拷贝数据返回 */
    if (rows[k-1] < k)
    {   
        for (i = 0; i < size; i++)
        {
            for (j = 0; j < k; j++)
            {
                memcpy(out, in[j]+i*EC_METHOD_CHUNK_SIZE, EC_METHOD_CHUNK_SIZE);
                out += EC_METHOD_CHUNK_SIZE;
            }
        }

        return size * EC_METHOD_CHUNK_SIZE * k;
    }

    decoding_matrix = talloc(uint8_t, k*k);
    if(decoding_matrix == NULL)
    {
        return -1;
    }

    decoding_tables = talloc(uint8_t, k*k*32);
    if (decoding_tables == NULL)
    {
        free(decoding_matrix);
        return -1;
    }

    if (ec_make_decoding_matrix(k, matrix, decoding_matrix, rows) < 0)
    {
        free(decoding_matrix);
        free(decoding_tables);
        return -1;
    }
    
    ec_init_tables(k, k, decoding_matrix, decoding_tables);

    for(i = 0; i < size; i++)
    {
        for (j = 0; j < k; j++)
        {             
            gf_vect_dot_prod_sse(EC_METHOD_CHUNK_SIZE, k, decoding_tables+(j*k*32), in, out);
            out += EC_METHOD_CHUNK_SIZE;
        }

        if ( i < size-1 )
        {
            for (j = 0; j < k; j++)
            {
                in[j] += EC_METHOD_CHUNK_SIZE;
            }
        }
    }

    free(decoding_tables);
    free(decoding_matrix);
       
    return size * EC_METHOD_CHUNK_SIZE * k;
}
