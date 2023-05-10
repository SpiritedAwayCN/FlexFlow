/* Copyright 2023 CMU, Facebook, LANL, MIT, NVIDIA, and Stanford (alphabetical)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "flexflow/inference.h"
#include "flexflow/utils/cuda_helper.h"

namespace FlexFlow {

using namespace Legion;

void RequestManager::load_tokens_task(
    Task const *task,
    std::vector<PhysicalRegion> const &regions,
    Context ctx,
    Runtime *runtime) {
  assert(regions.size() == 1);
  assert(task->regions.size() == 1);

  BatchConfig const batch_config = *((BatchConfig *)task->args);
  BatchConfig::TokenId dram_copy[BatchConfig::MAX_NUM_TOKENS];
  for (int i = 0; i < batch_config.num_tokens; i++) {
    dram_copy[i] = batch_config.tokensInfo[i].token_id;
  }
  TokenId *fb_ptr = helperGetTensorPointerWO<TokenId>(
      regions[0], task->regions[0], FID_DATA, ctx, runtime);
  Domain domain = runtime->get_index_space_domain(
      ctx, task->regions[0].region.get_index_space());
  assert(batch_config.num_tokens <= domain.get_volume());
  cudaStream_t stream;
  checkCUDA(get_legion_stream(&stream));
  checkCUDA(cudaMemcpyAsync(fb_ptr,
                            dram_copy,
                            sizeof(TokenId) * batch_config.num_tokens,
                            cudaMemcpyHostToDevice,
                            stream));
}

void RequestManager::load_positions_task(
    Task const *task,
    std::vector<PhysicalRegion> const &regions,
    Context ctx,
    Runtime *runtime) {
  assert(regions.size() == 1);
  assert(task->regions.size() == 1);

  // int const offset = *((int *)task->args);

  BatchConfig const batch_config = *((BatchConfig *)task->args);
  int offset = 2;
  int *pos_ptr = helperGetTensorPointerWO<int>(
      regions[0], task->regions[0], FID_DATA, ctx, runtime);
  Domain domain = runtime->get_index_space_domain(
      ctx, task->regions[0].region.get_index_space());
  int dram_copy[BatchConfig::MAX_NUM_TOKENS];

  for (int i = 0; i < batch_config.num_tokens; i++) {
    dram_copy[i] = batch_config.tokensInfo[i].abs_depth_in_request + offset;
  }
  // int max_seq_length = domain.hi()[0] - domain.lo()[0] + 1;
  // int batch_size = domain.hi()[1] - domain.lo()[1] + 1;
  // int volume = max_seq_length * batch_size;
  // int* data = (int *)malloc(sizeof(int) * volume);
  // int* back_data = (int *)malloc(sizeof(int) * volume);
  // // printf("max_seq laneght %d, x %d", max_seq_length, batch_size);
  // for(int i = 0; i < volume; i++){
  //   data[i] = i % max_seq_length + offset;
  //   printf("data %d, value %d", i,  i % max_seq_length + offset);
  // }

  cudaStream_t stream;
  checkCUDA(get_legion_stream(&stream));
  checkCUDA(cudaMemcpyAsync(pos_ptr,
                            dram_copy,
                            sizeof(int) * batch_config.num_tokens,
                            cudaMemcpyHostToDevice,
                            stream));
}

}; // namespace FlexFlow
