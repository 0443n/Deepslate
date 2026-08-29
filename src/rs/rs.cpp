#include "rs/rs.h"

bool rsAbiOk() {
    return ds_abi_check(1, 2, 3, 4) == 1234;
}
