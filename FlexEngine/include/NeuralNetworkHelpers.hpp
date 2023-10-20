#pragma once

#include <random>

namespace flex
{
	enum class ActivationFunc
	{
		RELU,
		LEAKY_RELU,
		SIGMOID,
		TANH,
		NONE,

		_COUNT
	};

	static const char* ActivationFuncStr[] =
	{
		"ReLU",
		"Leaky ReLU",
		"Sigmoid",
		"tanh",
		"None",
	};
	static_assert(ARRAY_LENGTH(ActivationFuncStr) == (u32)ActivationFunc::_COUNT, "ActivationFuncStr length must match ActivationFunc enum");

	enum class LayerType
	{
		FULLY_CONNECTED,
		ACTIVATION,
		NONE,

		_COUNT
	};

	static const char* LayerTypeStr[] =
	{
		"Fully connected",
		"Activation",
		"None"
	};
	static_assert(ARRAY_LENGTH(LayerTypeStr) == (u32)LayerType::_COUNT, "LayerTypeStr length must match LayerType enum");

	struct Layer
	{
		LayerType m_Type = LayerType::NONE;
		ActivationFunc m_ActivationFunc = ActivationFunc::NONE;
		u32 m_InputSize = 0;
		u32 m_OutputSize = 0;
		real m_Bias = 0.0f;
		std::vector<real> m_Weights;
		std::vector<real> m_Inputs; // Only used in FC
	};

	struct Matrix
	{
		std::vector<real> m_Data;
	};

	struct TrainingData
	{
		Matrix m_Input;
		Matrix m_Output;
		Matrix m_LastOutput;
		real m_LastLoss;
	};

	struct Network
	{
		Network(u64 seed = 0);

		void AddFullyConnectedLayer(u32 inputSize, u32 outputSize);
		void AddActivationLayer(u32 size, ActivationFunc func);

		real GetNextRandFloat();

		void RunEpoch(std::vector<TrainingData>& trainingData, real learningRate);
		real Evaluate(TrainingData& trainingData);
		void Evaluate(Matrix& inputs);

		std::vector<Layer> m_Layers;

		std::vector<real> m_LastOutputVector;
		u32 m_Epochs = 0;
		real m_RMSE = 1.0f;

		std::vector<real> m_RMSEHistory;

		std::uniform_real_distribution<real> m_RealDistribution;
		std::mt19937 m_Rng;
	};

	void DrawImGuiForNetwork(Network& network);

	float RandomFloatNormalized(std::uniform_real_distribution<real>& distribution, std::mt19937& rng);
	real ReLU(real x);
	real LeakyReLU(real x);
	real Sigmoid(real x);
	real Tanh(real x);
	real ComputeActivationFunc(ActivationFunc func, real x);
	real ComputeActivationFuncPrime(ActivationFunc func, real x);
	real CalculateLoss(const std::vector<real>& predicated, const std::vector<real>& expected);
	FLEX_NO_DISCARD std::vector<real> CalculateLossPrime(const std::vector<real>& predicated, const std::vector<real>& expected);

	FLEX_NO_DISCARD std::vector<real> RunForwardPropagation(Network& network, const std::vector<real>& inputNeurons);

	void RunBackPropagation(Network& network,
		const std::vector<real>& expected,
		const std::vector<real>& predicted,
		real learningRate);
} // namespace flex
