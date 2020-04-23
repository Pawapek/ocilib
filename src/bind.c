/*
 * OCILIB - C Driver for Oracle (C Wrapper for Oracle OCI)
 *
 * Website: http://www.ocilib.net
 *
 * Copyright (c) 2007-2020 Vincent ROGIER <vince.rogier@ocilib.net>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bind.h"

#include "array.h"
#include "collection.h"
#include "date.h"
#include "file.h"
#include "helpers.h"
#include "interval.h"
#include "lob.h"
#include "macro.h"
#include "memory.h"
#include "number.h"
#include "object.h"
#include "ref.h"
#include "timestamp.h"

static const unsigned int CharsetFormValues[]   = { OCI_CSF_DEFAULT, OCI_CSF_NATIONAL };
static const unsigned int BindDirectionValues[] = { OCI_BDM_IN, OCI_BDM_OUT, OCI_BDM_IN_OUT };

void BindAllocateBuffers
(
    OCI_Context *ctx,
    OCI_Bind    *bnd,
    unsigned int mode,
    boolean      reused,
    unsigned int nballoc,
    unsigned int nbelem,
    boolean      plsql_table
)
{
    /* allocate indicators array */

    if (OCI_STATUS)
    {
        OCI_ALLOCATE_DATA(OCI_IPC_BIND, bnd->buffer.inds, nballoc)

        if (OCI_STATUS && SQLT_NTY == bnd->code)
        {
            OCI_ALLOCATE_DATA(OCI_IPC_INDICATOR_ARRAY, bnd->buffer.obj_inds, nballoc)
        }
    }

    /* check need for PL/SQL table extra info */

    if (OCI_STATUS && plsql_table)
    {
        bnd->nbelem = nbelem;

        /* allocate array of returned codes */

        OCI_ALLOCATE_DATA(OCI_IPC_PLS_RCODE_ARRAY, bnd->plrcds, nballoc)
    }

    /* set allocation mode prior any required data allocation */

    if (OCI_STATUS)
    {
        bnd->alloc_mode = (ub1)bnd->stmt->bind_alloc_mode;
    }

    /* for handle based data types, we need to allocate an array of handles for
       bind calls because OCILIB uses external arrays of OCILIB Objects */

    if (OCI_STATUS && (OCI_BIND_INPUT == mode))
    {
        if (OCI_BAM_EXTERNAL == bnd->alloc_mode)
        {
            if ((OCI_CDT_RAW     != bnd->type)  &&
                (OCI_CDT_LONG    != bnd->type)  &&
                (OCI_CDT_CURSOR  != bnd->type)  &&
                (OCI_CDT_LONG    != bnd->type)  &&
                (OCI_CDT_BOOLEAN != bnd->type)  &&
                (OCI_CDT_NUMERIC != bnd->type || SQLT_VNU == bnd->code) &&
                (OCI_CDT_TEXT    != bnd->type || OCILib.use_wide_char_conv))
            {
                bnd->alloc = TRUE;

                if (reused)
                {
                    OCI_FREE(bnd->buffer.data)
                }

                OCI_ALLOCATE_BUFFER(OCI_IPC_BUFF_ARRAY, bnd->buffer.data, bnd->size, nballoc)
            }
            else
            {
                bnd->buffer.data = (void **)bnd->input;
            }
        }
    }

    /* setup data length array */

    if (OCI_STATUS && ((OCI_CDT_RAW == bnd->type) || (OCI_CDT_TEXT == bnd->type)))
    {
        OCI_ALLOCATE_BUFFER(OCI_IPC_BUFF_ARRAY, bnd->buffer.lens, sizeof(ub2), nballoc)

        /* initialize length array with buffer default size */

        if (OCI_STATUS)
        {
            for (unsigned int i = 0; i < nbelem; i++)
            {
                *(ub2*)(((ub1 *)bnd->buffer.lens) + sizeof(ub2) * (size_t) i) = (ub2)bnd->size;
            }
        }
    }

    /* internal allocation if needed */

    if (!bnd->input && (OCI_BAM_INTERNAL == bnd->alloc_mode))
    {
        OCI_STATUS = BindAllocData(bnd);
    }

}
/* --------------------------------------------------------------------------------------------- *
* BindCheckAvailability
* --------------------------------------------------------------------------------------------- */

