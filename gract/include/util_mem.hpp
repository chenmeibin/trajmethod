/***
BSD 2-Clause License

Copyright (c) 2018, Adrián
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/


//
// Created by Adrián on 10/01/2019.
//

#ifndef UTIL_MEM_HPP
#define UTIL_MEM_HPP

#include <unistd.h>

namespace util {

    namespace memory {

        int64_t total_memory_bytes() {
#if USE_SYSCTL_HW_MEMSIZE
            uint64_t mem;
            size_t len = sizeof(mem);
            sysctlbyname("hw.memsize", &mem, &len, NULL, 0);
            unsigned phys_pages = mem/sysconf(_SC_PAGE_SIZE);
#else
            return sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
#endif
        }

        int64_t total_memory_megabytes(){
            return (total_memory_bytes() >> 20);
        }

        int64_t total_memory_gigabytes(){
            return (total_memory_bytes() >> 30);
        }
    }
}

#endif //UTIL_MEM_HPP
