/*
 * Copyright (c) 2009-2021, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "upb/reflection/def_pool.h"

#include <stdio.h>

#include "upb/reflection/def_builder.h"
#include "upb/reflection/def_type.h"
#include "upb/reflection/enum_def.h"
#include "upb/reflection/enum_value_def.h"
#include "upb/reflection/field_def.h"
#include "upb/reflection/file_def.h"
#include "upb/reflection/message_def.h"
#include "upb/reflection/service_def.h"

// Must be last.
#include "upb/port_def.inc"

struct upb_DefPool {
  upb_Arena* arena;
  upb_strtable syms;   // full_name -> packed def ptr
  upb_strtable files;  // file_name -> (upb_FileDef*)
  upb_inttable exts;   // (upb_MiniTable_Extension*) -> (upb_FieldDef*)
  upb_ExtensionRegistry* extreg;
  size_t bytes_loaded;
};

void upb_DefPool_Free(upb_DefPool* s) {
  upb_Arena_Free(s->arena);
  upb_gfree(s);
}

upb_DefPool* upb_DefPool_New(void) {
  upb_DefPool* s = upb_gmalloc(sizeof(*s));
  if (!s) return NULL;

  s->arena = upb_Arena_New();
  s->bytes_loaded = 0;

  if (!upb_strtable_init(&s->syms, 32, s->arena)) goto err;
  if (!upb_strtable_init(&s->files, 4, s->arena)) goto err;
  if (!upb_inttable_init(&s->exts, s->arena)) goto err;

  s->extreg = upb_ExtensionRegistry_New(s->arena);
  if (!s->extreg) goto err;

  return s;

err:
  upb_Arena_Free(s->arena);
  upb_gfree(s);
  return NULL;
}

bool _upb_DefPool_Contains(const upb_DefPool* s, const char* sym) {
  return upb_strtable_lookup(&s->syms, sym, NULL);
}

bool _upb_DefPool_Insert(upb_DefPool* s, const char* sym, upb_value v) {
  return _upb_DefPool_Insert2(s, sym, strlen(sym), v);
}

bool _upb_DefPool_Insert2(upb_DefPool* s, const char* sym, size_t size,
                          upb_value v) {
  return upb_strtable_insert(&s->syms, sym, size, v, s->arena);
}

bool _upb_DefPool_InsertExt(upb_DefPool* s, const upb_MiniTable_Extension* ext,
                            upb_FieldDef* f, upb_Arena* a) {
  return upb_inttable_insert(&s->exts, (uintptr_t)ext, upb_value_constptr(f),
                             a);
}

static const void* _upb_DefPool_Lookup(const upb_DefPool* s, const char* sym,
                                       upb_deftype_t type) {
  return _upb_DefPool_Lookup2(s, sym, strlen(sym), type);
}

const void* _upb_DefPool_Lookup2(const upb_DefPool* s, const char* sym,
                                 size_t size, upb_deftype_t type) {
  upb_value v;
  return upb_strtable_lookup2(&s->syms, sym, size, &v)
             ? _upb_DefType_Unpack(v, type)
             : NULL;
}

bool _upb_DefPool_LookupAny2(const upb_DefPool* s, const char* sym, size_t size,
                             upb_value* v) {
  return upb_strtable_lookup2(&s->syms, sym, size, v);
}

upb_ExtensionRegistry* _upb_DefPool_ExtReg(const upb_DefPool* s) {
  return s->extreg;
}

const upb_MessageDef* upb_DefPool_FindMessageByName(const upb_DefPool* s,
                                                    const char* sym) {
  return _upb_DefPool_Lookup(s, sym, UPB_DEFTYPE_MSG);
}

const upb_MessageDef* upb_DefPool_FindMessageByNameWithSize(
    const upb_DefPool* s, const char* sym, size_t len) {
  return _upb_DefPool_Lookup2(s, sym, len, UPB_DEFTYPE_MSG);
}

const upb_EnumDef* upb_DefPool_FindEnumByName(const upb_DefPool* s,
                                              const char* sym) {
  return _upb_DefPool_Lookup(s, sym, UPB_DEFTYPE_ENUM);
}

const upb_EnumValueDef* upb_DefPool_FindEnumByNameval(const upb_DefPool* s,
                                                      const char* sym) {
  return _upb_DefPool_Lookup(s, sym, UPB_DEFTYPE_ENUMVAL);
}

const upb_FileDef* upb_DefPool_FindFileByName(const upb_DefPool* s,
                                              const char* name) {
  upb_value v;
  return upb_strtable_lookup(&s->files, name, &v) ? upb_value_getconstptr(v)
                                                  : NULL;
}

const upb_FileDef* upb_DefPool_FindFileByNameWithSize(const upb_DefPool* s,
                                                      const char* name,
                                                      size_t len) {
  upb_value v;
  return upb_strtable_lookup2(&s->files, name, len, &v)
             ? upb_value_getconstptr(v)
             : NULL;
}

const upb_FieldDef* upb_DefPool_FindExtensionByNameWithSize(
    const upb_DefPool* s, const char* name, size_t size) {
  upb_value v;
  if (!upb_strtable_lookup2(&s->syms, name, size, &v)) return NULL;

  switch (_upb_DefType_Type(v)) {
    case UPB_DEFTYPE_FIELD:
      return _upb_DefType_Unpack(v, UPB_DEFTYPE_FIELD);
    case UPB_DEFTYPE_MSG: {
      const upb_MessageDef* m = _upb_DefType_Unpack(v, UPB_DEFTYPE_MSG);
      return _upb_MessageDef_InMessageSet(m)
                 ? upb_MessageDef_NestedExtension(m, 0)
                 : NULL;
    }
    default:
      break;
  }

  return NULL;
}

const upb_FieldDef* upb_DefPool_FindExtensionByName(const upb_DefPool* s,
                                                    const char* sym) {
  return upb_DefPool_FindExtensionByNameWithSize(s, sym, strlen(sym));
}

const upb_ServiceDef* upb_DefPool_FindServiceByName(const upb_DefPool* s,
                                                    const char* name) {
  return _upb_DefPool_Lookup(s, name, UPB_DEFTYPE_SERVICE);
}

const upb_ServiceDef* upb_DefPool_FindServiceByNameWithSize(
    const upb_DefPool* s, const char* name, size_t size) {
  return _upb_DefPool_Lookup2(s, name, size, UPB_DEFTYPE_SERVICE);
}

const upb_FileDef* upb_DefPool_FindFileContainingSymbol(const upb_DefPool* s,
                                                        const char* name) {
  upb_value v;
  // TODO(haberman): non-extension fields and oneofs.
  if (upb_strtable_lookup(&s->syms, name, &v)) {
    switch (_upb_DefType_Type(v)) {
      case UPB_DEFTYPE_EXT: {
        const upb_FieldDef* f = _upb_DefType_Unpack(v, UPB_DEFTYPE_EXT);
        return upb_FieldDef_File(f);
      }
      case UPB_DEFTYPE_MSG: {
        const upb_MessageDef* m = _upb_DefType_Unpack(v, UPB_DEFTYPE_MSG);
        return upb_MessageDef_File(m);
      }
      case UPB_DEFTYPE_ENUM: {
        const upb_EnumDef* e = _upb_DefType_Unpack(v, UPB_DEFTYPE_ENUM);
        return upb_EnumDef_File(e);
      }
      case UPB_DEFTYPE_ENUMVAL: {
        const upb_EnumValueDef* ev =
            _upb_DefType_Unpack(v, UPB_DEFTYPE_ENUMVAL);
        return upb_EnumDef_File(upb_EnumValueDef_Enum(ev));
      }
      case UPB_DEFTYPE_SERVICE: {
        const upb_ServiceDef* service =
            _upb_DefType_Unpack(v, UPB_DEFTYPE_SERVICE);
        return upb_ServiceDef_File(service);
      }
      default:
        UPB_UNREACHABLE();
    }
  }

  const char* last_dot = strrchr(name, '.');
  if (last_dot) {
    const upb_MessageDef* parent =
        upb_DefPool_FindMessageByNameWithSize(s, name, last_dot - name);
    if (parent) {
      const char* shortname = last_dot + 1;
      if (upb_MessageDef_FindByNameWithSize(parent, shortname,
                                            strlen(shortname), NULL, NULL)) {
        return upb_MessageDef_File(parent);
      }
    }
  }

  return NULL;
}

static void remove_filedef(upb_DefPool* s, upb_FileDef* file) {
  intptr_t iter = UPB_INTTABLE_BEGIN;
  upb_StringView key;
  upb_value val;
  while (upb_strtable_next2(&s->syms, &key, &val, &iter)) {
    const upb_FileDef* f;
    switch (_upb_DefType_Type(val)) {
      case UPB_DEFTYPE_EXT:
        f = upb_FieldDef_File(_upb_DefType_Unpack(val, UPB_DEFTYPE_EXT));
        break;
      case UPB_DEFTYPE_MSG:
        f = upb_MessageDef_File(_upb_DefType_Unpack(val, UPB_DEFTYPE_MSG));
        break;
      case UPB_DEFTYPE_ENUM:
        f = upb_EnumDef_File(_upb_DefType_Unpack(val, UPB_DEFTYPE_ENUM));
        break;
      case UPB_DEFTYPE_ENUMVAL:
        f = upb_EnumDef_File(upb_EnumValueDef_Enum(
            _upb_DefType_Unpack(val, UPB_DEFTYPE_ENUMVAL)));
        break;
      case UPB_DEFTYPE_SERVICE:
        f = upb_ServiceDef_File(_upb_DefType_Unpack(val, UPB_DEFTYPE_SERVICE));
        break;
      default:
        UPB_UNREACHABLE();
    }

    if (f == file) upb_strtable_removeiter(&s->syms, &iter);
  }
}

static const upb_FileDef* _upb_DefPool_AddFile(
    upb_DefPool* s, const google_protobuf_FileDescriptorProto* file_proto,
    const upb_MiniTable_File* layout, upb_Status* status) {
  const upb_StringView name = google_protobuf_FileDescriptorProto_name(file_proto);

  // Determine whether we already know about this file.
  {
    upb_value v;
    if (upb_strtable_lookup2(&s->files, name.data, name.size, &v)) {
      upb_Status_SetErrorFormat(status,
                                "duplicate file name " UPB_STRINGVIEW_FORMAT,
                                UPB_STRINGVIEW_ARGS(name));
      return NULL;
    }
  }

  upb_DefBuilder ctx = {
      .symtab = s,
      .layout = layout,
      .msg_count = 0,
      .enum_count = 0,
      .ext_count = 0,
      .status = status,
      .file = NULL,
      .arena = upb_Arena_New(),
      .tmp_arena = upb_Arena_New(),
  };

  if (UPB_SETJMP(ctx.err)) {
    UPB_ASSERT(!upb_Status_IsOk(status));
    if (ctx.file) {
      remove_filedef(s, ctx.file);
      ctx.file = NULL;
    }
  } else if (!ctx.arena || !ctx.tmp_arena) {
    _upb_DefBuilder_OomErr(&ctx);
  } else {
    _upb_FileDef_Create(&ctx, file_proto);
    upb_strtable_insert(&s->files, name.data, name.size,
                        upb_value_constptr(ctx.file), ctx.arena);
    UPB_ASSERT(upb_Status_IsOk(status));
    upb_Arena_Fuse(s->arena, ctx.arena);
  }

  if (ctx.arena) upb_Arena_Free(ctx.arena);
  if (ctx.tmp_arena) upb_Arena_Free(ctx.tmp_arena);
  return ctx.file;
}

const upb_FileDef* upb_DefPool_AddFile(
    upb_DefPool* s, const google_protobuf_FileDescriptorProto* file_proto,
    upb_Status* status) {
  return _upb_DefPool_AddFile(s, file_proto, NULL, status);
}

/* Include here since we want most of this file to be stdio-free. */
#include <stdio.h>