void BindCheckAvailability
(
    OCI_Context   *ctx,
    OCI_Statement *stmt,
    unsigned int   mode,
    boolean        reused
)
{
    if (OCI_STATUS && !reused)
    {
        if (OCI_BIND_INPUT == mode)
        {
            if (stmt->nb_ubinds >= OCI_BIND_MAX)
            {
                ExceptionMaxBind(stmt);
                OCI_STATUS = FALSE;
            }

            /* allocate user bind array if necessary */

            OCI_REALLOCATE_DATA
            (
                OCI_IPC_BIND_ARRAY,
                stmt->ubinds,
                stmt->nb_ubinds,
                stmt->allocated_ubinds,
                min(stmt->nb_ubinds + OCI_BIND_ARRAY_GROWTH_FACTOR, OCI_BIND_MAX)
            )
        }
        else
        {
            if (stmt->nb_rbinds >= OCI_BIND_MAX)
            {
                ExceptionMaxBind(stmt);
                OCI_STATUS = FALSE;
            }

            /* allocate register bind array if necessary */

            OCI_REALLOCATE_DATA
            (
                OCI_IPC_BIND_ARRAY,
                stmt->rbinds,
                stmt->nb_rbinds,
                stmt->allocated_rbinds,
                min(stmt->nb_rbinds + OCI_BIND_ARRAY_GROWTH_FACTOR, OCI_BIND_MAX)
            )
        }
    }
}

 /* --------------------------------------------------------------------------------------------- *
  * BindPerformBinding
  * --------------------------------------------------------------------------------------------- */

void BindPerformBinding
(
    OCI_Context  *ctx, 
    OCI_Bind     *bnd,
    unsigned int  mode,
    unsigned int  index,
    unsigned int  exec_mode,
    boolean       plsql_table
)
{
    if (OCI_BIND_BY_POS == bnd->stmt->bind_mode)
    {
        OCI_EXEC
        (
            OCIBindByPos
            (
                bnd->stmt->stmt,
                (OCIBind **)&bnd->buffer.handle,
                bnd->stmt->con->err,
                (ub4)index,
                (void *)bnd->buffer.data,
                bnd->size,
                bnd->code,
                (void *)bnd->buffer.inds,
                (ub2 *)bnd->buffer.lens,
                bnd->plrcds,
                (ub4)(plsql_table ? bnd->nbelem : 0),
                (ub4*)(plsql_table ? &bnd->nbelem : NULL),
                (ub4) exec_mode
            )
        )
    }
    else
    {
        dbtext * dbstr = NULL;
        int      dbsize = -1;

        dbstr = OCI_StringGetOracleString(bnd->name, &dbsize);

        OCI_EXEC
        (
            OCIBindByName
            (
                bnd->stmt->stmt,
                (OCIBind **)&bnd->buffer.handle,
                bnd->stmt->con->err,
                (OraText *)dbstr,
                (sb4)dbsize,
                (void *)bnd->buffer.data,
                bnd->size,
                bnd->code,
                (void *)bnd->buffer.inds,
                (ub2 *)bnd->buffer.lens,
                bnd->plrcds,
                (ub4)(plsql_table ? bnd->nbelem : 0),
                (ub4*)(plsql_table ? &bnd->nbelem : NULL),
                (ub4) exec_mode
            )
        )

            OCI_StringReleaseOracleString(dbstr);
    }

    if (SQLT_NTY == bnd->code || SQLT_REF == bnd->code)
    {
        OCI_EXEC
        (
            OCIBindObject
            (
                (OCIBind *)bnd->buffer.handle,
                bnd->stmt->con->err,
                (OCIType *)bnd->typinf->tdo,
                (void **)bnd->buffer.data,
                (ub4 *)NULL,
                (void **)bnd->buffer.obj_inds,
                (ub4 *)NULL
            )
        )
    }

    if (OCI_BIND_OUTPUT == mode)
    {
        /* register output placeholder */

        OCI_EXEC
        (
            OCIBindDynamic
            (
                (OCIBind *)bnd->buffer.handle, 
                bnd->stmt->con->err, 
                (dvoid *)bnd, 
                ProcInBind, 
                (dvoid *)bnd, 
                ProcOutBind
            )
        )
    }
}

 /* --------------------------------------------------------------------------------------------- *
  * BindAddToStatement
  * --------------------------------------------------------------------------------------------- */

