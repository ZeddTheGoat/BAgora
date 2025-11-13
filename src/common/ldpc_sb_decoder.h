#ifndef LDPC_SB_DECODER_H_
#define LDPC_SB_DECODER_H_

#include <cstddef>
#include <cstdint>

#include "ldpc_config.h"

namespace ldpc {

struct SbDecodeResult {
  bool success;
  size_t iterations;
  size_t unsatisfied_checks;
};

SbDecodeResult SimulatedBifurcationDecode(const LDPCconfig& config,
                                          const int8_t* channel_llrs,
                                          size_t num_channel_llrs,
                                          size_t num_filler_bits,
                                          uint8_t* decoded_message_bytes);

}  // namespace ldpc

#endif  // LDPC_SB_DECODER_H_
