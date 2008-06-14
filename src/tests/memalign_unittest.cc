// Copyright (c) 2004, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat
//
// Check memalign related routines.
//
// We can't really do a huge amount of checking, but at the very
// least, the following code checks that return values are properly
// aligned, and that writing into the objects works.

#include "config_for_unittests.h"

// Complicated ordering requirements.  tcmalloc.h defines (indirectly)
// _POSIX_C_SOURCE, which it needs so stdlib.h defines posix_memalign.
// unistd.h, on the other hand, requires _POSIX_C_SOURCE to be unset,
// at least on Mac OS X, in order to define getpagesize.  The solution
// is to #include unistd.h first.  This is safe because unistd.h
// doesn't sub-include stdlib.h, so we'll still get posix_memalign
// when we #include stdlib.h.  Blah.
#ifdef HAVE_UNISTD_H
#include <unistd.h>        // for getpagesize()
#endif
#include "tcmalloc.h"      // must come early, to pick up posix_memalign
#include <stdlib.h>        // defines posix_memalign
#include <stdio.h>         // for the printf at the end
#ifdef HAVE_STDINT_H
#include <stdint.h>        // for uintptr_t
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>        // for getpagesize()
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include "base/basictypes.h"
#include "base/logging.h"
#include "tests/testutil.h"


// Return the next interesting size/delta to check.  Returns -1 if no more.
static int NextSize(int size) {
  if (size < 100) {
    return size+1;
  } else if (size < 1048576) {
    // Find next power of two
    int power = 1;
    while (power < size) {
      power <<= 1;
    }

    // Yield (power-1, power, power+1)
    if (size < power-1) {
      return power-1;
    } else if (size == power-1) {
      return power;
    } else {
      assert(size == power);
      return power+1;
    }
  } else {
    return -1;
  }
}

// Shortform for cast
static uintptr_t Number(void* p) {
  return reinterpret_cast<uintptr_t>(p);
}

// Check alignment
static void CheckAlignment(void* p, int align) {
  if ((Number(p) & (align-1)) != 0)
    LOG(FATAL, "wrong alignment; wanted 0x%x; got %p\n", align, p);
}

// Fill a buffer of the specified size with a predetermined pattern
static void Fill(void* p, int n, char seed) {
  unsigned char* buffer = reinterpret_cast<unsigned char*>(p);
  for (int i = 0; i < n; i++) {
    buffer[i] = ((seed + i) & 0xff);
  }
}

// Check that the specified buffer has the predetermined pattern
// generated by Fill()
static bool Valid(const void* p, int n, char seed) {
  const unsigned char* buffer = reinterpret_cast<const unsigned char*>(p);
  for (int i = 0; i < n; i++) {
    if (buffer[i] != ((seed + i) & 0xff)) {
      return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  SetTestResourceLimit();

  // Try allocating data with a bunch of alignments and sizes
  for (int a = 1; a < 1048576; a *= 2) {
    for (int s = 0; s != -1; s = NextSize(s)) {
      void* ptr = memalign(a, s);
      CheckAlignment(ptr, a);
      Fill(ptr, s, 'x');
      CHECK(Valid(ptr, s, 'x'));
      free(ptr);

      if ((a >= sizeof(void*)) && ((a & (a-1)) == 0)) {
        CHECK(posix_memalign(&ptr, a, s) == 0);
        CheckAlignment(ptr, a);
        Fill(ptr, s, 'y');
        CHECK(Valid(ptr, s, 'y'));
        free(ptr);
      }
    }
  }

  {
    // Check various corner cases
    void* p1 = memalign(1<<20, 1<<19);
    void* p2 = memalign(1<<19, 1<<19);
    void* p3 = memalign(1<<21, 1<<19);
    CheckAlignment(p1, 1<<20);
    CheckAlignment(p2, 1<<19);
    CheckAlignment(p3, 1<<21);
    Fill(p1, 1<<19, 'a');
    Fill(p2, 1<<19, 'b');
    Fill(p3, 1<<19, 'c');
    CHECK(Valid(p1, 1<<19, 'a'));
    CHECK(Valid(p2, 1<<19, 'b'));
    CHECK(Valid(p3, 1<<19, 'c'));
    free(p1);
    free(p2);
    free(p3);
  }

  {
    // posix_memalign
    void* ptr;
    CHECK(posix_memalign(&ptr, 0, 1) == EINVAL);
    CHECK(posix_memalign(&ptr, sizeof(void*)/2, 1) == EINVAL);
    CHECK(posix_memalign(&ptr, sizeof(void*)+1, 1) == EINVAL);
    CHECK(posix_memalign(&ptr, 4097, 1) == EINVAL);

    // Make sure overflow is returned as ENOMEM
    for (size_t s = 0; ; s += (10 << 20)) {
      int r = posix_memalign(&ptr, 1024, s);
      if (r == ENOMEM) break;
      CHECK(r == 0);
      free(ptr);
    }
  }

  const int pagesize = getpagesize();
  {
    // valloc
    for (int s = 0; s != -1; s = NextSize(s)) {
      void* p = valloc(s);
      CheckAlignment(p, pagesize);
      Fill(p, pagesize, 'v');
      CHECK(Valid(p, pagesize, 'v'));
      free(p);
    }
  }

  {
    // pvalloc
    for (int s = 0; s != -1; s = NextSize(s)) {
      void* p = pvalloc(s);
      CheckAlignment(p, pagesize);
      int alloc_needed = ((s + pagesize - 1) / pagesize) * pagesize;
      Fill(p, alloc_needed, 'x');
      CHECK(Valid(p, alloc_needed, 'x'));
      free(p);
    }

    // should be safe to write upto a page in pvalloc(0) region
    void* p = pvalloc(0);
    Fill(p, pagesize, 'y');
    CHECK(Valid(p, pagesize, 'y'));
    free(p);
  }

  printf("PASS\n");
  return 0;
}