void BindAddToStatement
(
    OCI_Bind     *bnd,
    unsigned int  mode,
    boolean       reused
)
{
    if (OCI_BIND_INPUT == mode)
    {
        if (!reused)
        {
            bnd->stmt->ubinds[bnd->stmt->nb_ubinds++] = bnd;

            /* for user binds, add a positive index */

            OCI_HashAddInt(bnd->stmt->map, bnd->name, bnd->stmt->nb_ubinds);
        }
    }
    else
    {
        /* for register binds, add a negative index */

        bnd->stmt->rbinds[bnd->stmt->nb_rbinds++] = bnd;

        const int index = (int)bnd->stmt->nb_rbinds;

        OCI_HashAddInt(bnd->stmt->map, bnd->name, -index);
    }
}

/* --------------------------------------------------------------------------------------------- *
 * BindCreate
 * --------------------------------------------------------------------------------------------- */

OCI_Bind* BindCreate
(
    OCI_Context   *ctx,
    OCI_Statement *stmt,
    void          *data,
    const otext   *name,
    unsigned int   mode,
    ub4            size,
    ub1            type,
    unsigned int   code,
    unsigned int   subtype,
    OCI_TypeInfo  *typinf,
    unsigned int   nbelem
)
{
    OCI_Bind *bnd        = NULL;
    ub4 exec_mode        = OCI_DEFAULT;
    boolean plsql_table  = FALSE;
    boolean is_array     = FALSE;
    boolean reused       = FALSE;
    int index            = 0;
    int prev_index       = -1;
    unsigned int nballoc = nbelem;

    /* check index if necessary */

    if (OCI_BIND_BY_POS == stmt->bind_mode)
    {
        index = (int) ostrtol(&name[1], NULL, 10);

        if (index <= 0 || index > OCI_BIND_MAX)
        {
            ExceptionOutOfBounds(stmt->con, index);
            OCI_STATUS = FALSE;
        }
    }

    /* check if the bind name has already been used */

    if (OCI_STATUS)
    {
        if (OCI_BIND_INPUT == mode)
        {
            prev_index = OCI_BindGetInternalIndex(stmt, name);

            if (prev_index > 0)
            {
                if (!stmt->bind_reuse)
                {
                    ExceptionBindAlreadyUsed(stmt, name);
                    OCI_STATUS = FALSE;
                }
                else
                {
                    bnd = stmt->ubinds[prev_index-1];

                    if (bnd->type != type)
                    {
                        ExceptionRebindBadDatatype(stmt, name);
                        OCI_STATUS = FALSE;
                    }
                    else
                    {
                        reused = TRUE;
                    }
                }

                index = prev_index;
            }
        }
    }

    /* check if we can handle another bind */

    BindCheckAvailability(ctx, stmt, mode, reused);

    /* checks done */

    if (OCI_STATUS)
    {
        /* check out the number of elements that the bind variable will hold */

        if (nbelem > 0)
        {
            /* is it a pl/sql table bind ? */

            if (OCI_IS_PLSQL_STMT(stmt->type))
            {
                plsql_table = TRUE;
                is_array    = TRUE;
            }
        }
        else
        {
            nbelem   = stmt->nb_iters;
            is_array = stmt->bind_array;
        }

        /* compute iterations */
        if (nballoc < stmt->nb_iters_init)
        {
            nballoc = (size_t) stmt->nb_iters_init;
        }

        /* create hash table for mapping bind names / index */

        if (!stmt->map)
        {
            stmt->map = OCI_HashCreate(OCI_HASH_DEFAULT_SIZE, OCI_HASH_INTEGER);
            OCI_STATUS = (NULL != stmt->map);
        }
    }

    /* allocate bind object */

    OCI_ALLOCATE_DATA(OCI_IPC_BIND, bnd, 1)

    /* initialize bind object */

    if (OCI_STATUS)
    {
        /* initialize bind attributes */

        bnd->stmt      = stmt;
        bnd->input     = (void **) data;
        bnd->type      = type;
        bnd->size      = size;
        bnd->code      = (ub2) code;
        bnd->subtype   = (ub1) subtype;
        bnd->is_array  = is_array;
        bnd->typinf    = typinf;
        bnd->csfrm     = OCI_CSF_NONE;
        bnd->direction = OCI_BDM_IN_OUT;

        if (!bnd->name)
        {
            bnd->name = ostrdup(name);
        }

        /* initialize buffer */

        bnd->buffer.count   = nbelem;
        bnd->buffer.sizelen = sizeof(ub2);

        BindAllocateBuffers(ctx, bnd, mode, reused, nballoc, nbelem, plsql_table);
          
        /* if we bind an OCI_Long or any output bind, we need to change the
           execution mode to provide data at execute time */

        if (OCI_CDT_LONG == bnd->type)
        {
            OCI_Long *lg = (OCI_Long *)  bnd->input;

            lg->maxsize = size;
            exec_mode   = OCI_DATA_AT_EXEC;

            if (OCI_CLONG == bnd->subtype)
            {
                lg->maxsize /= (unsigned int) sizeof(otext);
                lg->maxsize *= (unsigned int) sizeof(dbtext);
            }
        }
        else if (OCI_BIND_OUTPUT == mode)
        {
            exec_mode = OCI_DATA_AT_EXEC;
        }
    }

    /* OCI binding */

    if (OCI_STATUS)
    {
        BindPerformBinding(ctx, bnd, mode, index, exec_mode, plsql_table);
    }

    /* set charset form */

    if (OCI_STATUS)
    {
        if ((OCI_CDT_LOB == bnd->type) && (OCI_NCLOB == bnd->subtype))
        {
            ub1 csfrm = SQLCS_NCHAR;

            OCI_SET_ATTRIB(OCI_HTYPE_BIND, OCI_ATTR_CHARSET_FORM, bnd->buffer.handle, &csfrm, sizeof(csfrm))
        }
    }

    /* on success, we :
         - add the bind handle to the bind array
         - add the bind index to the map
    */

    if (OCI_STATUS)
    {
        BindAddToStatement(bnd, mode, reused);
    }

    if (!OCI_STATUS)
    {
        if (bnd && (prev_index  == -1))
        {
            BindFree(bnd);
            bnd = NULL;
        }
    }

    return bnd;  
}

