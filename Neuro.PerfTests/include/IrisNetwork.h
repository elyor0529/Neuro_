#include <iostream>
#include <string>
#include <vector>

#include "Neuro.h"

using namespace std;
using namespace Neuro;

class IrisNetwork
{
public:
    static void Run()
    {
        Tensor::SetDefaultOpMode(EOpMode::GPU);

        auto model = new Sequential();
        model->AddLayer(new Dense(4, 1000, new ReLU()));
        model->AddLayer(new Dense(model->LastLayer(), 500, new ReLU()));
        model->AddLayer(new Dense(model->LastLayer(), 300, new ReLU()));
        //model->AddLayer(new Dropout(model->LastLayer(), 0.2f));
        model->AddLayer(new Dense(model->LastLayer(), 3, new Softmax()));
        auto net = new NeuralNetwork(model, "iris");
        net->Optimize(new Adam(), new BinaryCrossEntropy());

        Tensor inputs;
        Tensor outputs;
        LoadCSVData("data/iris_data.csv", 3, inputs, outputs, true, 30);
        inputs = inputs.Normalized(EAxis::Feature);

        Stopwatch timer;
        timer.Start();

        net->Fit(inputs, outputs, 20, 20, nullptr, nullptr, 2, Track::TrainError | Track::TrainAccuracy);

        timer.Stop();
        cout << "Training time " << timer.ElapsedMiliseconds() << "ms";

        return;
    }
};
