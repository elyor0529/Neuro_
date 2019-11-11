#include "ComputationalGraph/Operations/Conv2dBiasActivationOp.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    Conv2dBiasActivationOp::Conv2dBiasActivationOp(TensorLike* x, TensorLike* kernels, uint32_t stride, uint32_t padding, TensorLike* bias, EActivation activation, float activationAlpha, const string& name)
        : Operation({ x, kernels, bias }, name.empty() ? "conv2d_bias_activation" : name), m_Stride(stride), m_Padding(padding), m_Activation(activation), m_ActivationAlpha(activationAlpha)
    {
        m_Output.Resize(Tensor::GetConvOutputShape(x->GetShape(), kernels->GetShape().Batch(), kernels->GetShape().Width(), kernels->GetShape().Height(), m_Stride, m_Padding, m_Padding, NCHW));
    }

    //////////////////////////////////////////////////////////////////////////
    void Conv2dBiasActivationOp::ComputeInternal()
    {
        auto& x = *m_Inputs[0];
        auto& kernels = *m_Inputs[1];
        auto& bias = *m_Inputs[2];

        m_Output.ResizeBatch(x.Batch());

        return x.Conv2DBiasActivation(kernels, m_Stride, m_Padding, bias, m_Activation, m_ActivationAlpha, m_Output);
    }

    //////////////////////////////////////////////////////////////////////////
    void Conv2dBiasActivationOp::ComputeGradientInternal(const Tensor& grad)
    {
        auto& x = *m_Inputs[0];
        auto& kernels = *m_Inputs[1];
        auto& bias = *m_Inputs[2];

        if (!m_InputNodes[0]->CareAboutGradient() && !m_InputNodes[1]->CareAboutGradient() && !m_InputNodes[2]->CareAboutGradient())
            return;

        m_OutputGradTemp.Resize(grad.GetShape());
        m_OutputGradTemp.TryDeviceAllocate(); // this is actually workspace

        grad.ActivationGradient(m_Activation, m_ActivationAlpha, m_Output, grad, m_OutputGradTemp);

        if (m_InputNodes[1]->CareAboutGradient())
            grad.Conv2DKernelsGradient(x, m_OutputGradTemp, m_Stride, m_Padding, NCHW, m_InputsGrads[1]);
        if (m_InputNodes[2]->CareAboutGradient())
            grad.Conv2DBiasGradient(m_OutputGradTemp, m_InputsGrads[2]);
        if (m_InputNodes[0]->CareAboutGradient())
            grad.Conv2DInputsGradient(m_OutputGradTemp, kernels, m_Stride, m_Padding, NCHW, m_InputsGrads[0]);

        m_OutputGradTemp.TryDeviceRelease();
    }
}