/* --------------------------------------------------------------------------------------------- *
 * BindFree
 * --------------------------------------------------------------------------------------------- */

boolean BindFree
(
    OCI_Bind *bnd
)
{
    boolean res = TRUE;

    OCI_CHECK(NULL == bnd, FALSE)

    if (OCI_BAM_INTERNAL == bnd->alloc_mode)
    {
        if (bnd->is_array)
        {
            res = ArrayFreeFromHandles(bnd->input);
        }
        else
        {
            switch (bnd->type)
            {
                case OCI_CDT_NUMERIC:
                case OCI_CDT_TEXT:
                {
                    /* OCINumber binds */

                    if (bnd->type == OCI_CDT_NUMERIC && bnd->subtype == OCI_NUM_NUMBER)
                    {
                        FreeObjectFromType(bnd->input, bnd->type);
                    }
                    else
                    {
                        /* strings requiring otext / dbtext conversions and 64 bit integers */

                        OCI_MemFree(bnd->input);

                        if (bnd->alloc)
                        {
                            OCI_FREE(bnd->buffer.data)
                        }
                    }
                    break;
                }
                default:
                {
                    FreeObjectFromType(bnd->input, bnd->type);
                }
            }
        }
    }
    else
    {
        if (bnd->alloc)
        {
            OCI_FREE(bnd->buffer.data)
        }
    }

    OCI_FREE(bnd->buffer.inds)
    OCI_FREE(bnd->buffer.obj_inds)
    OCI_FREE(bnd->buffer.lens)
    OCI_FREE(bnd->buffer.tmpbuf)
    OCI_FREE(bnd->plrcds)
    OCI_FREE(bnd->name)
    OCI_FREE(bnd)

    return res;
}

/* --------------------------------------------------------------------------------------------- *
 * BindAllocData
 * --------------------------------------------------------------------------------------------- */

