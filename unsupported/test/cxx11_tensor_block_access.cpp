// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2018 Andy Davis <andydavis@google.com>
// Copyright (C) 2018 Eugene Zhulenev <ezhulenev@google.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "main.h"

#include <set>

#include <Eigen/CXX11/Tensor>

using Eigen::Tensor;
using Eigen::Index;
using Eigen::RowMajor;
using Eigen::ColMajor;

using internal::TensorBlockShapeType;

template<typename T>
static const T& choose(int layout, const T& col, const T& row) {
  return layout == ColMajor ? col : row;
}

static const TensorBlockShapeType RandomShape() {
  return internal::random<bool>()
             ? internal::TensorBlockShapeType::kUniformAllDims
             : internal::TensorBlockShapeType::kSkewedInnerDims;
}

template <int NumDims>
static std::size_t RandomTargetSize(const DSizes<Index, NumDims>& dims) {
  return internal::random<int>(1, dims.TotalSize());
}

template <typename T>
static T* GenerateRandomData(const Index& size) {
  T* data = new T[size];
  for (int i = 0; i < size; ++i) {
    data[i] = internal::random<T>();
  }
  return data;
}

template <int Layout>
static void test_block_mapper_sanity()
{
  using T = int;
  using TensorBlock = internal::TensorBlock<T, Index, 2, Layout>;
  using TensorBlockMapper = internal::TensorBlockMapper<T, Index, 2, Layout>;

  DSizes<Index, 2> tensor_dims(100, 100);

  // Test uniform blocks.
  TensorBlockMapper uniform_block_mapper(
      tensor_dims, internal::TensorBlockShapeType::kUniformAllDims, 100);

  VERIFY_IS_EQUAL(uniform_block_mapper.total_block_count(), 100);
  VERIFY_IS_EQUAL(uniform_block_mapper.block_dims_total_size(), 100);

  // 10x10 blocks
  auto uniform_b0 = uniform_block_mapper.GetBlockForIndex(0, nullptr);
  VERIFY_IS_EQUAL(uniform_b0.block_sizes().at(0), 10);
  VERIFY_IS_EQUAL(uniform_b0.block_sizes().at(1), 10);
  // Depending on a layout we stride by cols rows.
  VERIFY_IS_EQUAL(uniform_b0.block_strides().at(0), choose(Layout, 1, 10));
  VERIFY_IS_EQUAL(uniform_b0.block_strides().at(1), choose(Layout, 10, 1));
  // Tensor strides depend only on a layout and not on the block size.
  VERIFY_IS_EQUAL(uniform_b0.tensor_strides().at(0), choose(Layout, 1, 100));
  VERIFY_IS_EQUAL(uniform_b0.tensor_strides().at(1), choose(Layout, 100, 1));

  // Test skewed to inner dims blocks.
  TensorBlockMapper skewed_block_mapper(
      tensor_dims, internal::TensorBlockShapeType::kSkewedInnerDims, 100);

  VERIFY_IS_EQUAL(skewed_block_mapper.total_block_count(), 100);
  VERIFY_IS_EQUAL(skewed_block_mapper.block_dims_total_size(), 100);

  // 1x100 (100x1) rows/cols depending on a tensor layout.
  auto skewed_b0 = skewed_block_mapper.GetBlockForIndex(0, nullptr);
  VERIFY_IS_EQUAL(skewed_b0.block_sizes().at(0), choose(Layout, 100, 1));
  VERIFY_IS_EQUAL(skewed_b0.block_sizes().at(1), choose(Layout, 1, 100));
  // Depending on a layout we stride by cols rows.
  VERIFY_IS_EQUAL(skewed_b0.block_strides().at(0), choose(Layout, 1, 100));
  VERIFY_IS_EQUAL(skewed_b0.block_strides().at(1), choose(Layout, 100, 1));
  // Tensor strides depend only on a layout and not on the block size.
  VERIFY_IS_EQUAL(skewed_b0.tensor_strides().at(0), choose(Layout, 1, 100));
  VERIFY_IS_EQUAL(skewed_b0.tensor_strides().at(1), choose(Layout, 100, 1));
}

