#ifndef AISCRIPT_STD_RNG_H
#define AISCRIPT_STD_RNG_H

#include "types.h"

as_value_t* as_std_gen_rng(as_value_t** args, size_t arg_count, void* userdata);
as_value_t* as_std_rng_call(as_value_t** args, size_t arg_count, void* userdata);

#endif
