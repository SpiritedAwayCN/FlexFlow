#pragma once

#include "flexflow/model.h"

namespace FlexFlow {

class LayerNormMeta;

class LayerNorm : public Op {
public:
  using Params = LayerNormParams;
  using Input = ParallelTensor;
  LayerNorm(FFModel &model,
            LayerNormParams const &params,
            ParallelTensor input,
            char const *name = nullptr,
            bool allocate_weights = false);
  LayerNorm(FFModel &model,
            LayerID const &_layer_guid,
            const ParallelTensor _input,
            std::vector<int> const &axes,
            bool _elementwise_affine,
            float _eps,
            bool allocate_weights,
            char const *name);
  void init(FFModel const &);
  void forward(FFModel const &);
  void backward(FFModel const &);
  void print_layer(FFModel const &model) {
    assert(0);
  }
  static Op *
      create_operator_from_layer(FFModel &model,
                                 Layer const *layer,
                                 std::vector<ParallelTensor> const &inputs);
  void serialize(Legion::Serializer &) const override;
  static PCG::Node deserialize(FFModel &ff,
                               Legion::Deserializer &d,
                               ParallelTensor inputs[],
                               int num_inputs);
  Op *materialize(FFModel &ff,
                  ParallelTensor inputs[],
                  int num_inputs) const override;
  // size_t get_params_hash() const override;
  LayerNormParams get_params() const;

  static OpMeta *init_task(Legion::Task const *task,
                           std::vector<Legion::PhysicalRegion> const &regions,
                           Legion::Context ctx,
                           Legion::Runtime *runtime);
  static void forward_task(Legion::Task const *task,
                           std::vector<Legion::PhysicalRegion> const &regions,
                           Legion::Context ctx,
                           Legion::Runtime *runtime);
  static void backward_task(Legion::Task const *task,
                            std::vector<Legion::PhysicalRegion> const &regions,
                            Legion::Context ctx,
                            Legion::Runtime *runtime);
  bool measure_operator_cost(Simulator *sim,
                             MachineView const &pc,
                             CostMetrics &cost_metrics) const;
  template <typename T>
  static void forward_kernel(LayerNormMeta const *m,
                             T const *input_ptr,
                             T *output_ptr,
                             T *gamma_ptr,
                             T *beta_ptr,
                             ffStream_t stream);
  template <typename T>
  static void forward_kernel_wrapper(LayerNormMeta const *m,
                                     T const *input_ptr,
                                     T *output_ptr,
                                     T *gamma_ptr,
                                     T *beta_ptr);
  template <typename T>
  static void backward_kernel(LayerNormMeta const *m,
                              T const *output_grad_ptr,
                              T const *input_ptr,
                              T *input_grad_ptr,
                              T const *gamma_ptr,
                              T *gamma_grad_ptr,
                              T *beta_grad_ptr,
                              ffStream_t stream);
  template <typename T>
  static void backward_kernel_wrapper(LayerNormMeta const *m,
                                      T const *output_grad_ptr,
                                      T const *input_ptr,
                                      T *input_grad_ptr,
                                      T const *gamma_ptr,
                                      T *gamma_grad_ptr,
                                      T *beta_grad_ptr);

public:
  bool elementwise_affine;
  int64_t effective_batch_size, effective_num_elements;
  float eps;
  std::vector<int> axes;
};

class LayerNormMeta : public OpMeta {
public:
  LayerNormMeta(FFHandler handle, LayerNorm const *ln);

public:
  bool elementwise_affine;
  int64_t effective_batch_size, effective_num_elements;
  float eps;
  float *mean_ptr, *rstd_ptr, *ds_ptr, *db_ptr, *scale_ptr, *bias_ptr;
  char op_name[MAX_OPNAME];
};

}; // namespace FlexFlow
