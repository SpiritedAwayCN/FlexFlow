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

#pragma once

#include "flexflow/batch_config.h"
#include "flexflow/model.h"


using namespace std;
using namespace FlexFlow;

class FileDataLoader {
public:
  FileDataLoader(std::string _input_path, std::string _weight_file_path);

  BatchConfig::TokenId *generate_requests(int num, int length);
  
  void load_weights(FFModel *ff, std::unordered_map<std::string, Layer *> weights_layers);

private:
  std::string input_path;
  std::string weight_file_path;
};
