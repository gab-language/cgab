/**
 *  MIT License
 *
 *  Copyright (c) 2023 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include "gab.h"

GAB_DYNLIB_NATIVE_FN(channel, close) {
  gab_chnclose(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_arg(0));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_closed) {
  bool closed = gab_chnisclosed(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(closed));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_full) {
  bool full = gab_chnisfull(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(full));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(channel, is_empty) {
  bool empty = gab_chnisempty(gab_arg(0));

  gab_vmpush(gab_thisvm(gab), gab_bool(empty));

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_type(gab, kGAB_CHANNEL);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "close"),
              t,
              gab_snative(gab, "close", gab_mod_channel_close),
          },
          {
              gab_message(gab, "is\\closed"),
              t,
              gab_snative(gab, "is\\closed", gab_mod_channel_is_closed),
          },
          {
              gab_message(gab, "is\\full"),
              t,
              gab_snative(gab, "is\\full", gab_mod_channel_is_full),
          },
          {
              gab_message(gab, "is\\empty"),
              t,
              gab_snative(gab, "is\\empty", gab_mod_channel_is_empty),
          });

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}