// Given a TensorBlock "visit" every element accessible though it, and a keep an
// index in the visited set. Verify that every coeff accessed only once.
template <typename T, int Layout, int NumDims>
static void UpdateCoeffSet(
    const internal::TensorBlock<T, Index, 4, Layout>& block,
    Index first_coeff_index, int dim_index, std::set<Index>* visited_coeffs) {
  const DSizes<Index, NumDims> block_sizes = block.block_sizes();
  const DSizes<Index, NumDims> tensor_strides = block.tensor_strides();

  for (int i = 0; i < block_sizes[dim_index]; ++i) {
    if (tensor_strides[dim_index] == 1) {
      auto inserted = visited_coeffs->insert(first_coeff_index + i);
      VERIFY_IS_EQUAL(inserted.second, true);
    } else {
      int next_dim_index = dim_index + choose(Layout, -1, 1);
      UpdateCoeffSet<T, Layout, NumDims>(block, first_coeff_index,
                                         next_dim_index, visited_coeffs);
      first_coeff_index += tensor_strides[dim_index];
    }
  }
}

template <int Layout>
static void test_block_mapper_maps_every_element()
{
  using T = int;
  using TensorBlock = internal::TensorBlock<T, Index, 4, Layout>;
  using TensorBlockMapper = internal::TensorBlockMapper<T, Index, 4, Layout>;

  DSizes<Index, 4> dims(5, 7, 11, 17);

  // Keep track of elements indices available via block access.
  std::set<Index> coeff_set;

  // Try different combinations of block types and sizes.
  TensorBlockMapper block_mapper(dims, RandomShape(), RandomTargetSize(dims));

  for (int i = 0; i < block_mapper.total_block_count(); ++i) {
    TensorBlock block = block_mapper.GetBlockForIndex(i, nullptr);
    UpdateCoeffSet<T, Layout, 4>(block, block.first_coeff_index(),
                                 choose(Layout, 3, 0), &coeff_set);
  }

  // Verify that every coefficient in the original Tensor is accessible through
  // TensorBlock only once.
  auto total_coeffs = static_cast<int>(dims.TotalSize());
  VERIFY_IS_EQUAL(coeff_set.size(), total_coeffs);
  VERIFY_IS_EQUAL(*coeff_set.begin(), static_cast<Index>(0));
  VERIFY_IS_EQUAL(*coeff_set.rbegin(), static_cast<Index>(total_coeffs - 1));
}

template <int Layout>
static void test_slice_block_mapper_maps_every_element()
{
  using T = int;
  using TensorBlock = internal::TensorBlock<T, Index, 4, Layout>;
  using TensorSliceBlockMapper =
      internal::TensorSliceBlockMapper<T, Index, 4, Layout>;

  DSizes<Index, 4> tensor_dims(5,7,11,17);
  DSizes<Index, 4> tensor_slice_offsets(1,3,5,7);
  DSizes<Index, 4> tensor_slice_extents(3,2,4,5);

  // Keep track of elements indices available via block access.
  std::set<Index> coeff_set;

  auto total_coeffs = static_cast<int>(tensor_slice_extents.TotalSize());

  // Pick a random dimension sizes for the tensor blocks.
  DSizes<Index, 4> block_sizes;
  for (int i = 0; i < 4; ++i) {
    block_sizes[i] = internal::random<int>(1, tensor_slice_extents[i]);
  }

  TensorSliceBlockMapper block_mapper(tensor_dims, tensor_slice_offsets,
                                      tensor_slice_extents, block_sizes,
                                      DimensionList<Index, 4>());

  for (int i = 0; i < block_mapper.total_block_count(); ++i) {
    TensorBlock block = block_mapper.GetBlockForIndex(i, nullptr);
    UpdateCoeffSet<T, Layout, 4>(block, block.first_coeff_index(),
                                 choose(Layout, 3, 0), &coeff_set);
  }

  VERIFY_IS_EQUAL(coeff_set.size(), total_coeffs);
}

