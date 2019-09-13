﻿#pragma once

#include "Layers/SingleLayer.h"

namespace Neuro
{
    // This layer should only be used when we want to combine raw input with output of another layer
    // somewhere inside a network
    class Input : public SingleLayer
    {
	public:
        Input(const Shape& inputShape, const string& name = "");

	protected:
        Input();

		virtual LayerBase* GetCloneInstance() const override;
		virtual void FeedForwardInternal(bool training) override;
		virtual void BackPropInternal(vector<Tensor>& outputsGradient) override;
	};
}
