#include "phy_ldpc_decoder_5gnr.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ldpc_sb_decoder.h"
#include "utils_ldpc.h"

int32_t bblib_ldpc_decoder_5gnr(
    struct bblib_ldpc_decoder_5gnr_request* request,
    struct bblib_ldpc_decoder_5gnr_response* response) {
  if (request == nullptr || response == nullptr) {
    return -1;
  }
  if (request->varNodes == nullptr || response->compactedMessageBytes == nullptr) {
    return -2;
  }

  const uint16_t base_graph = static_cast<uint16_t>(request->baseGraph);
  const uint16_t zc = static_cast<uint16_t>(request->Zc);
  const size_t num_rows = static_cast<size_t>(request->nRows);
  const uint32_t num_cb_len =
      static_cast<uint32_t>(LdpcNumInputBits(base_graph, zc));
  const uint32_t num_cb_codew_len = static_cast<uint32_t>(
      LdpcNumEncodedBits(base_graph, zc, num_rows));

  LDPCconfig config(base_graph, zc, request->maxIterations,
                    request->enableEarlyTermination != 0, num_cb_len,
                    num_cb_codew_len, num_rows, /*num_blocks_in_symbol=*/1);

  const size_t num_channel_llrs =
      static_cast<size_t>(std::max<int16_t>(request->numChannelLlrs, 0));
  const size_t num_filler_bits =
      static_cast<size_t>(std::max<int16_t>(request->numFillerBits, 0));

  const auto decode_result = ldpc::SimulatedBifurcationDecode(
      config, request->varNodes, num_channel_llrs, num_filler_bits,
      response->compactedMessageBytes);

  const int16_t max_i16 = std::numeric_limits<int16_t>::max();
  response->iterations = static_cast<int16_t>(std::min<size_t>(
      decode_result.iterations, static_cast<size_t>(max_i16)));
  response->parityCheckStatus = decode_result.success
                                    ? 0
                                    : static_cast<int16_t>(std::min<size_t>(
                                          decode_result.unsatisfied_checks,
                                          static_cast<size_t>(max_i16)));

  if (response->varNodes != nullptr) {
    const size_t count = num_channel_llrs;
    std::fill(response->varNodes, response->varNodes + count, 0);
  }

  return 0;
}
