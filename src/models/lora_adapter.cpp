// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "lora_adapter.h"

#include "../generators.h"
#include "../span.h"
#include "../flatbuffers.h"
#include "../flatbuffers/lora_format_version.h"
#include "../flatbuffers/flatbuffers_utils.h"
#include "model.h"
#include "onnxruntime_api.h"
#include "utils.h"

#include <fstream>

namespace Generators {
namespace {
uint64_t empty_input_buf[] = {0xdeadbeefbeefdead};
}  // namespace

namespace details {

std::shared_ptr<OrtValue> CreateEmptyInput(const Model& model, const OrtValue& original) {
  const auto& requested_mem_info = model.allocator_device_->GetInfo();
  auto type_and_shape = original.GetTensorTypeAndShapeInfo();
  auto shape = type_and_shape->GetShape();

  // Modify shape
  const auto num_dims = shape.size();
  if (num_dims < 2) {
    throw std::runtime_error("Shape must have at least 2 dimensions");
  }

  // Zero out lora_r dim
  const size_t last_dim = shape[num_dims - 1];
  const size_t penal_dim = shape[num_dims - 2];
  if (last_dim < penal_dim) {
    shape[num_dims - 1] = 0;
  } else {
    shape[num_dims - 2] = 0;
  }

  return OrtValue::CreateTensor(requested_mem_info, &empty_input_buf, 0, shape, type_and_shape->GetElementType());
}

void BinaryFormatHolder::Load(const std::string& file_name) {
  std::ifstream is(file_name, std::ios::binary | std::ios::ate);
  if (!is.good()) {
    throw std::runtime_error("Error opening flatbuffers file: " + file_name);
  }

  auto const file_size = static_cast<size_t>(is.tellg());
  is.seekg(0, std::ios::beg);

  buffer_.resize(file_size);
  is.read(reinterpret_cast<char*>(buffer_.data()), file_size);

  if (!is.good()) {
    throw std::runtime_error("Error reading flatbuffers file: " + file_name);
  }

  is.close();

  if (!lora_parameters::utils::IsGenAiLoraFormatModelBytes(reinterpret_cast<const uint8_t*>(buffer_.data()), file_size)) {
    throw std::runtime_error(file_name + ": does not appear to be a valid lora parameter format");
  }

  flatbuffers::Verifier verifier(buffer_.data(), file_size);
  if (!lora_parameters::VerifyParametersBuffer(verifier)) {
    throw std::runtime_error(file_name + ": fails flatbuffers format verification");
  }

  parameters_ = lora_parameters::GetParameters(buffer_.data());
  if (!lora_parameters::IsLoraFormatVersionSupported(parameters_->version())) {
    throw std::runtime_error(file_name + ": unsupported lora format version");
  }
}

LoraParam::LoraParam(std::string name, std::shared_ptr<OrtValue> ort_value)
    : name_(std::move(name)), ort_user_supplied_value_(std::move(ort_value)) {
}

std::shared_ptr<OrtValue> MakeDeviceCopyIfNeeded(const Model& model, const LoraParam& param) {
  std::shared_ptr<OrtValue> result;
  const auto& src_value = param.ort_user_supplied_value_;

  // Check if the target device is not CPU
  // XXX: Adjust for caching when implemented
  if (model.device_type_ != DeviceType::CPU) {
    // Check if the user has already supplied his buffers on the target device
    const auto& mem_info = src_value->GetTensorMemoryInfo();
    auto src_device_type = mem_info.GetDeviceType();

    if ((model.device_type_ == DeviceType::CUDA &&
         src_device_type == OrtMemoryInfoDeviceType::OrtMemoryInfoDeviceType_GPU)) {
      // Re-use what user has supplied on GPU
      result = src_value;
    } else if (src_device_type != OrtMemoryInfoDeviceType::OrtMemoryInfoDeviceType_CPU) {
      // XXX: Can the user supply buffers already on DML?
      throw std::runtime_error("Lora parameter buffers are on unsupported device: " +
                               std::to_string(static_cast<int>(model.device_type_)));
    } else {
      result = CopyToDevice(*src_value, model);
    }
  } else {
    result = src_value;
  }
  return result;
}

void LoraAdapter::LoadParametersFromFlatBuffer(const std::string& file_name) {
  format_holder_.Load(file_name);

  const auto* fbs_parameters = format_holder_.GetParameters();
  std::vector<LoraParam> parameters;
  parameters.reserve(fbs_parameters->parameters()->size());

  for (const auto* fbs_tensor : *fbs_parameters->parameters()) {
    auto [name, ort_value] = lora_parameters::utils::CreateOrtValueOverFlatBufferLoraParameter(*fbs_tensor);
    parameters.emplace_back(std::move(name), std::move(ort_value));
  }
  parameters_.swap(parameters);
}

}  // namespace details

void LoraAdapterContainer::LoadAdaptersFromConfig(const fs::path& model_path, const Config& config) {
  AdapterMap adapters;
  for (const auto& [adapter_name, file_name] : config.lora_adapters.adapters) {
    auto hit = adapters.find(adapter_name);
    if (hit != adapters.end()) {
      throw std::runtime_error("Adapter: " + adapter_name + " already exist");
    }
    auto& adapter = adapters[adapter_name];
    adapter.SetName(adapter_name);
    auto full_path = model_path / file_name;
    adapter.LoadParametersFromFlatBuffer(full_path.string());
  }
  adapters_.swap(adapters);
}

}  // namespace Generators