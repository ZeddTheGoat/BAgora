#ifndef PHY_LDPC_ENCODER_5GNR_H_
#define PHY_LDPC_ENCODER_5GNR_H_

#include <cstddef>
#include <cstdint>

static constexpr size_t kLdpcEncoderMaxCodeblocks = 64;

struct bblib_ldpc_encoder_5gnr_request {
  int8_t* input[kLdpcEncoderMaxCodeblocks];
  uint16_t Zc;
  uint16_t baseGraph;
  uint16_t nRows;
  int16_t numberCodeblocks;
};

struct bblib_ldpc_encoder_5gnr_response {
  int8_t* output[kLdpcEncoderMaxCodeblocks];
};

#ifdef __cplusplus
extern "C" {
#endif
int32_t bblib_ldpc_encoder_5gnr(
    struct bblib_ldpc_encoder_5gnr_request* request,
    struct bblib_ldpc_encoder_5gnr_response* response);
#ifdef __cplusplus
}
#endif

#endif  // PHY_LDPC_ENCODER_5GNR_H_
