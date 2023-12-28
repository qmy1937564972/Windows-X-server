
template = """\
/*
 * Copyright (c) 2018 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * This file was generated by aco_opcodes_h.py
 */

#ifndef _ACO_OPCODES_
#define _ACO_OPCODES_

#include <stdint.h>

namespace aco {

enum class Format : uint16_t {
% for e in Format:
   ${e.name} = ${hex(e.value)},
% endfor
};

enum class instr_class : uint8_t {
% for name in InstrClass:
   ${name.value},
% endfor
   count,
};

<% opcode_names = sorted(opcodes.keys()) %>

enum class aco_opcode : uint16_t {
% for name in opcode_names:
   ${name},
% endfor
   last_opcode = ${opcode_names[-1]},
   num_opcodes = last_opcode + 1
};

}
#endif /* _ACO_OPCODES_ */"""

from aco_opcodes import opcodes, InstrClass, Format
from mako.template import Template

print(Template(template).render(opcodes=opcodes, InstrClass=InstrClass, Format=Format))