template <int Layout>
static void test_block_io_copy_data_from_source_to_target()
{
  using T = float;

  typedef internal::TensorBlock<T, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockMapper<T, Index, 5, Layout> TensorBlockMapper;

  typedef internal::TensorBlockReader<T, Index, 5, Layout, true>
      TensorBlockReader;
  typedef internal::TensorBlockWriter<T, Index, 5, Layout, true>
      TensorBlockWriter;

  typedef std::vector<T, aligned_allocator<T>> DataVector;

  DSizes<Index, 5> input_tensor_dims(5, 7, 11, 17, 3);
  const auto input_tensor_size = input_tensor_dims.TotalSize();
  DataVector input_data(input_tensor_size, 0);
  for (int i = 0; i < input_tensor_size; ++i) {
    input_data[i] = internal::random<T>();
  }

  DataVector output_data(input_tensor_size, 0);

  TensorBlockMapper block_mapper(input_tensor_dims, RandomShape(),
                                 RandomTargetSize(input_tensor_dims));

  DataVector block_data(block_mapper.block_dims_total_size(), 0);
  for (int i = 0; i < block_mapper.total_block_count(); ++i) {
    TensorBlock block = block_mapper.GetBlockForIndex(i, block_data.data());
    TensorBlockReader::Run(&block, input_data.data());
    TensorBlockWriter::Run(block, output_data.data());
  }

  for (int i = 0; i < input_tensor_size; ++i) {
    VERIFY_IS_EQUAL(input_data[i], output_data[i]);
  }
}

template <int Layout, int NumDims>
static int GetInputIndex(Index output_index,
                         const array<Index, NumDims>& output_to_input_dim_map,
                         const array<Index, NumDims>& input_strides,
                         const array<Index, NumDims>& output_strides) {
  int input_index = 0;
  if (Layout == ColMajor) {
    for (int i = NumDims - 1; i > 0; --i) {
      const int idx = output_index / output_strides[i];
      input_index += idx * input_strides[output_to_input_dim_map[i]];
      output_index -= idx * output_strides[i];
    }
    return input_index +
           output_index * input_strides[output_to_input_dim_map[0]];
  } else {
    for (int i = 0; i < NumDims - 1; ++i) {
      const int idx = output_index / output_strides[i];
      input_index += idx * input_strides[output_to_input_dim_map[i]];
      output_index -= idx * output_strides[i];
    }
    return input_index +
           output_index * input_strides[output_to_input_dim_map[NumDims - 1]];
  }
}

template <int Layout, int NumDims>
static array<Index, NumDims> ComputeStrides(
    const array<Index, NumDims>& sizes) {
  array<Index, NumDims> strides;
  if (Layout == ColMajor) {
    strides[0] = 1;
    for (int i = 1; i < NumDims; ++i) {
      strides[i] = strides[i - 1] * sizes[i - 1];
    }
  } else {
    strides[NumDims - 1] = 1;
    for (int i = NumDims - 2; i >= 0; --i) {
      strides[i] = strides[i + 1] * sizes[i + 1];
    }
  }
  return strides;
}

