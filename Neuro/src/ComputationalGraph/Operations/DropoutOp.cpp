#include "ComputationalGraph/Operations/DropoutOp.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    DropoutOp::DropoutOp(TensorLike* x, float prob, const string& name)
        : Operation({ x }, name.empty() ? "dropout" : name), m_Prob(prob)
    {
        m_Mask.Resize(x->GetShape());
        m_Mask.Name(Name() + "/mask");
        UpdateOutputShape();
    }

    //////////////////////////////////////////////////////////////////////////
    void DropoutOp::ComputeInternal()
    {
        auto& x = *m_Inputs[0];

        m_Output.ResizeBatch(m_Inputs[0]->Batch());
        if (m_Training)
        {
            m_Mask.ResizeBatch(m_Inputs[0]->Batch());
            m_Inputs[0]->Dropout(m_Prob, m_Mask, m_Output);
        }
        else
        {
            m_Inputs[0]->CopyTo(m_Output);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    void DropoutOp::ComputeGradientInternal(const Tensor& grad)
    {
        if (m_InputNodes[0]->CareAboutGradient())
            grad.DropoutGradient(grad, m_Prob, m_Mask, m_InputsGrads[0]);
    }
}