#ifndef PHY_LDPC_DECODER_5GNR_H_
#define PHY_LDPC_DECODER_5GNR_H_

#include <cstdint>

struct bblib_ldpc_decoder_5gnr_request {
  int8_t* varNodes;
  int16_t numChannelLlrs;
  int16_t numFillerBits;
  int16_t maxIterations;
  int16_t enableEarlyTermination;
  int16_t Zc;
  int16_t baseGraph;
  int16_t nRows;
};

struct bblib_ldpc_decoder_5gnr_response {
  int16_t* varNodes;
  uint8_t* compactedMessageBytes;
  int32_t numMsgBits;
  int16_t iterations;
  int16_t parityCheckStatus;
};

int32_t bblib_ldpc_decoder_5gnr(
    struct bblib_ldpc_decoder_5gnr_request* request,
    struct bblib_ldpc_decoder_5gnr_response* response);

#endif  // PHY_LDPC_DECODER_5GNR_H_
