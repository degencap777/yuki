/*
    This file is part of Yuki.
    Copyright (C) 2017 Guofeng Dai

    Yuki is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Yuki is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yuki.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"
#ifdef USE_OPENCL

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <fstream>
#include <cmath>
#include <array>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "Utils.h"
#include "Timing.h"
#include "OpenCL.h"
#include "Network.h"
#include "GTP.h"

using namespace Utils;

static std::string sourceCode_convolve1 = boost::str(boost::format(R"(
    __kernel
    __attribute__((work_group_size_hint(8, 16, 1)))
    void convolve1(
                   __global const float * in,
                   __global float * merge,
                   __global const float * weights,
                   __local float * channel_buff,
                   __local float * row_buff) {
        // cl::NDRange global(channels, outputs, row);
        const int c   = get_global_id(0);  // channel
        const int o   = get_global_id(1);  // output
        const int row = get_global_id(2);  // row

        const int channels = get_global_size(0);
        const int outputs  = get_global_size(1);

        // cl::NDRange local(2, (1->32), 1);
        const int lx = get_local_id(0);
        const int ly = get_local_id(1);

        const int chan_buff_size = 8;
        const int out_buff_size  = get_local_size(1);
        const int row_buff_size  = 7;
        const int chan_shift     = 3;

        // input = channels * height * width
        // output = outputs * height * width
        // weights = output * channels * filter
        // merge = channels * outputs * height * width

        const int BOARD_SIZE = %d;
        const int width = BOARD_SIZE;
        const int height = BOARD_SIZE;
        const int strip_size = width;

        // Copy the input channels (strips) locally
        if (out_buff_size < BOARD_SIZE && ly == 0) {
            // strip-row
            for (int w = 0; w < width; w++) {
                channel_buff[lx * width + w] =
                    in[(c * height + row) * width + w];
            }
        } else if (out_buff_size >= BOARD_SIZE && ly < BOARD_SIZE) {
            // Every thread copies a column
            channel_buff[lx * width + ly] = in[(c * height + row) * width + ly];
        }

        // Copy the filter we are applying locally
        __private float filter_buff = weights[(o * channels + c)];

        barrier(CLK_LOCAL_MEM_FENCE);

        int out_lane = 0;
        int out_cw   = 0;
        #pragma unroll
        for (int cw = 0; cw < width; cw++) {
            int fid = lx * strip_size;
            float out  = channel_buff[fid + cw] * filter_buff;
            row_buff[(ly * chan_buff_size + lx) * row_buff_size + out_lane] = out;
            out_lane++;
            // Row buffer full or last lane?
            if (out_lane == row_buff_size || (cw == width - 1)) {
                barrier(CLK_LOCAL_MEM_FENCE);
                if (lx < out_lane) {
                    float val;
                    val  = row_buff[(ly * chan_buff_size + 0) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 1) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 2) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 3) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 4) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 5) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 6) * row_buff_size + lx];
                    val += row_buff[(ly * chan_buff_size + 7) * row_buff_size + lx];
                    merge[(((c >> chan_shift) * height + row) * width + out_cw + lx) * outputs + o] = val;
                }
                out_cw  += row_buff_size;
                out_lane = 0;
           }
       }
    }
)") % BOARD_SIZE);

static std::string sourceCode_convolve3 = boost::str(boost::format(R"(
    __kernel
    __attribute__((work_group_size_hint(8, 32, 1)))
    void convolve3(
                   __global const float * in,
                   __global float * merge,
                   __global const float * weights,
                   __local float * channel_buff,
                   __local float * row_buff,
                   const int row_tile_size,
                   const int row_buff_size,
                   const int chan_buff_size,
                   const int chan_shift) {

        // cl::NDRange global(channels, outputs, row);
        const int c   = get_global_id(0);  // channel
        const int o   = get_global_id(1);  // output
        const int r   = get_global_id(2);  // row

        const int channels = get_global_size(0);
        const int outputs  = get_global_size(1);

        // cl::NDRange local(2, (1->32), 1);
        const int lx = get_local_id(0);
        const int ly = get_local_id(1);

        const int out_buff_size  = get_local_size(1);
        const int BOARD_SIZE = %d;
        const int width = BOARD_SIZE;
        const int height = BOARD_SIZE;

        const int filter_size = 3;
        const int filter_len = filter_size * filter_size;
        const int mid = (filter_size / 2) + 1;
        const int extent = mid - 1;
        const int pad_width = width + filter_size - 1;

        // input = channels * height * width
        // output = outputs * height * width
        // weights = output * channels * filter
        // merge = channels * outputs * height * width

        __private float filter_buff[9];
        __private float chan_cache[2];
        __private float stripe_cache[9];

        // Copy the filter we are applying locally
        // output * channel * filter_len
        for (int f = 0; f < filter_len; f++) {
            filter_buff[f] = weights[(o * channels + c) * filter_len + f];
        }

        for (int tile = 0; tile < row_tile_size; tile++) {
            int row = r * row_tile_size + tile;
            if (row > 18) break;

            // Copy the input channels (strips) locally
            if (out_buff_size < 21 && ly == 0) {
                // strip-row
                for (int srow = 0; srow < filter_size; srow++) {
                    int in_row = row - extent + srow;
                    channel_buff[(lx * pad_width + 0) * filter_size + srow]             = 0.0f;
                    if ((unsigned)in_row < height) {
                        for (int w = 0; w < width; w++) {
                            float val = in[(c * height + in_row) * width + w];
                            channel_buff[(lx * pad_width + w + extent) * filter_size + srow] = val;
                        }
                    } else {
                        for (int w = 0; w < width; w++) {
                            channel_buff[(lx * pad_width + w + extent) * filter_size + srow] = 0.0f;
                        }
                    }
                    channel_buff[(lx * pad_width + pad_width - 1) * filter_size + srow] = 0.0f;
                }
            } else if (out_buff_size >= 21 && ly < 21) {
                // Every thread copies a column
                int copy_idx = (lx * pad_width + ly) * filter_size;
                if (tile == 0 || row == 18) {
                    // Every thread copies a column
                    for (int srow = 0; srow < filter_size; srow++) {
                        int in_row = row - extent + srow;
                        float val = 0.0f;
                        if ((unsigned)in_row < height && ly >= 1 && ly <= BOARD_SIZE) {
                            val = in[(c * height + in_row) * width + ly - 1];
                        }
                        channel_buff[copy_idx + srow] = val;
                        if (srow > 0) {
                            chan_cache[srow - 1] = val;
                        }
                    }
                } else {
                    int in_row = row - extent + 2;
                    float val = 0.0f;
                    if (ly >= 1 && ly <= BOARD_SIZE) {
                        val = in[(c * height + in_row) * width + ly - 1];
                    }
                    channel_buff[copy_idx + 0] = chan_cache[0];
                    channel_buff[copy_idx + 1] = chan_cache[1];
                    channel_buff[copy_idx + 2] = val;
                    chan_cache[0] = chan_cache[1];
                    chan_cache[1] = val;
                }
            }

            int out_lane = 0;
            int out_cw   = 0;
            __local float * out_row_buff = &row_buff[(ly * chan_buff_size + lx) * row_buff_size];
            int fid = (lx * pad_width) * filter_size;
            barrier(CLK_LOCAL_MEM_FENCE);

            for (int rc = 0; rc < 9; rc++) {
                stripe_cache[rc] = channel_buff[fid + rc];
            }

            #pragma unroll
            for (int cw = 0; cw < width; cw++) {
                // Start filter
                float out  =   stripe_cache[      0] * filter_buff[0]
                             + stripe_cache[      1] * filter_buff[3]
                             + stripe_cache[      2] * filter_buff[6]
                             + stripe_cache[      3] * filter_buff[1]
                             + stripe_cache[      4] * filter_buff[4]
                             + stripe_cache[      5] * filter_buff[7]
                             + stripe_cache[      6] * filter_buff[2]
                             + stripe_cache[      7] * filter_buff[5]
                             + stripe_cache[      8] * filter_buff[8];
                // End filter
                out_row_buff[out_lane++] = out;
                fid += filter_size;

                for (int rc = 0; rc < 6; rc++) {
                    stripe_cache[rc] = stripe_cache[rc + 3];
                }
                stripe_cache[6] = channel_buff[fid + 6];
                stripe_cache[7] = channel_buff[fid + 7];
                stripe_cache[8] = channel_buff[fid + 8];

                // Row buffer full or last lane?
                if (out_lane == row_buff_size || (cw == width - 1)) {
                    barrier(CLK_LOCAL_MEM_FENCE);
                    if (lx < out_lane) {
                        // lx = channels 2 or 8, ly = outputs 32
                        // repurpose the lx threads over columns now
                        if (chan_buff_size == 8) {
                            float val;
                            val  = row_buff[(ly * chan_buff_size + 0) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 1) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 2) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 3) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 4) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 5) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 6) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 7) * row_buff_size + lx];
                            merge[(((c >> chan_shift) * height + row) * width + out_cw + lx) * outputs + o] = val;
                        } else if (chan_buff_size == 2) {
                            float val;
                            val  = row_buff[(ly * chan_buff_size + 0) * row_buff_size + lx];
                            val += row_buff[(ly * chan_buff_size + 1) * row_buff_size + lx];
                            merge[(((c >> chan_shift) * height + row) * width + out_cw + lx) * outputs + o] = val;
                        }
                    }
                    out_cw  += row_buff_size;
                    out_lane = 0;
                }
            }
        }
    }
)") % BOARD_SIZE);

static std::string sourceCode_utility = boost::str(boost::format(R"(
    __kernel void merge(
                        __global const float * in,
                        __global float * out,
                        __constant const float * biases,
                        __private const int channels) {

        // cl::NDRange global(outputs, BOARD_SIZE*BOARD_SIZE);
        const int gx = get_global_id(0);
        const int gy = get_global_id(1);

        const int output = gx;
        const int b = gy;
        const int outputs = get_global_size(0);

        const int BOARD_SIZE = %d;
        const int width = BOARD_SIZE;
        const int height = BOARD_SIZE;
        const int boardsize = width * height;

        const int o = output;
        const float bias = biases[o];

        float sum = bias;
        for (int c = 0; c < channels; c++) {
            sum += in[(c * boardsize + b) * outputs + o];
        }
        out[o * boardsize + b] = sum;
    }

    __kernel void batchnorm(
                        __global const float * in,
                        __global float * out,
                        __global const float * residual,
                        __constant const float * means,
                        __constant const float * variances) {

        const int BOARD_SIZE = BOARD_SIZE;
        // cl::NDRange global(outputs, BOARD_SIZE*BOARD_SIZE);
        const int gx = get_global_id(0);
        const int gy = get_global_id(1);

        const int output = gx;
        const int outputs      = get_global_size(0);
        const int channel_size = get_global_size(1);

        const unsigned int o = output;
        const unsigned int b = gy;

        const float epsilon = 1e-5;

        const float mean = means[o];
        const float variance = epsilon + variances[o];
        const float scale_stddiv = 1.0f / sqrt(variance);

        // BN
        float sum = scale_stddiv * (in[o * channel_size + b] - mean);
        // Residual Eltwise
        if (residual) {
            sum += residual[o * channel_size + b];
        }
        // ReLU
        out[o * channel_size + b] = sum > 0 ? sum : 0.0f;
    }
)") % BOARD_SIZE);

OpenCL opencl;
OpenCL_Network opencl_net;
thread_local ThreadData opencl_thread_data;

void OpenCL::ensure_thread_initialized() {
    if (!opencl_thread_data.m_is_initialized) {
        // Make kernels
        opencl_thread_data.m_convolve1_kernel = cl::Kernel(m_program, "convolve1");
        opencl_thread_data.m_convolve3_kernel = cl::Kernel(m_program, "convolve3");
        opencl_thread_data.m_merge_kernel = cl::Kernel(m_program, "merge");
        opencl_thread_data.m_batchnorm_kernel = cl::Kernel(m_program, "batchnorm");
        opencl_thread_data.m_commandqueue = cl::CommandQueue(cl::Context::getDefault(),
                                                             cl::Device::getDefault());
        opencl_thread_data.m_is_initialized = true;
    }
}

void OpenCL_Network::add_weights(size_t layer,
                                 size_t size,
                                 const float * weights) {
    if (layer >= m_layers.size()) {
        m_layers.push_back(Layer());
    }

    size_t weightSize = size *
        sizeof(std::remove_pointer<decltype(weights)>::type);

    cl::Buffer bufferWeights = cl::Buffer(CL_MEM_COPY_HOST_PTR | CL_MEM_READ_ONLY,
                                          weightSize, const_cast<float*>(weights));

    m_layers.back().weights.push_back(bufferWeights);
}

void OpenCL_Network::forward(const std::vector<float>& input,
                             std::vector<float>& output) {
    constexpr int width = BOARD_SIZE;
    constexpr int height = BOARD_SIZE;
    constexpr size_t one_plane = width * height * sizeof(float);

    opencl.ensure_thread_initialized();
    const size_t midSize = one_plane * Network::MAX_CHANNELS;
    const size_t inSize = sizeof(float) * input.size();
    const size_t finalSize = m_layers.back().outputs * one_plane;

    if (!opencl_thread_data.m_buffers_allocated) {
        size_t alloc_midSize = one_plane * Network::MAX_CHANNELS;
        size_t alloc_mergeSize = one_plane *
            Network::MAX_CHANNELS * (Network::MAX_CHANNELS / 2);

        opencl_thread_data.m_inBuffer = cl::Buffer(
            CL_MEM_READ_WRITE, alloc_midSize);
        opencl_thread_data.m_tmpBuffer = cl::Buffer(
            CL_MEM_READ_WRITE, alloc_midSize);
        opencl_thread_data.m_residualBuffer = cl::Buffer(
            CL_MEM_READ_WRITE, alloc_midSize);
        opencl_thread_data.m_mergeBuffer = cl::Buffer(
            CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, alloc_mergeSize);
        opencl_thread_data.m_outBuffer = cl::Buffer(
            CL_MEM_WRITE_ONLY, finalSize);
        opencl_thread_data.m_buffers_allocated = true;
    }

    cl::Buffer & inBuffer = opencl_thread_data.m_inBuffer;
    cl::Buffer & outBuffer = opencl_thread_data.m_outBuffer;
    cl::Buffer & tmpBuffer = opencl_thread_data.m_tmpBuffer;
    cl::Buffer & mergeBuffer = opencl_thread_data.m_mergeBuffer;
    cl::Buffer & residualBuffer = opencl_thread_data.m_residualBuffer;
    cl::CommandQueue & queue = opencl_thread_data.m_commandqueue;

    // XXX: this copies a lot of zeroes
    queue.enqueueWriteBuffer(inBuffer, CL_FALSE, 0, inSize, input.data());

    for (auto& layer : m_layers) {
        if (layer.is_batchnorm) {
            batchnorm(layer.outputs,
                      layer.filter_size,
                      inBuffer,
                      tmpBuffer,
                      nullptr,
                      layer.weights);
            std::swap(inBuffer, tmpBuffer);
        } else if (layer.is_residual_block) {
            auto conv1_weights = std::vector<cl::Buffer>(begin(layer.weights),
                                                         begin(layer.weights) + 2);
            auto bn1_weights   = std::vector<cl::Buffer>(begin(layer.weights) + 2,
                                                         begin(layer.weights) + 4);
            auto conv2_weights = std::vector<cl::Buffer>(begin(layer.weights) + 4,
                                                         begin(layer.weights) + 6);
            auto bn2_weights   = std::vector<cl::Buffer>(begin(layer.weights) + 6,
                                                         begin(layer.weights) + 8);
            queue.enqueueCopyBuffer(inBuffer, residualBuffer, 0, 0, midSize);
            convolve(layer.filter_size,
                     layer.channels,
                     layer.outputs,
                     inBuffer,
                     tmpBuffer,
                     mergeBuffer,
                     conv1_weights);
            std::swap(inBuffer, tmpBuffer);
            batchnorm(layer.outputs,
                      BOARD_SQUARE_SIZE,
                      inBuffer,
                      tmpBuffer,
                      nullptr,
                      bn1_weights);
            std::swap(inBuffer, tmpBuffer);
            convolve(layer.filter_size,
                     layer.channels,
                     layer.outputs,
                     inBuffer,
                     tmpBuffer,
                     mergeBuffer,
                     conv2_weights);
            std::swap(inBuffer, tmpBuffer);
            batchnorm(layer.outputs,
                      BOARD_SQUARE_SIZE,
                      inBuffer,
                      tmpBuffer,
                      &residualBuffer,
                      bn2_weights);
            std::swap(inBuffer, tmpBuffer);
        } else  {
            // plain convolution
            convolve(layer.filter_size,
                     layer.channels,
                     layer.outputs,
                     inBuffer,
                     tmpBuffer,
                     mergeBuffer,
                     layer.weights);
            std::swap(inBuffer, tmpBuffer);
        }
    }

    queue.enqueueCopyBuffer(inBuffer, outBuffer, 0, 0, finalSize);
    queue.enqueueReadBuffer(outBuffer, CL_FALSE, 0, finalSize, output.data());

    queue.finish();
}

void OpenCL_Network::convolve(int filter_size, int channels, int outputs,
                              cl::Buffer& bufferInput,
                              cl::Buffer& bufferOutput,
                              cl::Buffer& bufferMerge,
                              std::vector<cl::Buffer>& weights) {
    // fixed for BOARD_SIZE*BOARD_SIZE
    constexpr int width = BOARD_SIZE;
    constexpr int height = BOARD_SIZE;
    constexpr int boardsize = width * height;

    cl::Kernel * m_convolve_kernel = nullptr;
    if (filter_size == 3) {
        m_convolve_kernel = &opencl_thread_data.m_convolve3_kernel;
    } else {
        assert(filter_size == 1);
        m_convolve_kernel = &opencl_thread_data.m_convolve1_kernel;
    }

    // Input channel grouping
    int channelGroup = 8;
    int channelShift = 3;
    // Input layer is not a multiple of 8
    if (channels == 18) {
        channelGroup = 2;
        channelShift = 1;
    }

    constexpr int rowGroup = 1;
    size_t outputGroup = std::min(outputs, 32);

#ifndef NDEBUG
    // Total output size after reducing
    size_t outSize = width * height * outputs * sizeof(float);

    // Produce channel * output planes and merge them at the end
    size_t mergeSize = (channels >> channelShift) * outSize;
#endif

    // Copy the rows locally
    size_t stripSize;
    int rowTileSize;
    int rowTiles;
    if (filter_size == 3) {
        stripSize = filter_size * (width + (filter_size - 1)) * sizeof(float);
        rowTiles    =  cfg_rowtiles;
        rowTileSize =  (BOARD_SIZE + rowTiles - 1) / rowTiles;
    } else {
        assert(filter_size == 1);
        stripSize = width * sizeof(float);
        rowTiles    = BOARD_SIZE;
        rowTileSize =  1;
        assert(channelGroup == 8); // hardcoded in kernel
    }

    int rowBuffer = std::min<int>(channelGroup, 7);
    size_t rowSize = channelGroup * outputGroup * rowBuffer * sizeof(float);

    assert(mergeSize <= bufferMerge.getInfo<CL_MEM_SIZE>());

    cl::CommandQueue & queue = opencl_thread_data.m_commandqueue;

    try {
        m_convolve_kernel->setArg(0, bufferInput);
        m_convolve_kernel->setArg(1, bufferMerge);
        m_convolve_kernel->setArg(2, weights[0]);
        m_convolve_kernel->setArg(3, cl::Local(stripSize * channelGroup * rowGroup));
        m_convolve_kernel->setArg(4, cl::Local(rowSize));
        if (filter_size == 3) {
            m_convolve_kernel->setArg(5, rowTileSize);
            m_convolve_kernel->setArg(6, rowBuffer);
            m_convolve_kernel->setArg(7, channelGroup);
            m_convolve_kernel->setArg(8, channelShift);
        }

        queue.enqueueNDRangeKernel(*m_convolve_kernel, cl::NullRange,
                                   cl::NDRange(channels, outputs, rowTiles),
                                   cl::NDRange(channelGroup, outputGroup, rowGroup));
    } catch (const cl::Error &e) {
        std::cerr << "Error in convolve: " << e.what() << ": "
	        << e.err() << std::endl;
        throw;
    }

    cl::Kernel & merge_kernel = opencl_thread_data.m_merge_kernel;
    assert(channels % (1 << channelShift) == 0);

    try {
        merge_kernel.setArg(0, bufferMerge);
        merge_kernel.setArg(1, bufferOutput);
        merge_kernel.setArg(2, weights[1]);
        merge_kernel.setArg(3, channels >> channelShift);

        queue.enqueueNDRangeKernel(merge_kernel, cl::NullRange,
                                   cl::NDRange(outputs, boardsize),
                                   cl::NDRange(std::min(8, outputs), BOARD_SIZE));
    } catch (const cl::Error &e) {
        std::cerr << "Error in merge: " << e.what() << ": "
	        << e.err() << std::endl;
        throw;
    }
}

void OpenCL_Network::batchnorm(int outputs,
                               int channel_size,
                               cl::Buffer& bufferInput,
                               cl::Buffer& bufferOutput,
                               cl::Buffer* bufferResidual,
                               std::vector<cl::Buffer>& weights) {
    cl::CommandQueue & queue = opencl_thread_data.m_commandqueue;

    cl::Kernel & batchnorm_kernel = opencl_thread_data.m_batchnorm_kernel;

    size_t channelGroup = 1;
    if (channel_size == BOARD_SQUARE_SIZE) {
        channelGroup = BOARD_SIZE;
    }

    try {
        batchnorm_kernel.setArg(0, bufferInput);
        batchnorm_kernel.setArg(1, bufferOutput);
        if (bufferResidual) {
            batchnorm_kernel.setArg(2, *bufferResidual);
        } else {
            batchnorm_kernel.setArg(2, nullptr);
        }
        batchnorm_kernel.setArg(3, weights[0]);
        batchnorm_kernel.setArg(4, weights[1]);

        queue.enqueueNDRangeKernel(batchnorm_kernel, cl::NullRange,
                                   cl::NDRange(outputs, channel_size),
                                   cl::NDRange(std::min(8, outputs), channelGroup));
    } catch (const cl::Error &e) {
        std::cerr << "Error in batchnorm: " << e.what() << ": "
            << e.err() << std::endl;
        throw;
    }
}

template<class T>
static std::string opencl_dev_type_to_string(T type) {
    if (type == CL_DEVICE_TYPE_CPU) {
        return "CPU";
    } else if (type == CL_DEVICE_TYPE_GPU) {
        return "GPU";
    } else if (type == CL_DEVICE_TYPE_ACCELERATOR) {
        return "Accelerator";
    } else {
        return "Unknown";
    }
}

static std::string trim(std::string trim_me) {
    boost::algorithm::trim(trim_me);
    return trim_me;
}

void OpenCL::initialize(void) {
    std::vector<cl::Platform> platforms;
    try {
        cl::Platform::get(&platforms);
    } catch (const cl::Error &e) {
        myprintf("OpenCL: %s\n", e.what());
        throw;
    }

    float best_version = 0.0f;
    cl::Platform best_platform;
    cl::Device best_device;
    std::string best_vendor;
    int best_score = 0;
    bool found_device = false;
    int id = 0;

    myprintf("Detected %d OpenCL platforms\n", platforms.size());

    for (auto &p : platforms) {
        std::string platvers = p.getInfo<CL_PLATFORM_VERSION>();
        std::string platprof = p.getInfo<CL_PLATFORM_PROFILE>();
        std::string platname = p.getInfo<CL_PLATFORM_NAME>();
        std::string platvend = p.getInfo<CL_PLATFORM_VENDOR>();
        myprintf("Platform version: %s\n", platvers.c_str());;
        myprintf("Platform profile: %s\n", platprof.c_str());
        myprintf("Platform name:    %s\n", platname.c_str());
        myprintf("Platform vendor:  %s\n", platvend.c_str());

        std::istringstream versstream(platvers);
        std::string tmp;
        float opencl_version;
        versstream >> tmp >> opencl_version;

        std::vector<cl::Device> devices;
        try {
            p.getDevices(CL_DEVICE_TYPE_ALL, &devices);
        } catch (const cl::Error &e) {
            myprintf("Error getting device(s): %s: %d\n", e.what(), e.err());
            devices.clear();
        }
        for (auto& d : devices) {
            myprintf("Device ID:     %d\n", id);
            myprintf("Device name:   %s\n",
                     trim(d.getInfo<CL_DEVICE_NAME>()).c_str());
            myprintf("Device type:   %s\n",
                     opencl_dev_type_to_string(d.getInfo<CL_DEVICE_TYPE>()).c_str());
            myprintf("Device vendor: %s\n",
                      d.getInfo<CL_DEVICE_VENDOR>().c_str());
            myprintf("Device driver: %s\n",
                      d.getInfo<CL_DRIVER_VERSION>().c_str());
            myprintf("Device speed:  %u MHz\n",
                      d.getInfo<CL_DEVICE_MAX_CLOCK_FREQUENCY>());
            myprintf("Device cores:  %u CU\n",
                      d.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>());

            // assign score, try to find best device
            int this_score = 0;
            std::string this_vendor = d.getInfo<CL_DEVICE_VENDOR>();
            this_score += 1000 * boost::icontains(this_vendor, "advanced micro devices");
            this_score += 1000 * boost::icontains(this_vendor, "amd");
            this_score += 1000 * boost::icontains(this_vendor, "nvidia");
            this_score +=  500 * boost::icontains(this_vendor, "intel");
            this_score +=  100 * (d.getInfo<CL_DEVICE_TYPE>() == CL_DEVICE_TYPE_GPU);
            this_score +=  opencl_version * 10;
            myprintf("Device score:  %d\n", this_score);

            bool preferred = std::find(cfg_gpus.cbegin(), cfg_gpus.cend(), id) != cfg_gpus.cend();

            if ((this_score > best_score) || preferred) {
                best_version = opencl_version;
                best_platform = p;
                best_device = d;
                if (preferred) {
                    best_score = std::numeric_limits<decltype(best_score)>::max();
                } else {
                    best_score = this_score;
                }
                found_device = true;
            }
            id++;
        }
    }

    if (!found_device) {
        throw std::runtime_error("No suitable OpenCL device found.");
    }

    cl::Platform::setDefault(best_platform);
    myprintf("Selected platform: %s\n", best_platform.getInfo<CL_PLATFORM_NAME>().c_str());
    myprintf("Selected device: %s\n", trim(best_device.getInfo<CL_DEVICE_NAME>()).c_str());
    myprintf("with OpenCL %2.1f capability\n", best_version);

    cl::Context context;
    try {
        context = cl::Context(best_device);
    } catch (const cl::Error &e) {
        myprintf("Error creating OpenCL context: %s: %d", e.what(), e.err());
        throw;
    }
    cl::Context::setDefault(context);
    cl::Device::setDefault(best_device);

    // Read source file
    //std::ifstream sourceFile("convolve_kernel.cl", std::ifstream::in);
    //std::string sourceCode(std::istreambuf_iterator<char>(sourceFile),
    //                       (std::istreambuf_iterator<char>()));

    // Make program of the source code in the context
    try {
        m_program = cl::Program(sourceCode_convolve1
                                + sourceCode_convolve3
                                + sourceCode_utility);
    } catch (const cl::Error &e) {
        myprintf("Error getting kernels: %s: %d", e.what(), e.err());
        throw;
    }
    // Build program for these specific devices
    try {
        m_program.build("-cl-mad-enable -cl-fast-relaxed-math -cl-no-signed-zeros -cl-denorms-are-zero");
    } catch (const cl::Error&) {
        myprintf("Error building kernels: %s\n",
                    m_program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(cl::Device::getDefault()).c_str());
        throw;
    }

    ensure_thread_initialized();

    m_wavefront_size =
        opencl_thread_data.m_convolve3_kernel.getWorkGroupInfo<CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE>(
            best_device);
    myprintf("Wavefront/Warp size: %d\n", m_wavefront_size);

    m_max_workgroup_size = best_device.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>();
    m_max_workgroup_dims = best_device.getInfo<CL_DEVICE_MAX_WORK_ITEM_SIZES>();

    myprintf("Max workgroup size: %d\n", m_max_workgroup_size);
    myprintf("Max workgroup dimensions: ");
    for (auto d : m_max_workgroup_dims) {
        myprintf("%d ", d);
    }
    myprintf("\n");

    m_init_ok = true;
}

std::string OpenCL::get_device_name() {
    std::stringstream ss;

    cl::Device device = cl::Device::getDefault();
    ss << "OpenCL: ";
    ss << device.getInfo<CL_DEVICE_VENDOR>() << " ";
    ss << device.getInfo<CL_DEVICE_NAME>() << " @ ";
    ss << device.getInfo<CL_DEVICE_MAX_CLOCK_FREQUENCY>() << "MHz";

    return ss.str();
}
#endif