bool _upb_DefPool_LoadDefInitEx(upb_DefPool* s, const _upb_DefPool_Init* init,
                                bool rebuild_minitable) {
  /* Since this function should never fail (it would indicate a bug in upb) we
   * print errors to stderr instead of returning error status to the user. */
  _upb_DefPool_Init** deps = init->deps;
  google_protobuf_FileDescriptorProto* file;
  upb_Arena* arena;
  upb_Status status;

  upb_Status_Clear(&status);

  if (upb_DefPool_FindFileByName(s, init->filename)) {
    return true;
  }

  arena = upb_Arena_New();

  for (; *deps; deps++) {
    if (!_upb_DefPool_LoadDefInitEx(s, *deps, rebuild_minitable)) goto err;
  }

  file = google_protobuf_FileDescriptorProto_parse_ex(
      init->descriptor.data, init->descriptor.size, NULL,
      kUpb_DecodeOption_AliasString, arena);
  s->bytes_loaded += init->descriptor.size;

  if (!file) {
    upb_Status_SetErrorFormat(
        &status,
        "Failed to parse compiled-in descriptor for file '%s'. This should "
        "never happen.",
        init->filename);
    goto err;
  }

  const upb_MiniTable_File* mt = rebuild_minitable ? NULL : init->layout;
  if (!_upb_DefPool_AddFile(s, file, mt, &status)) {
    goto err;
  }

  upb_Arena_Free(arena);
  return true;

err:
  fprintf(stderr,
          "Error loading compiled-in descriptor for file '%s' (this should "
          "never happen): %s\n",
          init->filename, upb_Status_ErrorMessage(&status));
  upb_Arena_Free(arena);
  return false;
}

