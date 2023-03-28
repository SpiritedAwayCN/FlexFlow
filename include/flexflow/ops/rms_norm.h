#ifndef _FLEXFLOW_RMS_NORM_H
#define _FLEXFLOW_RMS_NORM_H

#include "flexflow/model.h"
#include "flexflow/ops/rms_norm_params.h"

namespace FlexFlow {

class RMSNorm : public Op {
public:
  using Input = ParallelTensor;
  RMSNorm(FFModel &model,
            LayerID const &_layer_guid,
            const ParallelTensor _input,
            float _eps,
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
  RMSNormParams get_params() const;

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

public:
    float eps;
    char op_name[MAX_OPNAME];
};

} // namespace FlexFlow

#endif // _FLEXFLOW_RMS_NORM_H