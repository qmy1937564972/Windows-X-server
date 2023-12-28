/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * Symbol lookup.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include <windows.h>
#include "util/compiler.h"
#include "util/u_thread.h"
#include "util/simple_mtx.h"
#include "util/u_string.h"

#include "util/u_debug.h"
#include "u_debug_symbol.h"
#include "util/hash_table.h"


#if DETECT_OS_WINDOWS

#include <windows.h>
#include <stddef.h>

#include "dbghelp.h"


/**
 * SymInitialize() must be called once for each process (in this case, the
 * current process), before any of the other functions can be called.
 */
static BOOL g_bSymInitialized = false;


/**
 * Lookup the address of a DbgHelp function.
 */
static FARPROC WINAPI
getDbgHelpProcAddress(LPCSTR lpProcName)
{
   static HMODULE hModule = NULL;

   if (!hModule) {
      static bool bail = false;

      if (bail) {
         return NULL;
      }

#if DETECT_CC_GCC
      /*
       * DbgHelp does not understand the debug information generated by MinGW toolchain.
       *
       * mgwhelp.dll is a dbghelp.dll look-alike replacement, which is able to
       * understand MinGW symbols, including on 64-bit builds.
       */
      if (!hModule) {
         hModule = LoadLibraryA("mgwhelp.dll");
         if (!hModule) {
            _debug_printf("warning: mgwhelp.dll not found: symbol names will not be resolved\n"
                          "warning: download it from https://github.com/jrfonseca/drmingw/#mgwhelp\n");
         }
      }
#endif

      /*
       * Fallback to the real DbgHelp.
       */
      if (!hModule) {
         hModule = LoadLibraryA("dbghelp.dll");
      }

      if (!hModule) {
         bail = true;
         return NULL;
      }
   }

   return GetProcAddress(hModule, lpProcName);
}


/**
 * Generic macro to dispatch a DbgHelp functions.
 */
#define DBGHELP_DISPATCH(_name, _ret_type, _ret_default, _arg_types, _arg_names) \
   static _ret_type WINAPI \
   j_##_name _arg_types \
   { \
      typedef BOOL (WINAPI *PFN) _arg_types; \
      static PFN pfn = NULL; \
      if (!pfn) { \
         pfn = (PFN) getDbgHelpProcAddress(#_name); \
         if (!pfn) { \
            return _ret_default; \
         } \
      } \
      return pfn _arg_names; \
   }

DBGHELP_DISPATCH(SymInitialize,
                 BOOL, 0,
                 (HANDLE hProcess, PSTR UserSearchPath, BOOL fInvadeProcess),
                 (hProcess, UserSearchPath, fInvadeProcess))

DBGHELP_DISPATCH(SymSetOptions,
                 DWORD, false,
                 (DWORD SymOptions),
                 (SymOptions))

#ifndef _GAMING_XBOX
DBGHELP_DISPATCH(SymFromAddr,
                 BOOL, false,
                 (HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement, PSYMBOL_INFO Symbol),
                 (hProcess, Address, Displacement, Symbol))
#endif

DBGHELP_DISPATCH(SymGetLineFromAddr64,
                 BOOL, false,
                 (HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line),
                 (hProcess, dwAddr, pdwDisplacement, Line))

DBGHELP_DISPATCH(SymCleanup, BOOL, false, (HANDLE hProcess), (hProcess))


#undef DBGHELP_DISPATCH


static inline bool
debug_symbol_name_dbghelp(const void *addr, char* buf, unsigned size)
{
#ifndef _GAMING_XBOX
   DWORD64 dwAddr = (DWORD64)(uintptr_t)addr;
   HANDLE hProcess = GetCurrentProcess();

   /* General purpose buffer, to back pSymbol and other temporary stuff.
    * Must not be too memory hungry here to avoid stack overflows.
    */
   CHAR buffer[512];

   PSYMBOL_INFO pSymbol = (PSYMBOL_INFO) buffer;
   DWORD64 dwDisplacement = 0;  /* Displacement of the input address, relative to the start of the symbol */
   DWORD dwLineDisplacement = 0;
   IMAGEHLP_LINE64 Line;

   memset(pSymbol, 0, sizeof *pSymbol);
   pSymbol->SizeOfStruct = sizeof *pSymbol;
   pSymbol->MaxNameLen = sizeof buffer - offsetof(SYMBOL_INFO, Name);

   if (!g_bSymInitialized) {
      /* Some components (e.g. Java) will init dbghelp before we're loaded, causing the "invade process"
       * option to be invalid when attempting to re-init. But without it, we'd have to manually
       * load symbols for all modules in the stack. For simplicity, we can just uninit and then
       * re-"invade".
       */
      if (debug_get_bool_option("GALLIUM_SYMBOL_FORCE_REINIT", false))
         j_SymCleanup(hProcess);

      j_SymSetOptions(/* SYMOPT_UNDNAME | */ SYMOPT_LOAD_LINES);
      if (j_SymInitialize(hProcess, NULL, true)) {
         g_bSymInitialized = true;
      }
   }

   /* Lookup symbol name */
   if (!g_bSymInitialized ||
       !j_SymFromAddr(hProcess, dwAddr, &dwDisplacement, pSymbol)) {
      /*
       * We couldn't obtain symbol information.  At least tell which module the address belongs.
       */

      HMODULE hModule = NULL;

      if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                             (LPCTSTR)addr,
                             &hModule)) {
         return false;
      }

      if (GetModuleFileNameA(hModule, buffer, sizeof buffer) == sizeof buffer) {
         return false;
      }
      snprintf(buf, size, "%p at %s+0x%lx",
               addr, buffer,
               (unsigned long)((uintptr_t)addr - (uintptr_t)hModule));

      return true;
   }

   /*
    * Try to get filename and line number.
    */
   memset(&Line, 0, sizeof Line);
   Line.SizeOfStruct = sizeof Line;
   if (!j_SymGetLineFromAddr64(hProcess, dwAddr, &dwLineDisplacement, &Line)) {
      Line.FileName = NULL;
   }

   if (Line.FileName) {
      snprintf(buf, size, "%s at %s:%lu", pSymbol->Name, Line.FileName, Line.LineNumber);
   } else {
      snprintf(buf, size, "%s", pSymbol->Name);
   }

   return true;
#else
   return false;
#endif /* _GAMING_XBOX */
}

#endif /* DETECT_OS_WINDOWS */

void
debug_symbol_name(const void *addr, char* buf, unsigned size)
{
#if DETECT_OS_WINDOWS
   if (debug_symbol_name_dbghelp(addr, buf, size)) {
      return;
   }
#endif

   snprintf(buf, size, "%p", addr);
   buf[size - 1] = 0;
}

void
debug_symbol_print(const void *addr)
{
   char buf[1024];
   debug_symbol_name(addr, buf, sizeof(buf));
   debug_printf("\t%s\n", buf);
}

static struct hash_table* symbols_hash;
static simple_mtx_t symbols_mutex = SIMPLE_MTX_INITIALIZER;

const char*
debug_symbol_name_cached(const void *addr)
{
   const char* name;
   simple_mtx_lock(&symbols_mutex);
   if(!symbols_hash)
      symbols_hash = _mesa_pointer_hash_table_create(NULL);
   struct hash_entry *entry = _mesa_hash_table_search(symbols_hash, addr);
   if (!entry) {
      char buf[1024];
      debug_symbol_name(addr, buf, sizeof(buf));
      name = strdup(buf);

      entry = _mesa_hash_table_insert(symbols_hash, addr, (void*)name);
   }
   simple_mtx_unlock(&symbols_mutex);
   return entry->data;
}
