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

//#include "ec-gf.h"
#include "ec-method.h"
#include "jerasure.h"

#define talloc(type, num) (type *) malloc(sizeof(type)*(num))

int ec_make_decoding_matrix(int k, int w, int *matrix, int *decoding_matrix, uint32_t *rows)
{
  int i, j, *tmpmat;

  /*重新组合矩阵:

  比如:
  k = 4, m =2 

   编码矩阵如下:
   
   1   0   0   0   D0     D0
   0   1   0   0   D1     D1
   0   0   1   0   D2     D2
   0   0   0   1   D3   = D3
   
   1   1   1   1          C0
   1  70 143 200          C1

   假设丢失了数据D1， C1:
   重新选择矩阵:

   [1   0   0   0]   D0     D0
   [0   0   1   0]   D1  =  D2
   [0   0   0   1]   D2     D3
   [1   1   1   1]   D3     C0
   
  */
  
  tmpmat = talloc(int, k*k);
  if (tmpmat == NULL) { return -1; }

  for (i = 0; i < k; i++) 
  {
    if (rows[i] < k) 
    {
      for (j = 0; j < k; j++) 
        tmpmat[i*k+j] = 0;
      
      tmpmat[i*k+rows[i]] = 1;
    } 
    else 
    {
      for (j = 0; j < k; j++) 
      {
        tmpmat[i*k+j] = matrix[(rows[i]-k)*k+j];
      }
    }
  }

  /*  计算下面矩阵的逆矩阵 
   [1   0   0   0]   D0     D0
   [0   0   1   0]   D1  =  D2
   [0   0   0   1]   D2     D3
   [1   1   1   1]   D3     C0  
   */
   
  i = jerasure_invert_matrix(tmpmat, decoding_matrix, k, w);
  free(tmpmat);
  
  return i;
}

size_t ec_method_encode(uint32_t k, int32_t m, int32_t idx, int32_t ***smart,
                        uint8_t * in, uint8_t * out, size_t size)
{
    uint32_t i, j, tdone;
    uint8_t **ptr_copy = NULL;
    uint32_t w_package_size = 0;
    uint8_t *sptr;
    size_t strip_size = 0;
    uint8_t *code_ptr;

    strip_size = EC_METHOD_CHUNK_SIZE * k;
    size /= strip_size;

    sptr = in;
    code_ptr = out;
    
    if (idx < k)
    {
        for (i = 0; i < size; i++)
        {
            memcpy(code_ptr, sptr+idx*EC_METHOD_CHUNK_SIZE, EC_METHOD_CHUNK_SIZE);
            code_ptr += EC_METHOD_CHUNK_SIZE;
            sptr += strip_size;
        }
        goto ret;
    }

    w_package_size = EC_METHOD_PACKAGE_SIZE*EC_METHOD_WORD_SIZE;
    
    ptr_copy = talloc(uint8_t *, k);
    
    for (i = 0; i < size; i++)
    {     
        for (j = 0; j < k; j++)
        {
            ptr_copy[j] = in + i*EC_METHOD_CHUNK_SIZE*k + j*EC_METHOD_CHUNK_SIZE;
        }

        for (tdone = 0; tdone < EC_METHOD_CHUNK_SIZE; tdone += w_package_size) 
        {
            jerasure_do_scheduled_operations_rely_idx((char **)ptr_copy, (char *)code_ptr, smart[idx-k], EC_METHOD_PACKAGE_SIZE);
            for (j = 0; j < k; j++) 
                ptr_copy[j] += w_package_size;
            
            code_ptr += w_package_size;
        }
    }

    if(ptr_copy != NULL)
    {
        free(ptr_copy);
    }
    
ret:    
    return  size*EC_METHOD_CHUNK_SIZE;
}


size_t ec_method_decode(size_t size, uint32_t k, int32_t *matrix, uint32_t * rows,
                        uint8_t ** in, uint8_t * out)
{   
    uint32_t i, j;
    int32_t *decoding_matrix;
    
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

        goto end;
    }

    /* 根据row_id , 获得解码矩阵 */
    
    decoding_matrix = talloc(int, k*k);
    if (ec_make_decoding_matrix(k, EC_METHOD_WORD_SIZE, matrix, decoding_matrix, rows) < 0)
    {
        free(decoding_matrix);
        return -1;
    }

    for(i = 0; i < size; i++)
    {
        for (j = 0; j < k; j++)
        {
            jerasure_matrix_dotprod_one(k, EC_METHOD_WORD_SIZE, decoding_matrix+(j*k),
                          (char **)in, (char *)out, EC_METHOD_CHUNK_SIZE);
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
    
    free(decoding_matrix);
    
end:    
    return size * EC_METHOD_CHUNK_SIZE * k;
}
