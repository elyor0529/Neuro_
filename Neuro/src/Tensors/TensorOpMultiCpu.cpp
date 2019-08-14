﻿#include <ppl.h>

#include "Tensors/TensorOpMultiCpu.h"

namespace Neuro
{
    using namespace concurrency;

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Add(float alpha, const Tensor& t1, float beta, const Tensor& t2, Tensor& result) const
    {
        t1.CopyToHost();
        t2.CopyToHost();
        result.OverrideHost();

        auto& t1Values = t1.GetValues();
        auto& t2Values = t2.GetValues();
        auto& resultValues = result.GetValues();

        if (t2.BatchSize() == t1.BatchSize())
        {
            parallel_for(0, (int)t1Values.size(), [&](int i)
            {
                resultValues[i] = alpha * t1Values[i] + beta * t2Values[i];
            });
            return;
        }

        parallel_for(0, t1.BatchSize(), [&](int n)
        {
            for (int i = 0, idx = n * t1.BatchLength(); i < t1.BatchLength(); ++i, ++idx)
                resultValues[idx] = alpha * t1Values[idx] + beta * t2Values[i];
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Mul(bool transposeT1, bool transposeT2, const Tensor& t1, const Tensor& t2, Tensor& result) const
    {
        auto t1Temp = transposeT1 ? t1.Transposed() : t1;
        auto t2Temp = transposeT2 ? t2.Transposed() : t2;

        t1Temp.CopyToHost();
        t2Temp.CopyToHost();
        result.Zero();

        parallel_for(0, result.BatchSize(), [&](int n)
        {
            int t1N = min(n, t1Temp.BatchSize() - 1);
            int t2N = min(n, t2Temp.BatchSize() - 1);

            parallel_for(0, t1Temp.Depth(), [&](int d)
            {
                for (int h = 0; h < t1Temp.Height(); ++h)
                for (int w = 0; w < t2Temp.Width(); ++w)
                for (int i = 0; i < t1Temp.Width(); ++i)
                    result(w, h, d, n) += t1Temp.Get(i, h, d, t1N) * t2Temp.Get(w, i, d, t2N);
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::MulElem(const Tensor& t1, const Tensor& t2, Tensor& result) const
    {
        t1.CopyToHost();
        t2.CopyToHost();
        result.OverrideHost();

        auto& t1Values = t1.GetValues();
        auto& t2Values = t2.GetValues();
        auto& resultValues = result.GetValues();

        parallel_for(0, (int)t1Values.size(), [&](int i)
        {
            resultValues[i] = t1Values[i] * t2Values[i];
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Transpose(const Tensor& t, Tensor& result) const
    {
        t.CopyToHost();
        result.OverrideHost();

        parallel_for(0, t.BatchSize(), [&](int n)
        {
            parallel_for(0, t.Depth(), [&](int d)
            {
                for (int h = 0; h < t.Height(); ++h)
                for (int w = 0; w < t.Width(); ++w)
                    result(h, w, d, n) = t(w, h, d, n);
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Conv2D(const Tensor& t, const Tensor& kernels, int stride, Tensor::EPaddingType padding, Tensor& result) const
    {
        t.CopyToHost();
        kernels.CopyToHost();
        result.OverrideHost();

        int outputWidth = 0, outputHeight = 0, paddingX = 0, paddingY = 0;
        Tensor::GetPaddingParams(padding, t.Width(), t.Height(), kernels.Width(), kernels.Height(), stride, outputHeight, outputWidth, paddingX, paddingY);

        parallel_for(0, t.BatchSize(), [&](int n)
        {
            parallel_for(0, kernels.BatchSize(), [&](int outD)
            {
                for (int h = -paddingY, outH = 0; outH < result.Height(); h += stride, ++outH)
                for (int w = -paddingX, outW = 0; outW < result.Width(); w += stride, ++outW)
                {
                    float val = 0;

                    for (int kernelD = 0; kernelD < kernels.Depth(); ++kernelD)
                    for (int kernelH = 0; kernelH < kernels.Height(); ++kernelH)
                    for (int kernelW = 0; kernelW < kernels.Width(); ++kernelW)
                        val += t.TryGet(0, w + kernelW, h + kernelH, kernelD, n) * kernels(kernelW, kernelH, kernelD, outD);

                    result(outW, outH, outD, n) = val;
                }
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Conv2DInputGradient(const Tensor& gradient, const Tensor& kernels, int stride, Tensor::EPaddingType padding, Tensor& inputGradients) const
    {
        gradient.CopyToHost();
        kernels.CopyToHost();
        inputGradients.CopyToHost();

        Tensor rotKernels = kernels.Rotated180();
        padding = Tensor::EPaddingType::Full;

        int outputWidth = 0, outputHeight = 0, paddingX = 0, paddingY = 0;
        Tensor::GetPaddingParams(padding, gradient.Width(), gradient.Height(), kernels.Width(), kernels.Height(), stride, outputHeight, outputWidth, paddingX, paddingY);

        parallel_for(0, gradient.BatchSize(), [&](int n)
        {
            for (int outH = 0, h = -paddingY; outH < inputGradients.Height(); h += stride, ++outH)
            for (int outW = 0, w = -paddingX; outW < inputGradients.Width(); w += stride, ++outW)
            parallel_for(0, inputGradients.Depth(), [&](int outD)
            {
                for (int kernelN = 0; kernelN < rotKernels.BatchSize(); ++kernelN)
                for (int kernelH = 0; kernelH < rotKernels.Height(); ++kernelH)
                for (int kernelW = 0; kernelW < rotKernels.Width(); ++kernelW)
                    inputGradients(outW, outH, outD, n) += gradient.TryGet(0, w + kernelW, h + kernelH, kernelN, n) * rotKernels(kernelW, kernelH, outD, kernelN);
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Conv2DKernelsGradient(const Tensor& input, const Tensor& gradient, int stride, Tensor::EPaddingType padding, Tensor& kernelsGradient) const
    {
        input.CopyToHost();
        gradient.CopyToHost();
        kernelsGradient.CopyToHost();

        int outputWidth = 0, outputHeight = 0, paddingX = 0, paddingY = 0;
        Tensor::GetPaddingParams(padding, input.Width(), input.Height(), kernelsGradient.Width(), kernelsGradient.Height(), stride, outputHeight, outputWidth, paddingX, paddingY);

        for (int n = 0; n < gradient.BatchSize(); ++n)
        parallel_for(0, kernelsGradient.BatchSize(), [&](int outD)
        {
            for (int h = -paddingY, outH = 0; outH < gradient.Height(); h += stride, ++outH)
            for (int w = -paddingX, outW = 0; outW < gradient.Width(); w += stride, ++outW)
            {
                float grad = gradient(outW, outH, outD, n);

                for (int kernelD = 0; kernelD < kernelsGradient.Depth(); ++kernelD)
                for (int kernelH = 0; kernelH < kernelsGradient.Height(); ++kernelH)
                for (int kernelW = 0; kernelW < kernelsGradient.Width(); ++kernelW)
                {
                    float kernGradVal = input.TryGet(0, w + kernelW, h + kernelH, kernelD, n) * grad;
                    kernelsGradient(kernelW, kernelH, kernelD, outD) += kernGradVal;
                }
            }
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Pool(const Tensor& t, int filterSize, int stride, Tensor::EPoolType type, int paddingX, int paddingY, Tensor& result) const
    {
        t.CopyToHost();
        result.OverrideHost();

        parallel_for(0, t.BatchSize(), [&](int outN)
        {
            parallel_for(0, t.Depth(), [&](int outD)
            {
                for (int outH = 0, h = -paddingY; outH < result.Height(); h += stride, ++outH)
                for (int outW = 0, w = -paddingX; outW < result.Width(); w += stride, ++outW)
                {
                    if (type == Tensor::EPoolType::Max)
                    {
                        float value = numeric_limits<float>::min();

                        for (int poolY = 0; poolY < filterSize; ++poolY)
                        for (int poolX = 0; poolX < filterSize; ++poolX)
                        {
                            value = max(value, t.TryGet(numeric_limits<float>::min(), w + poolX, h + poolY, outD, outN));
                        }

                        result(outW, outH, outD, outN) = value;
                    }
                    else if (type == Tensor::EPoolType::Avg)
                    {
                        float sum = 0;
                        for (int poolY = 0; poolY < filterSize; ++poolY)
                        for (int poolX = 0; poolX < filterSize; ++poolX)
                            sum += t.TryGet(0, w + poolX, h + poolY, outD, outN);

                        result(outW, outH, outD, outN) = sum / (filterSize * filterSize);
                    }
                }
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::PoolGradient(const Tensor& output, const Tensor& input, const Tensor& outputGradient, int filterSize, int stride, Tensor::EPoolType type, int paddingX, int paddingY, Tensor& result) const
    {
        output.CopyToHost();
        input.CopyToHost();
        outputGradient.CopyToHost();
        result.OverrideHost();

        result.Zero();

        parallel_for(0, output.BatchSize(), [&](int outN)
        {
            parallel_for(0, output.Depth(), [&](int outD)
            {
                for (int outH = 0, h = -paddingY; outH < output.Height(); ++outH, h += stride)
                for (int outW = 0, w = -paddingX; outW < output.Width(); ++outW, w += stride)
                {
                    if (type == Tensor::EPoolType::Max)
                    {
                        // use 1 for all elements equal to max value in each pooled matrix and 0 for all others
                        for (int poolH = 0; poolH < filterSize; ++poolH)
                        for (int poolW = 0; poolW < filterSize; ++poolW)
                        {
                            float value = input.TryGet(numeric_limits<float>::min(), w + poolW, h + poolH, outD, outN);
                            if (value == output(outW, outH, outD, outN))
                                result.TrySet(result.TryGet(numeric_limits<float>::min(), w + poolW, h + poolH, outD, outN) + outputGradient(outW, outH, outD, outN), w + poolW, h + poolH, outD, outN);
                        }
                    }
                    else if (type == Tensor::EPoolType::Avg)
                    {
                        float filterElementsNum = (float)filterSize * filterSize;

                        for (int poolH = 0; poolH < filterSize; ++poolH)
                        for (int poolW = 0; poolW < filterSize; ++poolW)
                        {
                            result.TrySet(result.TryGet(numeric_limits<float>::min(), w + poolW, h + poolH, outD, outN) + outputGradient(outW, outH, outD, outN) / filterElementsNum, w + poolW, h + poolH, outD, outN);
                        }
                    }
                }
            });
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Map(const function<float(float)>& func, const Tensor& t, Tensor& result) const
    {
        t.CopyToHost();
        result.OverrideHost();

        auto& tValues = t.GetValues();
        auto& resultValues = result.GetValues();

        parallel_for(0, (int)tValues.size(), [&](int i)
        {
            resultValues[i] = func(tValues[i]);
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::Map(const function<float(float, float)>& func, const Tensor& t1, const Tensor& t2, Tensor& result) const
    {
        t1.CopyToHost();
        t2.CopyToHost();
        result.OverrideHost();

        auto& t1Values = t1.GetValues();
        auto& t2Values = t2.GetValues();
        auto& resultValues = result.GetValues();

        parallel_for(0, (int)t1Values.size(), [&](int i)
        {
            resultValues[i] = func(t1Values[i], t2Values[i]);
        });
    }

    //////////////////////////////////////////////////////////////////////////
    void TensorOpMultiCpu::SumBatches(const Tensor& t, Tensor& result) const
    {
        t.CopyToHost();
        result.OverrideHost();

        auto& tValues = t.GetValues();
        auto& resultValues = result.GetValues();

        int batchLen = t.BatchLength();

        parallel_for(0, result.BatchSize(), [&](int n)
        {
            for (int i = 0, idx = n * batchLen; i < batchLen; ++i, ++idx)
                resultValues[i] += tValues[idx];
        });
    }

}