template <int Layout>
static void test_block_io_copy_using_reordered_dimensions() {
  typedef internal::TensorBlock<float, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockMapper<float, Index, 5, Layout>
      TensorBlockMapper;

  typedef internal::TensorBlockReader<float, Index, 5, Layout, false>
      TensorBlockReader;
  typedef internal::TensorBlockWriter<float, Index, 5, Layout, false>
      TensorBlockWriter;

  DSizes<Index, 5> input_tensor_dims(5, 7, 11, 17, 3);
  const auto input_tensor_size = input_tensor_dims.TotalSize();

  // Create a random input tensor.
  auto* input_data = GenerateRandomData<float>(input_tensor_size);

  // Create a random dimension re-ordering/shuffle.
  std::vector<Index> shuffle = {0, 1, 2, 3, 4};
  std::shuffle(shuffle.begin(), shuffle.end(), std::mt19937());

  DSizes<Index, 5> output_tensor_dims;
  array<Index, 5> input_to_output_dim_map;
  array<Index, 5> output_to_input_dim_map;
  for (Index i = 0; i < 5; ++i) {
    output_tensor_dims[shuffle[i]] = input_tensor_dims[i];
    input_to_output_dim_map[i] = shuffle[i];
    output_to_input_dim_map[shuffle[i]] = i;
  }

  // Random block shape and size.
  TensorBlockMapper block_mapper(output_tensor_dims, RandomShape(),
                                 RandomTargetSize(input_tensor_dims));

  auto* block_data = new float[block_mapper.block_dims_total_size()];
  auto* output_data = new float[input_tensor_size];

  array<Index, 5> input_tensor_strides =
      ComputeStrides<Layout, 5>(input_tensor_dims);
  array<Index, 5> output_tensor_strides =
      ComputeStrides<Layout, 5>(output_tensor_dims);

  for (Index i = 0; i < block_mapper.total_block_count(); ++i) {
    TensorBlock block = block_mapper.GetBlockForIndex(i, block_data);
    const Index first_coeff_index = GetInputIndex<Layout, 5>(
        block.first_coeff_index(), output_to_input_dim_map,
        input_tensor_strides, output_tensor_strides);
    TensorBlockReader::Run(&block, first_coeff_index, input_to_output_dim_map,
                           input_tensor_strides, input_data);
    TensorBlockWriter::Run(block, first_coeff_index, input_to_output_dim_map,
                           input_tensor_strides, output_data);
  }

  for (int i = 0; i < input_tensor_size; ++i) {
    VERIFY_IS_EQUAL(input_data[i], output_data[i]);
  }

  delete[] input_data;
  delete[] block_data;
  delete[] output_data;
}

template <int Layout>
static void test_block_io_zero_stride()
{
  typedef internal::TensorBlock<float, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockReader<float, Index, 5, Layout, true>
      TensorBlockReader;
  typedef internal::TensorBlockWriter<float, Index, 5, Layout, true>
      TensorBlockWriter;

  DSizes<Index, 5> input_tensor_dims(1, 2, 1, 3, 1);
  const auto input_tensor_size = input_tensor_dims.TotalSize();

  // Create a random input tensor.
  auto* input_data = GenerateRandomData<float>(input_tensor_size);

  DSizes<Index, 5> output_tensor_dims(3, 2, 3, 3, 2);

  DSizes<Index, 5> input_tensor_strides(
      ComputeStrides<Layout, 5>(input_tensor_dims));
  DSizes<Index, 5> output_tensor_strides(
      ComputeStrides<Layout, 5>(output_tensor_dims));

  DSizes<Index, 5> input_tensor_strides_with_zeros(input_tensor_strides);
  input_tensor_strides_with_zeros[0] = 0;
  input_tensor_strides_with_zeros[2] = 0;
  input_tensor_strides_with_zeros[4] = 0;

  // Verify that data was correctly read/written from/into the block.
  const auto verify_is_equal = [&](const float* output_data) {
    for (int i = 0; i < output_tensor_dims[0]; ++i) {
      for (int j = 0; j < output_tensor_dims[1]; ++j) {
        for (int k = 0; k < output_tensor_dims[2]; ++k) {
          for (int l = 0; l < output_tensor_dims[3]; ++l) {
            for (int m = 0; m < output_tensor_dims[4]; ++m) {
              const Index output_offset =
                  i * output_tensor_strides[0] + j * output_tensor_strides[1] +
                  k * output_tensor_strides[2] + l * output_tensor_strides[3] +
                  m * output_tensor_strides[4];
              const Index input_offset =
                  i % input_tensor_dims[0] * input_tensor_strides[0] +
                  j % input_tensor_dims[1] * input_tensor_strides[1] +
                  k % input_tensor_dims[2] * input_tensor_strides[2] +
                  l % input_tensor_dims[3] * input_tensor_strides[3] +
                  m % input_tensor_dims[4] * input_tensor_strides[4];
              VERIFY_IS_EQUAL(output_data[output_offset],
                              input_data[input_offset]);
            }
          }
        }
      }
    }
  };

  {
    auto* output_data = new float[output_tensor_dims.TotalSize()];
    TensorBlock read_block(0, output_tensor_dims, output_tensor_strides,
                           input_tensor_strides_with_zeros, output_data);
    TensorBlockReader::Run(&read_block, input_data);
    verify_is_equal(output_data);
    delete[] output_data;
  }

  {
    auto* output_data = new float[output_tensor_dims.TotalSize()];
    TensorBlock write_block(0, output_tensor_dims,
                            input_tensor_strides_with_zeros,
                            output_tensor_strides, input_data);
    TensorBlockWriter::Run(write_block, output_data);
    verify_is_equal(output_data);
    delete[] output_data;
  }

  delete[] input_data;
}

