#include "ldpc_sb_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

#include "gcc_phy_ldpc_encoder_5gnr_internal.h"
#include "ldpc_config.h"
#include "utils_ldpc.h"

namespace ldpc {
namespace {

constexpr size_t kNumPuncturedCols = 2;
constexpr size_t kSbReads = 4;
constexpr size_t kDefaultSbSteps = 80;
constexpr size_t kMinSbSteps = 16;
constexpr double kTimeStep = 0.2;
constexpr double kA0 = 1.0;
constexpr double kA1 = -1.0;
constexpr double kDamping = 0.02;
constexpr double kNoiseSigma = 0.0;
constexpr double kInitScale = 1e-2;
constexpr double kClip = 2.0;
constexpr double kPenalty = 6.0;
constexpr double kFieldScale = 1.0 / 8.0;
constexpr size_t kBitFlipIters = 32;

struct DecoderKey {
  uint16_t base_graph;
  uint16_t zc;
  size_t num_rows;

  bool operator<(const DecoderKey& other) const {
    return std::tie(base_graph, zc, num_rows) <
           std::tie(other.base_graph, other.zc, other.num_rows);
  }
};

struct Topology {
  uint16_t base_graph;
  uint16_t zc;
  size_t num_rows;
  size_t info_cols;
  size_t total_cols;
  std::vector<std::vector<uint32_t>> check_to_var;
  std::vector<std::vector<uint32_t>> var_to_check;
};

std::map<DecoderKey, Topology> g_decoder_cache;
std::mutex g_decoder_mutex;

double SafeDivide(double numerator, double denominator) {
  constexpr double kEps = 1e-12;
  double denom = denominator;
  if (std::abs(denominator) < kEps) {
    denom = (denominator >= 0.0) ? kEps : -kEps;
  }
  return numerator / denom;
}

const Topology& BuildTopology(const LDPCconfig& config) {
  DecoderKey key{config.BaseGraph(), config.ExpansionFactor(),
                 config.NumRows()};

  auto it = g_decoder_cache.find(key);
  if (it != g_decoder_cache.end()) {
    return it->second;
  }

  std::lock_guard<std::mutex> lock(g_decoder_mutex);
  auto cached = g_decoder_cache.find(key);
  if (cached != g_decoder_cache.end()) {
    return cached->second;
  }

  const uint16_t base_graph = config.BaseGraph();
  const uint16_t zc = config.ExpansionFactor();
  const size_t num_rows = config.NumRows();
  const size_t info_cols =
      (base_graph == 1 ? BG1_COL_INF_NUM : BG2_COL_INF_NUM);
  const size_t total_cols = info_cols + num_rows;
  const size_t total_vars = total_cols * zc;
  const size_t num_checks = num_rows * zc;

  Topology topo;
  topo.base_graph = base_graph;
  topo.zc = zc;
  topo.num_rows = num_rows;
  topo.info_cols = info_cols;
  topo.total_cols = total_cols;
  topo.check_to_var.assign(num_checks, {});
  topo.var_to_check.assign(total_vars, {});

  const int16_t* matrix_num_per_col =
      (base_graph == 1 ? kBg1MatrixNumPerCol : kBg2MatrixNumPerCol);
  const int16_t* address =
      (base_graph == 1 ? kBg1Address : kBg2Address);
  const int16_t* shift_matrix =
      (base_graph == 1 ? kBg1HShiftMatrix : kBg2HShiftMatrix);
  const size_t max_nonzero =
      (base_graph == 1 ? BG1_NONZERO_NUM : BG2_NONZERO_NUM);
  const size_t column_total =
      (base_graph == 1 ? BG1_COL_TOTAL : BG2_COL_TOTAL);

  const uint8_t i_ls = SelectBaseMatrixEntry(zc);

  size_t nz_offset = 0;
  for (size_t col = 0; col < total_cols && col < column_total; col++) {
    const int16_t num_nz = matrix_num_per_col[col];
    for (int16_t nz = 0; nz < num_nz; nz++) {
      const size_t idx = nz_offset + static_cast<size_t>(nz);
      if (idx >= max_nonzero) {
        break;
      }
      const size_t base_row = static_cast<size_t>(address[idx]) / 64u;
      if (base_row >= num_rows) {
        continue;
      }
      const int16_t shift = shift_matrix[idx * I_LS_NUM + i_ls];
      const size_t col_offset = col * zc;
      const size_t row_offset = base_row * zc;
      for (uint16_t r = 0; r < zc; r++) {
        const size_t var_index =
            col_offset + static_cast<size_t>((r + shift + zc) % zc);
        const size_t check_index = row_offset + r;
        topo.check_to_var[check_index].push_back(var_index);
        topo.var_to_check[var_index].push_back(check_index);
      }
    }
    nz_offset += static_cast<size_t>(num_nz);
  }

  auto insert_result = g_decoder_cache.emplace(key, std::move(topo));
  return insert_result.first->second;
}

size_t ComputeUnsatisfied(const Topology& topo,
                          const std::vector<uint8_t>& bits,
                          std::vector<int>& violation_counts) {
  const size_t num_checks = topo.check_to_var.size();
  size_t unsatisfied = 0;
  std::fill(violation_counts.begin(), violation_counts.end(), 0);
  for (size_t c = 0; c < num_checks; c++) {
    uint8_t parity = 0;
    for (uint32_t var : topo.check_to_var[c]) {
      parity ^= bits[var];
    }
    if (parity != 0) {
      unsatisfied++;
      for (uint32_t var : topo.check_to_var[c]) {
        violation_counts[var]++;
      }
    }
  }
  return unsatisfied;
}

}  // namespace

SbDecodeResult SimulatedBifurcationDecode(const LDPCconfig& config,
                                          const int8_t* channel_llrs,
                                          size_t num_channel_llrs,
                                          size_t num_filler_bits,
                                          uint8_t* decoded_message_bytes) {
  const Topology& topo = BuildTopology(config);
  const size_t zc = topo.zc;
  const size_t total_vars = topo.total_cols * zc;
  const size_t num_checks = topo.check_to_var.size();
  const size_t info_cols = topo.info_cols;
  const size_t total_info_bits = info_cols * zc;
  const size_t punctured_bits = kNumPuncturedCols * zc;
  const size_t expected_channel_bits =
      total_vars - punctured_bits;

  SbDecodeResult result{};
  if (num_channel_llrs != expected_channel_bits) {
    result.success = false;
    result.iterations = 0;
    result.unsatisfied_checks = num_checks;
    return result;
  }

  std::vector<double> fields(total_vars, 0.0);

  size_t llr_index = 0;
  for (size_t col = 0; col < topo.total_cols; col++) {
    const bool punctured = (col < kNumPuncturedCols);
    for (size_t row = 0; row < zc; row++) {
      const size_t var_index = col * zc + row;
      double llr = 0.0;
      if (!punctured && llr_index < num_channel_llrs) {
        llr = static_cast<double>(channel_llrs[llr_index]) * kFieldScale;
        llr_index++;
      }
      fields[var_index] = llr;
    }
  }

  if (llr_index != num_channel_llrs) {
    result.success = false;
    result.iterations = 0;
    result.unsatisfied_checks = num_checks;
    return result;
  }

  std::vector<double> best_spins(total_vars, 1.0);
  double best_energy = std::numeric_limits<double>::infinity();
  std::vector<double> check_products(num_checks, 0.0);
  std::vector<double> gradient(total_vars, 0.0);

  std::array<uint64_t, kSbReads> seeds{};
  {
    std::seed_seq seq{static_cast<uint32_t>(config.BaseGraph()),
                      static_cast<uint32_t>(config.ExpansionFactor()),
                      static_cast<uint32_t>(num_channel_llrs),
                      static_cast<uint32_t>(num_filler_bits)};
    seq.generate(seeds.begin(), seeds.end());
  }

  const size_t sb_steps =
      (config.MaxDecoderIter() > 0)
          ? std::max<size_t>(
                static_cast<size_t>(config.MaxDecoderIter()) * 8u,
                kMinSbSteps)
          : kDefaultSbSteps;
  const bool enable_early_stop = config.EarlyTermination();

  size_t total_iterations = 0;

  for (size_t read = 0; read < kSbReads; read++) {
    std::mt19937_64 rng(seeds[read]);
    std::normal_distribution<double> gauss(0.0, 1.0);

    std::vector<double> spins(total_vars, 0.0);
    std::vector<double> velocities(total_vars, 0.0);
    std::vector<uint8_t> step_bits(total_vars, 0u);
    std::vector<int> step_violations(total_vars, 0);

    for (size_t v = 0; v < total_vars; v++) {
      double init = std::tanh(fields[v]) + kInitScale * gauss(rng);
      init = std::clamp(init, -kClip, kClip);
      spins[v] = init;
    }

    double read_best_energy = std::numeric_limits<double>::infinity();
    std::vector<double> read_best_spins(total_vars, 1.0);
    size_t steps_this_read = 0;
    bool satisfied_this_read = false;
    for (size_t step = 0; step < sb_steps; step++) {
      steps_this_read++;
      double energy = 0.0;
      for (size_t v = 0; v < total_vars; v++) {
        energy -= fields[v] * spins[v];
      }
      for (size_t check = 0; check < num_checks; check++) {
        double product = 1.0;
        for (uint32_t var_index : topo.check_to_var[check]) {
          product *= spins[var_index];
        }
        check_products[check] = product;
        energy += -kPenalty * product;
      }
      if (energy < read_best_energy) {
        read_best_energy = energy;
        read_best_spins = spins;
      }

      if (enable_early_stop) {
        for (size_t v = 0; v < total_vars; v++) {
          step_bits[v] = (spins[v] < 0.0) ? 1u : 0u;
        }
        const size_t unsatisfied_now =
            ComputeUnsatisfied(topo, step_bits, step_violations);
        if (unsatisfied_now == 0) {
          read_best_energy = energy;
          read_best_spins = spins;
          satisfied_this_read = true;
          break;
        }
      }

      for (size_t v = 0; v < total_vars; v++) {
        gradient[v] = -fields[v];
      }
      for (size_t check = 0; check < num_checks; check++) {
        const double product = check_products[check];
        const double coeff = -kPenalty;
        for (uint32_t var_index : topo.check_to_var[check]) {
          double exclude = SafeDivide(product, spins[var_index]);
          gradient[var_index] += coeff * exclude;
        }
      }

      for (size_t v = 0; v < total_vars; v++) {
        double eta = (kNoiseSigma > 0.0) ? (kNoiseSigma * gauss(rng)) : 0.0;
        double cubic = kA1 * spins[v] * spins[v] * spins[v];
        velocities[v] =
            (1.0 - kDamping * kTimeStep) * velocities[v] +
            kTimeStep * (-kA0 * spins[v] + cubic - gradient[v] + eta);
      }
      for (size_t v = 0; v < total_vars; v++) {
        spins[v] += kTimeStep * velocities[v];
        spins[v] = std::clamp(spins[v], -kClip, kClip);
      }
    }

    total_iterations += steps_this_read;

    if (read_best_energy < best_energy) {
      best_energy = read_best_energy;
      best_spins = read_best_spins;
    }
    if (enable_early_stop && satisfied_this_read) {
      break;
    }
  }

  std::vector<uint8_t> bits(total_vars, 0);
  for (size_t v = 0; v < total_vars; v++) {
    bits[v] = (best_spins[v] < 0.0) ? 1 : 0;
  }

  std::vector<int> violation_counts(total_vars, 0);
  size_t unsatisfied =
      ComputeUnsatisfied(topo, bits, violation_counts);

  size_t flip_iter = 0;
  while (unsatisfied > 0 && flip_iter < kBitFlipIters) {
    int best_score = 0;
    int best_index = -1;
    double best_metric = -1e9;
    for (size_t v = 0; v < total_vars; v++) {
      const int score = violation_counts[v];
      if (score == 0) {
        continue;
      }
      const double metric = score - 0.002 * std::abs(fields[v]);
      if (score > best_score || (score == best_score && metric > best_metric)) {
        best_score = score;
        best_metric = metric;
        best_index = static_cast<int>(v);
      }
    }
    if (best_index < 0) {
      break;
    }
    bits[static_cast<size_t>(best_index)] ^= 1u;
    unsatisfied = ComputeUnsatisfied(topo, bits, violation_counts);
    flip_iter++;
  }

  result.iterations = total_iterations + flip_iter;
  result.unsatisfied_checks = unsatisfied;
  result.success = (unsatisfied == 0);

  const size_t message_bits =
      std::min(static_cast<size_t>(config.NumCbLen()), total_info_bits);
  const size_t useful_bits =
      (num_filler_bits >= message_bits) ? 0 : (message_bits - num_filler_bits);
  const size_t output_bits = useful_bits;
  const size_t output_bytes = BitsToBytes(output_bits);
  std::fill(decoded_message_bytes, decoded_message_bytes + output_bytes, 0u);

  size_t bit_index = 0;
  for (size_t col = 0; col < info_cols; col++) {
    for (size_t row = 0; row < zc; row++) {
      if (bit_index >= message_bits) {
        break;
      }
      if (bit_index >= num_filler_bits) {
        const size_t dst_bit = bit_index - num_filler_bits;
        const uint8_t bit = bits[col * zc + row];
        decoded_message_bytes[dst_bit / 8] |= bit << (dst_bit % 8);
      }
      bit_index++;
    }
  }

  return result;
}

}  // namespace ldpc
