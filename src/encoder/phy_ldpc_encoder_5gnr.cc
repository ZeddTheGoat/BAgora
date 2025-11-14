#include "phy_ldpc_encoder_5gnr.h"

#include "encoder.h"

extern "C" int32_t bblib_ldpc_encoder_5gnr(
    struct bblib_ldpc_encoder_5gnr_request* request,
    struct bblib_ldpc_encoder_5gnr_response* response) {
  return avx2enc::BblibLdpcEncoder5gnr(request, response);
}
