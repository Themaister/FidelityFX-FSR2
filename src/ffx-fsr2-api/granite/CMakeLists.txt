# Copyright (c) 2022 Hans-Kristian Arntzen
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

add_granite_internal_lib(granite-fsr2
        ../ffx_assert.cpp ../ffx_assert.h
        ../ffx_error.h
        ../ffx_fsr2.cpp ../ffx_fsr2.h
        ../ffx_fsr2_interface.h
        ../ffx_fsr2_maximum_bias.h
        ../ffx_fsr2_private.h
        ../ffx_types.h
        ../ffx_util.h
        ffx_fsr2_granite_static_reflection.h
        ffx_fsr2_granite.cpp ffx_fsr2_granite.h)

target_include_directories(granite-fsr2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(granite-fsr2 PRIVATE granite-vulkan)