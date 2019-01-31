#include <cmath>
#include <omp.h>

#include "cuBERT/common.h"
#include "LayerNorm.h"

namespace cuBERT {
    LayerNorm::LayerNorm(size_t max_batch_size, size_t channels, float *beta, float *gamma) {
        this->channels = channels;

        this->mean_gpu = static_cast<float *>(cuBERT::malloc(max_batch_size * sizeof(float)));
        this->var_gpu = static_cast<float *>(cuBERT::malloc(max_batch_size * sizeof(float)));

        this->beta = static_cast<float *>(cuBERT::malloc(channels * sizeof(float)));
        this->gamma = static_cast<float *>(cuBERT::malloc(channels * sizeof(float)));
        cuBERT::memcpy(this->beta, beta, channels * sizeof(float), 1);
        cuBERT::memcpy(this->gamma, gamma, channels * sizeof(float), 1);
    }

    LayerNorm::~LayerNorm() {
        cuBERT::free(gamma);
        cuBERT::free(beta);

        cuBERT::free(var_gpu);
        cuBERT::free(mean_gpu);
    }

    void LayerNorm::compute_(size_t batch_size, float *inout, void* stream) {
        if (cuBERT::gpu()) {
            layer_norm_(inout, batch_size, channels, beta, gamma, stream);
            return;
        }

#pragma omp parallel for
        for (int batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
            float mean = 0;
            float var = 0;
#pragma unroll
            for (int i = batch_idx * channels; i < (batch_idx + 1) * channels; ++i) {
                float t = inout[i];
                mean += t;
                var += t * t;
            }
            mean = mean / channels;
            var = var / channels - mean * mean;

            // 1 / sqrt(var)
            var = 1.f / sqrtf(var + 1e-12f);

#pragma unroll
            for (int i = 0; i < channels; ++i) {
                int j = batch_idx * channels + i;
                inout[j] = beta[i] + gamma[i] * var * (inout[j] - mean);
            }
        }
    }

    void LayerNorm::compute_(size_t batch_size, float *in, float *inout, void* stream) {
        if (cuBERT::gpu()) {
            layer_norm_(in, inout, batch_size, channels, mean_gpu, var_gpu, beta, gamma, stream);
            return;
        }

#pragma omp parallel for
        for (int batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
            float mean = 0;
            float var = 0;
#pragma unroll
            for (int i = batch_idx * channels; i < (batch_idx + 1) * channels; ++i) {
                float t = inout[i] + in[i];
                mean += t;
                var += t * t;
            }
            mean = mean / channels;
            var = var / channels - mean * mean;

            // 1 / sqrt(var)
            var = 1.f / sqrtf(var + 1e-12f);

#pragma unroll
            for (int i = 0; i < channels; ++i) {
                int j = batch_idx * channels + i;
                inout[j] = beta[i] + gamma[i] * var * (inout[j] + in[j] - mean);
            }
        }
    }
}
