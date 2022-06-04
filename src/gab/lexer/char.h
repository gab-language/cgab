#ifndef BLUF_CHAR_H
#define BLUF_CHAR_H

#include "../../common/common.h"

boolean is_whitespace(u8 c);

boolean is_alpha_lower(u8 c);

boolean is_alpha_upper(u8 c);

boolean is_alpha(u8 c);

boolean is_digit(u8 c);

boolean is_comment(u8 c);

#endif