template <int Layout>
static void test_block_io_squeeze_ones() {
  typedef internal::TensorBlock<float, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockReader<float, Index, 5, Layout, true>
      TensorBlockReader;
  typedef internal::TensorBlockWriter<float, Index, 5, Layout, true>
      TensorBlockWriter;

  // Total size > 1.
  {
    DSizes<Index, 5> block_sizes(1, 2, 1, 2, 1);
    const auto total_size = block_sizes.TotalSize();

    // Create a random input tensor.
    auto* input_data = GenerateRandomData<float>(total_size);
    DSizes<Index, 5> strides(ComputeStrides<Layout, 5>(block_sizes));

    {
      auto* output_data = new float[block_sizes.TotalSize()];
      TensorBlock read_block(0, block_sizes, strides, strides, output_data);
      TensorBlockReader::Run(&read_block, input_data);
      for (int i = 0; i < total_size; ++i) {
        VERIFY_IS_EQUAL(output_data[i], input_data[i]);
      }
      delete[] output_data;
    }

    {
      auto* output_data = new float[block_sizes.TotalSize()];
      TensorBlock write_block(0, block_sizes, strides, strides, input_data);
      TensorBlockWriter::Run(write_block, output_data);
      for (int i = 0; i < total_size; ++i) {
        VERIFY_IS_EQUAL(output_data[i], input_data[i]);
      }
      delete[] output_data;
    }
  }

  // Total size == 1.
  {
    DSizes<Index, 5> block_sizes(1, 1, 1, 1, 1);
    const auto total_size = block_sizes.TotalSize();

    // Create a random input tensor.
    auto* input_data = GenerateRandomData<float>(total_size);
    DSizes<Index, 5> strides(ComputeStrides<Layout, 5>(block_sizes));

    {
      auto* output_data = new float[block_sizes.TotalSize()];
      TensorBlock read_block(0, block_sizes, strides, strides, output_data);
      TensorBlockReader::Run(&read_block, input_data);
      for (int i = 0; i < total_size; ++i) {
        VERIFY_IS_EQUAL(output_data[i], input_data[i]);
      }
      delete[] output_data;
    }

    {
      auto* output_data = new float[block_sizes.TotalSize()];
      TensorBlock write_block(0, block_sizes, strides, strides, input_data);
      TensorBlockWriter::Run(write_block, output_data);
      for (int i = 0; i < total_size; ++i) {
        VERIFY_IS_EQUAL(output_data[i], input_data[i]);
      }
      delete[] output_data;
    }
  }
}

template <int Layout>
static void test_block_cwise_binary_io_basic() {
  typedef internal::scalar_sum_op<float> BinaryFunctor;
  typedef internal::TensorBlockCwiseBinaryIO<BinaryFunctor, Index, float, 5,
                                             Layout>
      TensorBlockCwiseBinaryIO;

  DSizes<Index, 5> block_sizes(2, 3, 5, 7, 11);
  DSizes<Index, 5> strides(ComputeStrides<Layout, 5>(block_sizes));

  const auto total_size = block_sizes.TotalSize();

  // Create a random input tensors.
  auto* left_data = GenerateRandomData<float>(total_size);
  auto* right_data = GenerateRandomData<float>(total_size);

  auto* output_data = new float[total_size];
  BinaryFunctor functor;
  TensorBlockCwiseBinaryIO::Run(functor, block_sizes, strides, output_data,
                                strides, left_data, strides, right_data);
  for (int i = 0; i < total_size; ++i) {
    VERIFY_IS_EQUAL(output_data[i], functor(left_data[i], right_data[i]));
  }

  delete[] left_data;
  delete[] right_data;
  delete[] output_data;
}

