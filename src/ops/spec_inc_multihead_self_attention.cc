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

#include "flexflow/ops/spec_inc_multihead_self_attention.h"
#include "flexflow/model.h"
#if defined(FF_USE_CUDA) || defined(FF_USE_HIP_CUDA)
#include "flexflow/utils/cuda_helper.h"
#else
#include "flexflow/utils/hip_helper.h"
#endif
#include "flexflow/utils/hash_utils.h"
#include "legion/legion_utilities.h"
#ifdef INFERENCE_TESTS
#include <torch/torch.h>
using namespace at::indexing;
#endif

namespace FlexFlow {

// declare Legion names
using Legion::ArgumentMap;
using Legion::Context;
using Legion::coord_t;
using Legion::Domain;
using Legion::FutureMap;
using Legion::IndexLauncher;
using Legion::Machine;
using Legion::Memory;
using Legion::PhysicalRegion;
using Legion::Predicate;
using Legion::Rect;
using Legion::RegionRequirement;
using Legion::Runtime;
using Legion::Task;
using Legion::TaskArgument;
using Legion::TaskLauncher;
using PCG::Node;

bool SpecIncMultiHeadSelfAttentionParams::is_valid(
    ParallelTensorShape const &input) const {
  bool is_valid = input.is_valid();
  return is_valid;
}

Tensor
    FFModel::spec_inc_multihead_self_attention(const Tensor input,
                                               int embed_dim,
                                               int num_heads,
                                               int kdim,
                                               int vdim,
                                               float dropout,
                                               bool bias,
                                               bool add_bias_kv,
                                               bool add_zero_attn,
                                               Initializer *kernel_initializer,
                                               bool apply_rotary_embedding,
                                               char const *name) {
  // Currently assume that
  Layer *li = new Layer(this,
                        OP_SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION,
                        DT_FLOAT,
                        name,
                        1 /*inputs*/,
                        1 /*weights*/,
                        1 /*outputs*/,
                        input);
  {
    int numdims = input->num_dims;
    int dims[MAX_TENSOR_DIM];
    for (int i = 0; i < numdims; i++) {
      dims[i] = input->dims[i];
    }
    dims[0] = embed_dim;
    li->outputs[0] = create_tensor_legion_ordering(
        numdims, dims, DT_FLOAT, li, 0, true /*create_grad*/);
  }
  {
    // Compute weight size
    int qProjSize = kdim, kProjSize = kdim, vProjSize = kdim,
        oProjSize = embed_dim;
    int qSize = input->dims[0], kSize = input->dims[0], vSize = input->dims[0];
    int qParas = qProjSize * qSize;
    int kParas = kProjSize * kSize;
    int vParas = vProjSize * vSize;
    int oParas = oProjSize * (vProjSize > 0 ? vProjSize : vSize);
    int dims[2] = {qParas + kParas + vParas + oParas, num_heads};
    li->weights[0] = create_weight_legion_ordering(2,
                                                   dims,
                                                   DT_FLOAT,
                                                   li,
                                                   true /*create_grad*/,
                                                   kernel_initializer,
                                                   CHOSEN_SYNC_TYPE);
  }
  li->data_type = DT_FLOAT;
  li->add_int_property("embed_dim", embed_dim);
  li->add_int_property("num_heads", num_heads);
  li->add_int_property("kdim", kdim);
  li->add_int_property("vdim", vdim);
  li->add_int_property("bias", bias);
  li->add_int_property("add_bias_kv", add_bias_kv);
  li->add_int_property("add_zero_attn", add_zero_attn);
  li->add_float_property("dropout", dropout);
  li->add_int_property("apply_rotary_embedding", apply_rotary_embedding);
  layers.push_back(li);
  return li->outputs[0];
}

Op *SpecIncMultiHeadSelfAttention::create_operator_from_layer(
    FFModel &model,
    Layer const *layer,
    std::vector<ParallelTensor> const &inputs) {

  std::cout << "spec create operator: " << layer->name << "\n";
  long long value;
  layer->get_int_property("embed_dim", value);
  int embed_dim = value;
  layer->get_int_property("num_heads", value);
  int num_heads = value;
  layer->get_int_property("kdim", value);
  int kdim = value;
  layer->get_int_property("vdim", value);
  int vdim = value;
  float dropout;
  layer->get_float_property("dropout", dropout);
  layer->get_int_property("bias", value);
  bool bias = (bool)value;
  layer->get_int_property("add_bias_kv", value);
  bool add_bias_kv = (bool)value;
  layer->get_int_property("add_zero_attn", value);
  bool add_zero_attn = (bool)value;
  layer->get_int_property("apply_rotary_embedding", value);
  bool apply_rotary_embedding = (bool)value;
  return new SpecIncMultiHeadSelfAttention(model,
                                           layer->layer_guid,
                                           inputs[0],
                                           embed_dim,
                                           num_heads,
                                           kdim,
                                           vdim,
                                           dropout,
                                           bias,
                                           add_bias_kv,
                                           add_zero_attn,
                                           apply_rotary_embedding,
                                           false /*allocate_weights*/,
                                           layer->name);
}

SpecIncMultiHeadSelfAttention::SpecIncMultiHeadSelfAttention(
    FFModel &model,
    LayerID const &_layer_guid,
    const ParallelTensor _input,
    int _embed_dim,
    int _num_heads,
    int _kdim,
    int _vdim,
    float _dropout,
    bool _bias,
    bool _add_bias_kv,
    bool _add_zero_attn,
    bool _apply_rotary_embedding,
    bool allocate_weights,
    char const *name)
    // Initializer* _bias_initializer)
    : Op(model,
         OP_SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION,
         DT_FLOAT,
         name,
         1 /*inputs*/,
         1 /*weights*/,
         1 /*outputs*/,
         _input),
      num_heads(_num_heads), dropout(_dropout), bias(_bias),
      add_bias_kv(_add_bias_kv), add_zero_attn(_add_zero_attn),
      apply_rotary_embedding(_apply_rotary_embedding),
      qSize(_input->dims[0].size), kSize(_input->dims[0].size),
      vSize(_input->dims[0].size), qProjSize(_kdim), kProjSize(_kdim),
      vProjSize(_vdim), oProjSize(_embed_dim),
      qoSeqLength(_input->dims[1].size), kvSeqLength(_input->dims[1].size) {
  // overwrite layer_guid
  layer_guid = _layer_guid;

  numOutputs = 1;
  int numdim = _input->num_dims;
  ParallelDim dims[MAX_TENSOR_DIM];
  for (int i = 0; i < numdim; i++) {
    dims[i] = _input->dims[i];
  }
  dims[0].size = _embed_dim;
  // Currently require no parallelism along this dim
  assert(dims[0].degree == 1);
  if (allocate_weights) {
    // Create weight tensor
    int num_dims = inputs[0]->num_dims;
    // Compute weight size
    int qParas = this->qProjSize * this->qSize;
    int kParas = this->kProjSize * this->kSize;
    int vParas = this->vProjSize * this->vSize;
    int oParas =
        this->oProjSize * (this->vProjSize > 0 ? this->vProjSize : this->vSize);
    ParallelDim dims[3];
    dims[0] = inputs[0]->dims[num_dims - 2];
    dims[0].size = dims[0].degree;
    dims[1] = inputs[0]->dims[num_dims - 1];
    dims[1].size = this->num_heads;
    dims[2].size = qParas + kParas + vParas + oParas;
    dims[2].degree = 1;
    dims[2].parallel_idx = -1;
    int seed = std::rand();
    Initializer *initializer = new GlorotUniform(seed);
#ifdef USE_NCCL
    ParameterSyncType comm_type = ParameterSyncType::NCCL;
#else
    ParameterSyncType comm_type = ParameterSyncType::PS;
#endif
    weights[0] = model.create_parallel_weight<3>(dims,
                                                 DT_FLOAT,
                                                 NULL /*owner_op*/,
                                                 true /*create_grad*/,
                                                 initializer,
                                                 comm_type);
  }

  outputs[0] = model.create_parallel_tensor_legion_ordering(
      _input->num_dims, dims, DT_FLOAT, this);
  /* for (int i = 0; i < numdim; i++) { */
  /*   register_output_input_parallel_dims(outputs[0], i, inputs[0], i); */
  /* } */
  /* // Check correctness */
  /* assert(check_output_input_weight_parallel_dims()); */
}

SpecIncMultiHeadSelfAttention::SpecIncMultiHeadSelfAttention(
    FFModel &model,
    const ParallelTensor _input,
    const ParallelTensor _weight,
    int _embed_dim,
    int _num_heads,
    int _kdim,
    int _vdim,
    float _dropout,
    bool _bias,
    bool _add_bias_kv,
    bool _add_zero_attn,
    bool _apply_rotary_embedding,
    bool allocate_weights,
    char const *name)
    // Initializer* _bias_initializer)
    : Op(model,
         OP_SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION,
         DT_FLOAT,
         name,
         1 /*inputs*/,
         1 /*weights*/,
         1 /*outputs*/,
         _input,
         _weight),
      num_heads(_num_heads), dropout(_dropout), bias(_bias),
      add_bias_kv(_add_bias_kv), add_zero_attn(_add_zero_attn),
      apply_rotary_embedding(_apply_rotary_embedding),
      qSize(_input->dims[0].size), kSize(_input->dims[0].size),
      vSize(_input->dims[0].size), qProjSize(_kdim), kProjSize(_kdim),
      vProjSize(_vdim), oProjSize(_embed_dim),
      qoSeqLength(_input->dims[1].size), kvSeqLength(_input->dims[1].size)
// bias_initializer(_bias_initializer)
{
  numOutputs = 1;
  int numdim = _input->num_dims;
  ParallelDim dims[MAX_TENSOR_DIM];
  for (int i = 0; i < numdim; i++) {
    dims[i] = _input->dims[i];
  }
  dims[0].size = _embed_dim;
  // Currently require no parallelism along this dim
  assert(dims[0].degree == 1);
  if (allocate_weights) {
    // Create weight tensor
    int num_dims = inputs[0]->num_dims;
    // Compute weight size
    int qParas = this->qProjSize * this->qSize;
    int kParas = this->kProjSize * this->kSize;
    int vParas = this->vProjSize * this->vSize;
    int oParas =
        this->oProjSize * (this->vProjSize > 0 ? this->vProjSize : this->vSize);
    ParallelDim dims[3];
    dims[0] = inputs[0]->dims[num_dims - 2];
    dims[0].size = dims[0].degree;
    dims[1] = inputs[0]->dims[num_dims - 1];
    dims[1].size = this->num_heads;
    dims[2].size = qParas + kParas + vParas + oParas;
    int seed = std::rand();
    Initializer *initializer = new GlorotUniform(seed);
#ifdef USE_NCCL
    ParameterSyncType comm_type = ParameterSyncType::NCCL;
#else
    ParameterSyncType comm_type = ParameterSyncType::PS;
#endif
    weights[0] = model.create_parallel_weight<3>(dims,
                                                 DT_FLOAT,
                                                 NULL /*owner_op*/,
                                                 true /*create_grad*/,
                                                 initializer,
                                                 comm_type);
  }
  outputs[0] = model.create_parallel_tensor_legion_ordering(
      _input->num_dims, dims, DT_FLOAT, this);

  /* for (int i = 0; i < numdim; i++) { */
  /*   register_output_input_parallel_dims(outputs[0], i, inputs[0], i); */
  /* } */
  /* register_output_weight_parallel_dims(outputs[0], numdim-1, _weight, 1); */
  /* register_output_weight_parallel_dims(outputs[0], numdim-2, _weight, 2); */
  // Check correctness
  /* assert(check_output_input_weight_parallel_dims()); */
}

SpecIncMultiHeadSelfAttention::SpecIncMultiHeadSelfAttention(
    FFModel &model,
    SpecIncMultiHeadSelfAttention const &other,
    const ParallelTensor input,
    bool allocate_weights)
    : SpecIncMultiHeadSelfAttention(model,
                                    other.layer_guid,
                                    input,
                                    other.oProjSize,
                                    other.num_heads,
                                    other.qProjSize,
                                    other.vProjSize,
                                    other.dropout,
                                    other.bias,
                                    other.add_bias_kv,
                                    other.add_zero_attn,
                                    other.apply_rotary_embedding,
                                    allocate_weights,
                                    other.name) {}

SpecIncMultiHeadSelfAttention::SpecIncMultiHeadSelfAttention(
    FFModel &model,
    SpecIncMultiHeadSelfAttentionParams const &params,
    ParallelTensor const &input,
    bool allocate_weights,
    char const *name)
    : SpecIncMultiHeadSelfAttention(model,
                                    params.layer_guid,
                                    input,
                                    params.embed_dim,
                                    params.num_heads,
                                    params.kdim,
                                    params.vdim,
                                    params.dropout,
                                    params.bias,
                                    params.add_bias_kv,
                                    params.add_zero_attn,
                                    params.apply_rotary_embedding,
                                    allocate_weights,
                                    name) {}

void SpecIncMultiHeadSelfAttention::init_inference(
    FFModel const &ff,
    std::vector<ParallelTensor> const &batch_inputs,
    std::vector<ParallelTensor> const &batch_outputs,
    MachineView const *mv) {
  assert(check_output_input_weight_same_parallel_is());
  parallel_is = batch_outputs[0]->parallel_is;
  ArgumentMap argmap;
  Context ctx = ff.config.lg_ctx;
  Runtime *runtime = ff.config.lg_hlr;
  MachineView const *view = mv ? mv : &batch_outputs[0]->machine_view;
  size_t machine_view_hash = view->hash();
  set_argumentmap_for_init_inference(ff, argmap, batch_outputs[0]);
  IndexLauncher launcher(
      SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION_INIT_TASK_ID,
      parallel_is,
      TaskArgument(this, sizeof(SpecIncMultiHeadSelfAttention)),
      argmap,
      Predicate::TRUE_PRED,
      false /*must*/,
      0 /*mapper_id*/,
      machine_view_hash);
  launcher.add_region_requirement(RegionRequirement(batch_inputs[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    batch_inputs[0]->region));
  launcher.add_field(0, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(weights[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    weights[0]->region));
  launcher.add_field(1, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(batch_outputs[0]->part,
                                                    0 /*projection id*/,
                                                    WRITE_ONLY,
                                                    EXCLUSIVE,
                                                    batch_outputs[0]->region));
  launcher.add_field(2, FID_DATA);
  FutureMap fm = runtime->execute_index_space(ctx, launcher);
  fm.wait_all_results();
  set_opmeta_from_futuremap_inference(ff, fm, batch_outputs[0]);
}

void SpecIncMultiHeadSelfAttention::init(FFModel const &ff) {
  assert(check_output_input_weight_same_parallel_is());
  parallel_is = outputs[0]->parallel_is;
  ArgumentMap argmap;
  Context ctx = ff.config.lg_ctx;
  Runtime *runtime = ff.config.lg_hlr;
  set_argumentmap_for_init(ff, argmap);
  IndexLauncher launcher(
      SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION_INIT_TASK_ID,
      parallel_is,
      TaskArgument(this, sizeof(SpecIncMultiHeadSelfAttention)),
      argmap,
      Predicate::TRUE_PRED,
      false /*must*/,
      0 /*mapper_id*/,
      outputs[0]->machine_view.hash());
  launcher.add_region_requirement(RegionRequirement(inputs[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    inputs[0]->region));
  launcher.add_field(0, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(weights[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    weights[0]->region));
  launcher.add_field(1, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(outputs[0]->part,
                                                    0 /*projection id*/,
                                                    WRITE_ONLY,
                                                    EXCLUSIVE,
                                                    outputs[0]->region));
  launcher.add_field(2, FID_DATA);
  FutureMap fm = runtime->execute_index_space(ctx, launcher);
  fm.wait_all_results();
  set_opmeta_from_futuremap(ff, fm);
}

/*
  regions[0](I): input
  regions[1](I): weight
  regions[2](O): output
*/
OpMeta *SpecIncMultiHeadSelfAttention::init_task(
    Task const *task,
    std::vector<PhysicalRegion> const &regions,
    Context ctx,
    Runtime *runtime) {
  SpecIncMultiHeadSelfAttention const *attn =
      (SpecIncMultiHeadSelfAttention *)task->args;
  FFHandler handle = *((FFHandler const *)task->local_args);

  GenericTensorAccessorR input = helperGetGenericTensorAccessorRO(
      DT_FLOAT, regions[0], task->regions[0], FID_DATA, ctx, runtime);
  GenericTensorAccessorR weight = helperGetGenericTensorAccessorRO(
      DT_FLOAT, regions[1], task->regions[1], FID_DATA, ctx, runtime);
  GenericTensorAccessorW output = helperGetGenericTensorAccessorWO(
      DT_FLOAT, regions[2], task->regions[2], FID_DATA, ctx, runtime);

  int num_samples = input.domain.hi()[2] - input.domain.lo()[2] + 1;
  assert(attn->qoSeqLength == input.domain.hi()[1] - input.domain.lo()[1] + 1);
  assert(attn->kvSeqLength == input.domain.hi()[1] - input.domain.lo()[1] + 1);
  int num_heads = weight.domain.hi()[1] - weight.domain.lo()[1] + 1;
  assert(attn->oProjSize == output.domain.hi()[0] - output.domain.lo()[0] + 1);

  Memory gpu_mem = Machine::MemoryQuery(Machine::get_machine())
                       .only_kind(Memory::GPU_FB_MEM)
                       .best_affinity_to(task->target_proc)
                       .first();
  SpecIncMultiHeadSelfAttentionMeta *m = new SpecIncMultiHeadSelfAttentionMeta(
      handle, attn, weight.get_float_ptr(), gpu_mem, num_samples, num_heads);
  m->profiling = attn->profiling;
  assert(weight.domain.get_volume() * sizeof(float) == m->weightSize);
  return m;
}

void SpecIncMultiHeadSelfAttention::forward(FFModel const &ff) {
  // SpecIncMultiHeadSelfAttention doesn't support forward
  assert(false);
}

FutureMap SpecIncMultiHeadSelfAttention::inference(
    FFModel const &ff,
    BeamSearchBatchConfig const &bc,
    std::vector<ParallelTensor> const &batch_inputs,
    std::vector<ParallelTensor> const &batch_outputs,
    MachineView const *mv) {
  ArgumentMap argmap;
  Context ctx = ff.config.lg_ctx;
  Runtime *runtime = ff.config.lg_hlr;
  parallel_is = batch_outputs[0]->parallel_is;
  MachineView const *view = mv ? mv : &batch_outputs[0]->machine_view;
  set_argumentmap_for_inference(ff, argmap, batch_outputs[0]);
  size_t machine_view_hash = view->hash();
  int idx = 0;
  printf("BeamSearchBatchConfig, num_tokens: %d, num_requests: %d\n",
         bc.num_tokens,
         bc.num_requests);
  IndexLauncher launcher(SPECULATIVE_INC_MULTIHEAD_SELF_ATTENTION_INF_TASK_ID,
                         parallel_is,
                         TaskArgument(&bc, sizeof(BeamSearchBatchConfig)),
                         argmap,
                         Predicate::TRUE_PRED,
                         false /*must*/,
                         0 /*mapper_id*/,
                         machine_view_hash);
  launcher.add_region_requirement(RegionRequirement(batch_inputs[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    batch_inputs[0]->region));
  launcher.add_field(idx++, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(weights[0]->part,
                                                    0 /*projection id*/,
                                                    READ_ONLY,
                                                    EXCLUSIVE,
                                                    weights[0]->region));
  launcher.add_field(idx++, FID_DATA);
  launcher.add_region_requirement(RegionRequirement(batch_outputs[0]->part,
                                                    0 /*projection id*/,
                                                    WRITE_ONLY,
                                                    EXCLUSIVE,
                                                    batch_outputs[0]->region));
  launcher.add_field(idx++, FID_DATA);
  return runtime->execute_index_space(ctx, launcher);
}

/*
  regions[0](I): input
  regions[3](I): weight
  regions[4](O): output
*/
void SpecIncMultiHeadSelfAttention::inference_task(
    Task const *task,
    std::vector<PhysicalRegion> const &regions,
    Context ctx,
    Runtime *runtime) {
  assert(regions.size() == 3);
  assert(task->regions.size() == regions.size());

  BeamSearchBatchConfig const *bc = (BeamSearchBatchConfig *)task->args;
  SpecIncMultiHeadSelfAttentionMeta const *m =
      *((SpecIncMultiHeadSelfAttentionMeta **)task->local_args);

  GenericTensorAccessorR input = helperGetGenericTensorAccessorRO(
      m->input_type[0], regions[0], task->regions[0], FID_DATA, ctx, runtime);
  GenericTensorAccessorR weight = helperGetGenericTensorAccessorRO(
      m->weight_type[0], regions[1], task->regions[1], FID_DATA, ctx, runtime);
  GenericTensorAccessorW output = helperGetGenericTensorAccessorWO(
      m->output_type[0], regions[2], task->regions[2], FID_DATA, ctx, runtime);

  Domain input_domain = runtime->get_index_space_domain(
      ctx, task->regions[0].region.get_index_space());
  Domain weight_domain = runtime->get_index_space_domain(
      ctx, task->regions[1].region.get_index_space());
  Domain output_domain = runtime->get_index_space_domain(
      ctx, task->regions[2].region.get_index_space());

  assert(input_domain.get_dim() == 4);
  assert(weight_domain.get_dim() == 3);
  assert(output_domain.get_dim() == 4);

  /* print_tensor<float>(input.get_float_ptr(),
                      input_domain.get_volume(),
                      "[Attention:forward:query]"); */

  SpecIncMultiHeadSelfAttention::inference_kernel_wrapper(
      m,
      bc,
      input.get_float_ptr(),
      weight.get_float_ptr(),
      output.get_float_ptr());

  // if(bc->beam_slots.at(0).current_depth == 1){
  //     print_beam_tensor<float>(input.get_float_ptr(), 50, 4096, 40, "mha topk
  //     input"); print_beam_tensor<float>(output.get_float_ptr(), 50, 4096, 40,
  //     "mha topk output");
  // }
}

void SpecIncMultiHeadSelfAttention::backward(FFModel const &ff) {
  // SpecIncMultiHeadSelfAttention does not support backward
  assert(false);
}

bool SpecIncMultiHeadSelfAttention::get_int_parameter(PMParameter para,
                                                      int *value) const {
  switch (para) {
    case PM_NUM_HEADS:
      *value = num_heads;
      return true;
    default:
      return Op::get_int_parameter(para, value);
  }
}

Op *SpecIncMultiHeadSelfAttention::materialize(FFModel &ff,
                                               ParallelTensor inputs[],
                                               int num_inputs) const {
  SpecIncMultiHeadSelfAttentionParams params = get_params();
  return new SpecIncMultiHeadSelfAttention(
      ff, params, inputs[0], true, this->name);
}

bool SpecIncMultiHeadSelfAttention::measure_operator_cost(
    Simulator *sim, MachineView const &mv, CostMetrics &cost_metrics) const {
  return false;
}

bool operator==(SpecIncMultiHeadSelfAttentionParams const &lhs,
                SpecIncMultiHeadSelfAttentionParams const &rhs) {
  return lhs.layer_guid == rhs.layer_guid && lhs.embed_dim == rhs.embed_dim &&
         lhs.num_heads == rhs.num_heads && lhs.kdim == rhs.kdim &&
         lhs.vdim == rhs.vdim && lhs.dropout == rhs.dropout &&
         lhs.bias == rhs.bias && lhs.add_bias_kv == rhs.add_bias_kv &&
         lhs.add_zero_attn == rhs.add_zero_attn &&
         lhs.apply_rotary_embedding == rhs.apply_rotary_embedding;
}

SpecIncMultiHeadSelfAttentionParams
    SpecIncMultiHeadSelfAttention::get_params() const {
  SpecIncMultiHeadSelfAttentionParams params;
  params.layer_guid = this->layer_guid;
  params.embed_dim = this->oProjSize;
  params.num_heads = this->num_heads;
  params.kdim = this->kProjSize;
  params.vdim = this->vProjSize;
  params.dropout = this->dropout;
  params.bias = this->bias;
  params.add_bias_kv = this->add_bias_kv;
  params.add_zero_attn = this->add_zero_attn;
  params.apply_rotary_embedding = this->apply_rotary_embedding;
  return params;
}

}; // namespace FlexFlow

namespace std {
size_t hash<FlexFlow::SpecIncMultiHeadSelfAttentionParams>::operator()(
    FlexFlow::SpecIncMultiHeadSelfAttentionParams const &params) const {
  size_t key = 0;
  hash_combine(key, params.layer_guid.id);
  hash_combine(key, params.embed_dim);
  hash_combine(key, params.num_heads);
  hash_combine(key, params.kdim);
  hash_combine(key, params.vdim);
  hash_combine(key, params.dropout);
  hash_combine(key, params.bias);
  hash_combine(key, params.add_bias_kv);
  hash_combine(key, params.add_zero_attn);
  hash_combine(key, params.apply_rotary_embedding);
  return key;
}
}; // namespace std