size_t _upb_DefPool_BytesLoaded(const upb_DefPool* s) {
  return s->bytes_loaded;
}

upb_Arena* _upb_DefPool_Arena(const upb_DefPool* s) { return s->arena; }

const upb_FieldDef* _upb_DefPool_FindExtensionByMiniTable(
    const upb_DefPool* s, const upb_MiniTable_Extension* ext) {
  upb_value v;
  bool ok = upb_inttable_lookup(&s->exts, (uintptr_t)ext, &v);
  UPB_ASSERT(ok);
  return upb_value_getconstptr(v);
}

const upb_FieldDef* upb_DefPool_FindExtensionByNumber(const upb_DefPool* s,
                                                      const upb_MessageDef* m,
                                                      int32_t fieldnum) {
  const upb_MiniTable* l = upb_MessageDef_MiniTable(m);
  const upb_MiniTable_Extension* ext = _upb_extreg_get(s->extreg, l, fieldnum);
  return ext ? _upb_DefPool_FindExtensionByMiniTable(s, ext) : NULL;
}

const upb_ExtensionRegistry* upb_DefPool_ExtensionRegistry(
    const upb_DefPool* s) {
  return s->extreg;
}

const upb_FieldDef** upb_DefPool_GetAllExtensions(const upb_DefPool* s,
                                                  const upb_MessageDef* m,
                                                  size_t* count) {
  size_t n = 0;
  intptr_t iter = UPB_INTTABLE_BEGIN;
  uintptr_t key;
  upb_value val;
  // This is O(all exts) instead of O(exts for m).  If we need this to be
  // efficient we may need to make extreg into a two-level table, or have a
  // second per-message index.
  while (upb_inttable_next2(&s->exts, &key, &val, &iter)) {
    const upb_FieldDef* f = upb_value_getconstptr(val);
    if (upb_FieldDef_ContainingType(f) == m) n++;
  }
  const upb_FieldDef** exts = malloc(n * sizeof(*exts));
  iter = UPB_INTTABLE_BEGIN;
  size_t i = 0;
  while (upb_inttable_next2(&s->exts, &key, &val, &iter)) {
    const upb_FieldDef* f = upb_value_getconstptr(val);
    if (upb_FieldDef_ContainingType(f) == m) exts[i++] = f;
  }
  *count = n;
  return exts;
}

bool _upb_DefPool_LoadDefInit(upb_DefPool* s, const _upb_DefPool_Init* init) {
  return _upb_DefPool_LoadDefInitEx(s, init, false);
}