template <int Layout>
static void test_block_cwise_binary_io_squeeze_ones() {
  typedef internal::scalar_sum_op<float> BinaryFunctor;
  typedef internal::TensorBlockCwiseBinaryIO<BinaryFunctor, Index, float, 5,
                                             Layout>
      TensorBlockCwiseBinaryIO;

  DSizes<Index, 5> block_sizes(1, 2, 1, 3, 1);
  DSizes<Index, 5> strides(ComputeStrides<Layout, 5>(block_sizes));

  const auto total_size = block_sizes.TotalSize();

  // Create a random input tensors.
  auto* left_data = GenerateRandomData<float>(total_size);
  auto* right_data = GenerateRandomData<float>(total_size);

  auto* output_data = new float[total_size];
  BinaryFunctor functor;
  TensorBlockCwiseBinaryIO::Run(functor, block_sizes, strides, output_data,
                                strides, left_data, strides, right_data);
  for (int i = 0; i < total_size; ++i) {
    VERIFY_IS_EQUAL(output_data[i], functor(left_data[i], right_data[i]));
  }

  delete[] left_data;
  delete[] right_data;
  delete[] output_data;
}

template <int Layout>
static void test_block_cwise_binary_io_zero_strides() {
  typedef internal::scalar_sum_op<float> BinaryFunctor;
  typedef internal::TensorBlockCwiseBinaryIO<BinaryFunctor, Index, float, 5,
                                             Layout>
      TensorBlockCwiseBinaryIO;

  DSizes<Index, 5> left_sizes(1, 3, 1, 7, 1);
  DSizes<Index, 5> left_strides(ComputeStrides<Layout, 5>(left_sizes));
  left_strides[0] = 0;
  left_strides[2] = 0;
  left_strides[4] = 0;

  DSizes<Index, 5> right_sizes(2, 1, 5, 1, 11);
  DSizes<Index, 5> right_strides(ComputeStrides<Layout, 5>(right_sizes));
  right_strides[1] = 0;
  right_strides[3] = 0;

  // Generate random data.
  auto* left_data = GenerateRandomData<float>(left_sizes.TotalSize());
  auto* right_data = GenerateRandomData<float>(right_sizes.TotalSize());

  DSizes<Index, 5> output_sizes(2, 3, 5, 7, 11);
  DSizes<Index, 5> output_strides(ComputeStrides<Layout, 5>(output_sizes));

  const auto output_total_size = output_sizes.TotalSize();
  auto* output_data = new float[output_total_size];

  BinaryFunctor functor;
  TensorBlockCwiseBinaryIO::Run(functor, output_sizes, output_strides,
                                output_data, left_strides, left_data,
                                right_strides, right_data);
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int k = 0; k < 5; ++k) {
        for (int l = 0; l < 7; ++l) {
          for (int m = 0; m < 11; ++m) {
            Index output_index = i * output_strides[0] + j * output_strides[1] +
                                 k * output_strides[2] + l * output_strides[3] +
                                 m * output_strides[4];
            Index left_index = i * left_strides[0] + j * left_strides[1] +
                               k * left_strides[2] + l * left_strides[3] +
                               m * left_strides[4];
            Index right_index = i * right_strides[0] + j * right_strides[1] +
                                k * right_strides[2] + l * right_strides[3] +
                                m * right_strides[4];
            VERIFY_IS_EQUAL(
                output_data[output_index],
                functor(left_data[left_index], right_data[right_index]));
          }
        }
      }
    }
  }

  delete[] left_data;
  delete[] right_data;
  delete[] output_data;
}