boolean BindAllocData
(
    OCI_Bind *bnd
)
{
    OCI_CHECK(NULL == bnd, FALSE)

    if (bnd->is_array)
    {
        unsigned int struct_size = 0;
        unsigned int elem_size   = 0;
        unsigned int handle_type = 0;

        OCI_Array *arr = NULL;

        switch (bnd->type)
        {
            case OCI_CDT_NUMERIC:
            {
                if (SQLT_VNU == bnd->code)
                {
                    struct_size = sizeof(big_int);
                    elem_size   = sizeof(OCINumber);
                }
                else
                {
                    struct_size = bnd->size;
                }
                break;
            }
            case OCI_CDT_DATETIME:
            {
                struct_size = sizeof(OCI_Date);
                elem_size   = sizeof(OCIDate);
                break;
            }
            case OCI_CDT_TEXT:
            {
                struct_size = bnd->size;

                if (OCILib.use_wide_char_conv)
                {
                    elem_size  = bnd->size * (sizeof(otext) / sizeof(dbtext));
                }
                break;
            }
            case OCI_CDT_LOB:
            {
                struct_size = sizeof(OCI_Lob);
                elem_size   = sizeof(OCILobLocator *);
                handle_type = OCI_DTYPE_LOB;
                break;
            }
            case OCI_CDT_FILE:
            {
                struct_size = sizeof(OCI_File);
                elem_size   = sizeof(OCILobLocator *);
                handle_type = OCI_DTYPE_LOB;
                break;
            }
            case OCI_CDT_TIMESTAMP:
            {
                struct_size = sizeof(OCI_Timestamp);
                elem_size   = sizeof(OCIDateTime *);
                handle_type = ExternalSubTypeToHandleType(OCI_CDT_TIMESTAMP, bnd->subtype);
                break;
            }
            case OCI_CDT_INTERVAL:
            {
                struct_size = sizeof(OCI_Interval);
                elem_size   = sizeof(OCIInterval *);
                handle_type = ExternalSubTypeToHandleType(OCI_CDT_INTERVAL, bnd->subtype);
                break;
            }
            case OCI_CDT_RAW:
            {
                struct_size = bnd->size;
                break;
            }
            case OCI_CDT_OBJECT:
            {
                struct_size = sizeof(OCI_Object);
                elem_size   = sizeof(void *);
                break;
            }
            case OCI_CDT_COLLECTION:
            {
                struct_size = sizeof(OCI_Coll);
                elem_size   = sizeof(OCIColl *);
                break;
            }
            case OCI_CDT_REF:
            {
                struct_size = sizeof(OCI_Ref);
                elem_size   = sizeof(OCIRef *);
                break;
            }
        }

        arr = ArrayCreate(bnd->stmt->con, bnd->buffer.count, bnd->type, bnd->subtype,
                  elem_size, struct_size, handle_type, bnd->typinf);

        if (arr)
        {
            switch (bnd->type)
            {
                case OCI_CDT_NUMERIC:
                {
                    if (bnd->subtype == OCI_NUM_NUMBER)
                    {
                        bnd->buffer.data = (void **)arr->mem_handle;
                        bnd->input       = (void **)arr->tab_obj;

                    }
                    else if (SQLT_VNU == bnd->code)
                    {
                        bnd->buffer.data = (void **) arr->mem_handle;
                        bnd->input       = (void **) arr->mem_struct;
                        bnd->alloc       = TRUE;
                    }
                    else
                    {
                        bnd->buffer.data = (void **) arr->mem_struct;
                        bnd->input       = (void **) bnd->buffer.data;
                    }
                    break;
                }
                case OCI_CDT_TEXT:
                {
                    if (OCILib.use_wide_char_conv)
                    {
                        bnd->buffer.data = (void **) arr->mem_handle;
                        bnd->input       = (void **) arr->mem_struct;
                        bnd->alloc       = TRUE;
                    }
                    else
                    {
                        bnd->buffer.data = (void **) arr->mem_struct;
                        bnd->input       = (void **) bnd->buffer.data;
                    }

                    break;
                }
                case OCI_CDT_RAW:
                {
                    bnd->buffer.data = (void **) arr->mem_struct;
                    bnd->input       = (void **) bnd->buffer.data;
                    break;
                }
                case OCI_CDT_DATETIME:
                case OCI_CDT_LOB:
                case OCI_CDT_FILE:
                case OCI_CDT_TIMESTAMP:
                case OCI_CDT_INTERVAL:
                case OCI_CDT_OBJECT:
                case OCI_CDT_COLLECTION:
                case OCI_CDT_REF:
                {
                    bnd->buffer.data = (void **) arr->mem_handle;
                    bnd->input       = (void **) arr->tab_obj;
                    break;
                }
            }
        }
    }
    else
    {
        switch (bnd->type)
        {
            case OCI_CDT_NUMERIC:
            {
                if (bnd->subtype == OCI_NUM_NUMBER)
                {
                    OCI_Number *number = OCI_NumberCreate(bnd->stmt->con);

                    if (number)
                    {
                        bnd->input       = (void **)number;
                        bnd->buffer.data = (void **)number->handle;
                    }

                }
                else if (SQLT_VNU == bnd->code)
                {
                    bnd->input       = (void **) OCI_MemAlloc(OCI_IPC_VOID, sizeof(big_int),   1, TRUE);
                    bnd->buffer.data = (void **) OCI_MemAlloc(OCI_IPC_VOID, sizeof(OCINumber), 1, TRUE);
                }
                else
                {
                    bnd->input       = (void **) OCI_MemAlloc(OCI_IPC_VOID, bnd->size, 1, TRUE);
                    bnd->buffer.data = (void **) bnd->input;
                }
                break;
            }
            case OCI_CDT_DATETIME:
            {
                OCI_Date *date = OCI_DateCreate(bnd->stmt->con);

                if (date)
                {
                    bnd->input       = (void **) date;
                    bnd->buffer.data = (void **) date->handle;
                }
                break;
            }
            case OCI_CDT_TEXT:
            {
                if (OCILib.use_wide_char_conv)
                {
                    bnd->buffer.data = (void **) OCI_MemAlloc(OCI_IPC_STRING, bnd->size * (sizeof(otext) / sizeof(dbtext)), 1, TRUE);
                    bnd->input       = (void **) OCI_MemAlloc(OCI_IPC_STRING, bnd->size, 1, TRUE);
                }
                else
                {
                    bnd->buffer.data = (void **) OCI_MemAlloc(OCI_IPC_STRING, bnd->size, 1, TRUE);
                    bnd->input       = (void **) bnd->buffer.data;
                }
                break;
            }
            case OCI_CDT_LOB:
            {
                OCI_Lob *lob = OCI_LobCreate(bnd->stmt->con, bnd->subtype);

                if (lob)
                {
                    bnd->input       = (void **) lob;
                    bnd->buffer.data = (void **) lob->handle;
                }
                break;
            }
            case OCI_CDT_FILE:
            {
                OCI_File *file = OCI_FileCreate(bnd->stmt->con,  bnd->subtype);

                if (file)
                {
                    bnd->input       = (void **) file;
                    bnd->buffer.data = (void **) file->handle;
                }
                break;
            }
            case OCI_CDT_TIMESTAMP:
            {
                OCI_Timestamp *tmsp = OCI_TimestampCreate(bnd->stmt->con, bnd->subtype);

                if (tmsp)
                {
                    bnd->input       = (void **) tmsp;
                    bnd->buffer.data = (void **) tmsp->handle;
                }
                break;
            }
            case OCI_CDT_INTERVAL:
            {
                OCI_Interval *itv = OCI_IntervalCreate(bnd->stmt->con, bnd->subtype);

                if (itv)
                {
                    bnd->input       = (void **) itv;
                    bnd->buffer.data = (void **) itv->handle;
                }
                break;
            }
            case OCI_CDT_RAW:
            {
                bnd->input       = (void **) OCI_MemAlloc(OCI_IPC_VOID, bnd->size, 1, TRUE);
                bnd->buffer.data = (void **) bnd->input;
                break;
            }
            case OCI_CDT_OBJECT:
            {
                OCI_Object *obj = OCI_ObjectCreate(bnd->stmt->con, bnd->typinf);

                if (obj)
                {
                    bnd->input       = (void **) obj;
                    bnd->buffer.data = (void **) obj->handle;
                }
                break;
            }
            case OCI_CDT_COLLECTION:
            {
                OCI_Coll *coll = OCI_CollCreate(bnd->typinf);

                if (coll)
                {
                    bnd->input       = (void **) coll;
                    bnd->buffer.data = (void **) coll->handle;
                }
                break;
            }
            case OCI_CDT_REF:
            {
                OCI_Ref *ref = OCI_RefCreate(bnd->stmt->con, bnd->typinf);

                if (ref)
                {
                    bnd->input       = (void **) ref;
                    bnd->buffer.data = (void **) ref->handle;
                }
                break;
            }
        }
    }

    return (NULL != bnd->input);
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetNullIndicator
 * --------------------------------------------------------------------------------------------- */

boolean OCI_BindSetNullIndicator
(
    OCI_Bind    *bnd,
    unsigned int position,
    sb2          value
)
{
    OCI_CHECK(NULL == bnd, FALSE)

    if (bnd->buffer.inds)
    {
        bnd->buffer.inds[position - 1] = value;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetName
 * --------------------------------------------------------------------------------------------- */

const otext * BindGetName
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(const otext*, NULL, OCI_IPC_BIND, bnd, name, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetType
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetType
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_BIND, bnd, type, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetSubtype
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetSubtype
(
    OCI_Bind *bnd
)
{
    OCI_CALL_ENTER(unsigned int, OCI_UNKNOWN)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    if (OCI_CDT_NUMERIC   == bnd->type ||
        OCI_CDT_LONG      == bnd->type ||
        OCI_CDT_LOB       == bnd->type ||
        OCI_CDT_FILE      == bnd->type ||
        OCI_CDT_TIMESTAMP == bnd->type ||
        OCI_CDT_INTERVAL  == bnd->type)
    {
        OCI_RETVAL = (unsigned int)bnd->subtype;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetDataCount
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetDataCount
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(unsigned int, 0, OCI_IPC_BIND, bnd, buffer.count, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetData
 * --------------------------------------------------------------------------------------------- */

void * BindGetData
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(void *, NULL, OCI_IPC_BIND, bnd, input, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetStatement
 * --------------------------------------------------------------------------------------------- */

OCI_Statement * BindGetStatement
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(OCI_Statement *, NULL, OCI_IPC_BIND, bnd, stmt, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetDataSize
 * --------------------------------------------------------------------------------------------- */

boolean BindSetDataSize
(
    OCI_Bind    *bnd,
    unsigned int size
)
{
    return OCI_BindSetDataSizeAtPos(bnd, 1, size);
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetDataSizeAtPos
 * --------------------------------------------------------------------------------------------- */

boolean BindSetDataSizeAtPos
(
    OCI_Bind    *bnd,
    unsigned int position,
    unsigned int size
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_BOUND(bnd->stmt->con, position, 1, bnd->buffer.count)
    OCI_CALL_CHECK_MIN(bnd->stmt->con, bnd->stmt, size, 1)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    if (bnd->buffer.lens)
    {
        if (OCI_CDT_TEXT == bnd->type)
        {
            if (bnd->size == (sb4) size)
            {
                size += (unsigned int) (size_t) sizeof(dbtext);
            }

            size *= (unsigned int) sizeof(dbtext);
        }

        ((ub2 *) bnd->buffer.lens)[position-1] = (ub2) size;

        OCI_RETVAL = TRUE;
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetDataSize
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetDataSize
(
    OCI_Bind *bnd
)
{
    return OCI_BindGetDataSizeAtPos(bnd, 1);
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetDataSizeAtPos
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetDataSizeAtPos
(
    OCI_Bind    *bnd,
    unsigned int position
)
{
    OCI_CALL_ENTER(unsigned int, 0)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_BOUND(bnd->stmt->con, position, 1, bnd->buffer.count)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    if (bnd->buffer.lens)
    {
        OCI_RETVAL = (unsigned int)((ub2 *)bnd->buffer.lens)[position - 1];

        if (OCI_CDT_TEXT == bnd->type)
        {
            if (bnd->size == (sb4)call_retval)
            {
                OCI_RETVAL -= (unsigned int) sizeof(dbtext);
            }

            OCI_RETVAL /= (unsigned int) sizeof(dbtext);
        }
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetNullAtPos
 * --------------------------------------------------------------------------------------------- */

boolean BindSetNullAtPos
(
    OCI_Bind    *bnd,
    unsigned int position
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_BOUND(bnd->stmt->con, position, 1, bnd->buffer.count)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    OCI_RETVAL = OCI_STATUS = OCI_BindSetNullIndicator(bnd, position, OCI_IND_NULL);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetNull
 * --------------------------------------------------------------------------------------------- */

boolean BindSetNull
(
    OCI_Bind *bnd
)
{
    return OCI_BindSetNullAtPos(bnd, 1);
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetNotNullAtPos
 * --------------------------------------------------------------------------------------------- */

boolean BindSetNotNullAtPos
(
    OCI_Bind    *bnd,
    unsigned int position
)
{   
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_BOUND(bnd->stmt->con, position, 1, bnd->buffer.count)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    OCI_RETVAL = OCI_STATUS = OCI_BindSetNullIndicator(bnd, position, OCI_IND_NOTNULL);

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetNotNull
 * --------------------------------------------------------------------------------------------- */

boolean BindSetNotNull
(
    OCI_Bind *bnd
)
{
    return OCI_BindSetNotNullAtPos(bnd, 1);
}

/* --------------------------------------------------------------------------------------------- *
 * BindIsNullAtPos
 * --------------------------------------------------------------------------------------------- */

boolean BindIsNullAtPos
(
    OCI_Bind    *bnd,
    unsigned int position
)
{
    OCI_CALL_ENTER(boolean, TRUE)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_BOUND(bnd->stmt->con, position, 1, bnd->buffer.count)
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    if (bnd->buffer.inds)
    {
        OCI_RETVAL = (OCI_IND_NULL == bnd->buffer.inds[position - 1]);
    }

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindIsNull
 * --------------------------------------------------------------------------------------------- */

boolean BindIsNull
(
    OCI_Bind *bnd
)
{
    return OCI_BindIsNullAtPos(bnd, 1);
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetCharsetForm
 * --------------------------------------------------------------------------------------------- */

boolean BindSetCharsetForm
(
    OCI_Bind    *bnd,
    unsigned int csfrm
)
{
    OCI_CALL_ENTER(boolean, FALSE)
    OCI_CALL_CHECK_PTR(OCI_IPC_BIND, bnd)
    OCI_CALL_CHECK_ENUM_VALUE(bnd->stmt->con, bnd->stmt, csfrm, CharsetFormValues, OTEXT("CharsetForm"))
    OCI_CALL_CONTEXT_SET_FROM_STMT(bnd->stmt)

    if ((OCI_CDT_TEXT == bnd->type) || (OCI_CDT_LONG == bnd->type))
    {
        if (OCI_CSF_NATIONAL == csfrm)
        {
            bnd->csfrm = SQLCS_NCHAR;
        }
        else if (OCI_CSF_DEFAULT == csfrm)
        {
            bnd->csfrm = SQLCS_IMPLICIT;
        }

        OCI_SET_ATTRIB(OCI_HTYPE_BIND, OCI_ATTR_CHARSET_FORM, bnd->buffer.handle, &bnd->csfrm, sizeof(bnd->csfrm))
    }

    OCI_RETVAL = OCI_STATUS;

    OCI_CALL_EXIT()
}

/* --------------------------------------------------------------------------------------------- *
 * BindSetDirection
 * --------------------------------------------------------------------------------------------- */

boolean BindSetDirection
(
    OCI_Bind    *bnd,
    unsigned int direction
)
{
    OCI_SET_PROP_ENUM(ub1, OCI_IPC_BIND, bnd, direction, direction, BindDirectionValues, OTEXT("Direction"), bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
 * BindGetDirection
 * --------------------------------------------------------------------------------------------- */

unsigned int BindGetDirection
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_BIND, bnd, direction, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}

/* --------------------------------------------------------------------------------------------- *
* BindGetAllocationMode
* --------------------------------------------------------------------------------------------- */

unsigned int BindGetAllocationMode
(
    OCI_Bind *bnd
)
{
    OCI_GET_PROP(unsigned int, OCI_UNKNOWN, OCI_IPC_BIND, bnd, alloc_mode, bnd->stmt->con, bnd->stmt, bnd->stmt->con->err)
}