template <int Layout>
static void test_uniform_block_shape()
{
  using T = int;
  typedef internal::TensorBlock<T, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockMapper<T, Index, 5, Layout> TensorBlockMapper;

  {
    // Test shape 'UniformAllDims' with uniform 'max_coeff count'.
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 5 * 5 * 5 * 5 * 5;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    for (int i = 0; i < 5; ++i) {
      VERIFY_IS_EQUAL(5, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'UniformAllDims' with larger 'max_coeff count' which spills
  // partially into first inner-most dimension.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 7 * 5 * 5 * 5 * 5;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[0]);
    for (int i = 1; i < 5; ++i) {
      VERIFY_IS_EQUAL(5, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 5 * 5 * 5 * 5 * 6;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(6, block.block_sizes()[4]);
    for (int i = 3; i >= 0; --i) {
      VERIFY_IS_EQUAL(5, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'UniformAllDims' with larger 'max_coeff count' which spills
  // fully into first inner-most dimension.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 5 * 5 * 5 * 5;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    for (int i = 1; i < 5; ++i) {
      VERIFY_IS_EQUAL(5, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 5 * 5 * 5 * 5 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    for (int i = 3; i >= 0; --i) {
      VERIFY_IS_EQUAL(5, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'UniformAllDims' with larger 'max_coeff count' which spills
  // fully into first few inner-most dimensions.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(7, 5, 6, 17, 7);
    const size_t max_coeff_count = 7 * 5 * 6 * 7 * 5;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[0]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(7, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[4]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(7, 5, 6, 9, 7);
    const size_t max_coeff_count = 5 * 5 * 5 * 6 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[0]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'UniformAllDims' with full allocation to all dims.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(7, 5, 6, 17, 7);
    const size_t max_coeff_count = 7 * 5 * 6 * 17 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[0]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(17, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(7, 5, 6, 9, 7);
    const size_t max_coeff_count = 7 * 5 * 6 * 9 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kUniformAllDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY_IS_EQUAL(9, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(7, block.block_sizes()[0]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }
}

template <int Layout>
static void test_skewed_inner_dim_block_shape()
{
  using T = int;
  typedef internal::TensorBlock<T, Index, 5, Layout> TensorBlock;
  typedef internal::TensorBlockMapper<T, Index, 5, Layout> TensorBlockMapper;

  // Test shape 'SkewedInnerDims' with partial allocation to inner-most dim.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 10 * 1 * 1 * 1 * 1;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(10, block.block_sizes()[0]);
    for (int i = 1; i < 5; ++i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 1 * 1 * 1 * 1 * 6;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(6, block.block_sizes()[4]);
    for (int i = 3; i >= 0; --i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'SkewedInnerDims' with full allocation to inner-most dim.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 1 * 1 * 1 * 1;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    for (int i = 1; i < 5; ++i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 1 * 1 * 1 * 1 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    for (int i = 3; i >= 0; --i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'SkewedInnerDims' with full allocation to inner-most dim,
  // and partial allocation to second inner-dim.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 3 * 1 * 1 * 1;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    VERIFY_IS_EQUAL(3, block.block_sizes()[1]);
    for (int i = 2; i < 5; ++i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 1 * 1 * 1 * 15 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY_IS_EQUAL(15, block.block_sizes()[3]);
    for (int i = 2; i >= 0; --i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'SkewedInnerDims' with full allocation to inner-most dim,
  // and partial allocation to third inner-dim.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 5 * 5 * 1 * 1;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[2]);
    for (int i = 3; i < 5; ++i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 1 * 1 * 5 * 17 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY_IS_EQUAL(17, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[2]);
    for (int i = 1; i >= 0; --i) {
      VERIFY_IS_EQUAL(1, block.block_sizes()[i]);
    }
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }

  // Test shape 'SkewedInnerDims' with full allocation to all dims.
  if (Layout == ColMajor) {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 5 * 6 * 17 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(17, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  } else {
    DSizes<Index, 5> dims(11, 5, 6, 17, 7);
    const size_t max_coeff_count = 11 * 5 * 6 * 17 * 7;
    TensorBlockMapper block_mapper(dims, TensorBlockShapeType::kSkewedInnerDims,
                                   max_coeff_count);
    TensorBlock block = block_mapper.GetBlockForIndex(0, nullptr);
    VERIFY_IS_EQUAL(7, block.block_sizes()[4]);
    VERIFY_IS_EQUAL(17, block.block_sizes()[3]);
    VERIFY_IS_EQUAL(6, block.block_sizes()[2]);
    VERIFY_IS_EQUAL(5, block.block_sizes()[1]);
    VERIFY_IS_EQUAL(11, block.block_sizes()[0]);
    VERIFY(block.block_sizes().TotalSize() <= max_coeff_count);
  }
}

template <int Layout>
static void test_empty_dims(const internal::TensorBlockShapeType block_shape)
{
  using T = int;

  // Test blocking of tensors with zero dimensions:
  //  - we must not crash on asserts and divisions by zero
  //  - we must not return block with zero dimensions
  //    (recipe for overflows/underflows, divisions by zero and NaNs later)
  //  - total block count must be zero
  {
    typedef internal::TensorBlockMapper<T, Index, 1, Layout> TensorBlockMapper;
    DSizes<Index, 1> dims(0);
    for (int max_coeff_count = 0; max_coeff_count < 2; ++max_coeff_count) {
      TensorBlockMapper block_mapper(dims, block_shape, max_coeff_count);
      VERIFY_IS_EQUAL(block_mapper.total_block_count(), 0);
      VERIFY(block_mapper.block_dims_total_size() >= 1);
    }
  }

  {
    typedef internal::TensorBlockMapper<T, Index, 2, Layout> TensorBlockMapper;
    for (int dim1 = 0; dim1 < 3; ++dim1) {
      for (int dim2 = 0; dim2 < 3; ++dim2) {
        DSizes<Index, 2> dims(dim1, dim2);
        for (int max_coeff_count = 0; max_coeff_count < 2; ++max_coeff_count) {
          TensorBlockMapper block_mapper(dims, block_shape, max_coeff_count);
          if (dim1 * dim2 == 0) {
            VERIFY_IS_EQUAL(block_mapper.total_block_count(), 0);
          }
          VERIFY(block_mapper.block_dims_total_size() >= 1);
        }
      }
    }
  }
}

#define CALL_SUBTEST_LAYOUTS(NAME) \
  CALL_SUBTEST(NAME<ColMajor>()); \
  CALL_SUBTEST(NAME<RowMajor>())

#define CALL_SUBTEST_LAYOUTS_WITH_ARG(NAME, ARG) \
  CALL_SUBTEST(NAME<ColMajor>(ARG)); \
  CALL_SUBTEST(NAME<RowMajor>(ARG))

EIGEN_DECLARE_TEST(cxx11_tensor_assign) {
  CALL_SUBTEST_LAYOUTS(test_block_mapper_sanity);
  CALL_SUBTEST_LAYOUTS(test_block_mapper_maps_every_element);
  CALL_SUBTEST_LAYOUTS(test_slice_block_mapper_maps_every_element);
  CALL_SUBTEST_LAYOUTS(test_block_io_copy_data_from_source_to_target);
  CALL_SUBTEST_LAYOUTS(test_block_io_copy_using_reordered_dimensions);
  CALL_SUBTEST_LAYOUTS(test_block_io_zero_stride);
  CALL_SUBTEST_LAYOUTS(test_block_io_squeeze_ones);
  CALL_SUBTEST_LAYOUTS(test_block_cwise_binary_io_basic);
  CALL_SUBTEST_LAYOUTS(test_block_cwise_binary_io_squeeze_ones);
  CALL_SUBTEST_LAYOUTS(test_block_cwise_binary_io_zero_strides);
  CALL_SUBTEST_LAYOUTS(test_uniform_block_shape);
  CALL_SUBTEST_LAYOUTS(test_skewed_inner_dim_block_shape);

  CALL_SUBTEST_LAYOUTS_WITH_ARG(test_empty_dims, TensorBlockShapeType::kUniformAllDims);
  CALL_SUBTEST_LAYOUTS_WITH_ARG(test_empty_dims, TensorBlockShapeType::kSkewedInnerDims);
}

#undef CALL_SUBTEST_LAYOUTS
#undef CALL_SUBTEST_LAYOUTS_WITH_ARG