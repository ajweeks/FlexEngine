#include "stdafx.hpp"

#include "NeuralNetworkHelpers.hpp"

#include "Helpers.hpp"

IGNORE_WARNINGS_PUSH
#include <imgui/imgui_internal.h> // For BeginColumns
IGNORE_WARNINGS_POP

namespace flex
{
	static const real e = glm::e<real>();

	Network::Network(u64 seed /* = 0 */) :
		m_RMSE(1.0f),
		m_Epochs(0)
	{
		m_Rng = std::mt19937((u32)seed);
		m_RealDistribution = std::uniform_real_distribution<real>(0.0f, 1.0f);
	}

	void Network::AddFullyConnectedLayer(u32 inputSize, u32 outputSize)
	{
		m_Layers.push_back({});
		Layer& newLayer = m_Layers.back();
		newLayer.m_Type = LayerType::FULLY_CONNECTED;
		newLayer.m_InputSize = inputSize;
		newLayer.m_OutputSize = outputSize;
		newLayer.m_Bias = GetNextRandFloat();
		newLayer.m_Weights.resize(inputSize * outputSize);
		for (real& weight : newLayer.m_Weights)
		{
			weight = GetNextRandFloat();
		}
	}

	void Network::AddActivationLayer(u32 size, ActivationFunc func)
	{
		m_Layers.push_back({});
		Layer& newLayer = m_Layers.back();
		newLayer.m_Type = LayerType::ACTIVATION;
		newLayer.m_ActivationFunc = func;
		newLayer.m_InputSize = size;
		newLayer.m_OutputSize = size;
	}

	real Network::GetNextRandFloat()
	{
		real result = m_RealDistribution(m_Rng);
		return result;
	}

	void Network::RunEpoch(std::vector<TrainingData>& trainingData, real learningRate)
	{
		real accumError = 0.0f;
		for (TrainingData& data : trainingData)
		{
			m_LastOutputVector = RunForwardPropagation(*this, data.m_Input.m_Data);
			real loss = CalculateLoss(m_LastOutputVector, data.m_Output.m_Data);
			accumError += loss;
			data.m_LastLoss = loss;
			data.m_LastOutput.m_Data = m_LastOutputVector;
			RunBackPropagation(*this, data.m_Output.m_Data, m_LastOutputVector, learningRate);
		}

		m_RMSE = accumError / trainingData.size();
		++m_Epochs;
		Print("Loss: %1.3f (epochs: %3u)\n", m_RMSE, m_Epochs);
		m_RMSEHistory.push_back(m_RMSE);
	}

	real Network::Evaluate(TrainingData& trainingData)
	{
		m_LastOutputVector = RunForwardPropagation(*this, trainingData.m_Input.m_Data);
		real loss = CalculateLoss(m_LastOutputVector, trainingData.m_Output.m_Data);
		return loss;
	}

	void Network::Evaluate(Matrix& inputs)
	{
		m_LastOutputVector = RunForwardPropagation(*this, inputs.m_Data);
	}

	void DrawImGuiForNetwork(Network& network)
	{
		ImGui::Text("Layers");

		if (ImGui::BeginChild("layers", ImVec2(0, 125), true))
		{
			ImGui::BeginColumns("cols", (i32)network.m_Layers.size());
			for (Layer& layer : network.m_Layers)
			{
				if (layer.m_Type == LayerType::ACTIVATION)
				{
					ImGui::Text("%s (%s)", LayerTypeStr[(u32)layer.m_Type], ActivationFuncStr[(u32)layer.m_ActivationFunc]);
				}
				else
				{
					ImGui::Text("%s", LayerTypeStr[(u32)layer.m_Type]);
				}

				if (layer.m_Type == LayerType::FULLY_CONNECTED)
				{
					ImGui::Text("Bias: %.4f", layer.m_Bias);
				}

				if (layer.m_Type == LayerType::FULLY_CONNECTED)
				{
					ImGui::Text("Weights:");
					for (u32 i = 0; i < (u32)layer.m_Weights.size(); ++i)
					{
						ImGui::Text("% .2f", layer.m_Weights[i]);

						if (i % layer.m_OutputSize != (layer.m_OutputSize - 1))
						{
							ImGui::SameLine();
						}
					}
				}
				ImGui::NextColumn();
			}

			ImGui::EndColumns();
		}
		ImGui::EndChild();

		//if (ImGui::BeginChild("scroll_region_eval", ImVec2(0, 125), true))
		//{
		//	ImGui::BeginColumns("cols2", (i32)network.m_Layers.size() + 1);
		//	for (Layer& layer : network.m_Layers)
		//	{
		//		ImGui::Text("Inputs:");
		//		for (u32 i = 0; i < (u32)layer.m_Inputs.size(); ++i)
		//		{
		//			ImGui::Text("% .2f", layer.m_Inputs[i]);
		//		}

		//		ImGui::NextColumn();
		//	}

		//	ImGui::Text("Outputs:");
		//	for (u32 i = 0; i < (u32)network.m_LastOutputVector.size(); ++i)
		//	{
		//		ImGui::Text("% .2f", network.m_LastOutputVector[i]);
		//	}
		//	ImGui::NextColumn();

		//	ImGui::EndColumns();
		//}
		//ImGui::EndChild();

		real maxLoss = 0.0f;
		for (real loss : network.m_RMSEHistory) maxLoss = glm::max(maxLoss, loss);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.75f, 0.6f, 0.15f, 1.0f));
		ImGui::PlotHistogram("Loss", network.m_RMSEHistory.data(), (int)network.m_RMSEHistory.size(), 0,
			nullptr, 0.0f, maxLoss, ImVec2(0, 100));
		ImGui::PopStyleColor();
	}

	float RandomFloatNormalized(std::uniform_real_distribution<real>& distribution, std::mt19937& rng)
	{
		real result = distribution(rng);
		return result;
	}

	// Returns [0, inf]
	real ReLU(real x)
	{
		return glm::max(x, 0.0f);
	}

	// Returns [-inf, inf]
	real LeakyReLU(real x)
	{
		return glm::max(x * 0.1f, x);
	}

	// Compresses to [0, 1] smoothly
	real Sigmoid(real x)
	{
		real result = 1.0f / (1.0f + glm::pow(e, -x));
		CHECK(!IsNanOrInf(result));
		return result;
	}

	// Compresses to [-1, 1], can work better in practice than Sigmoid
	real Tanh(real x)
	{
		real a = glm::pow(e, x);
		real b = glm::pow(e, -x);
		CHECK(!IsNanOrInf(a));
		CHECK(!IsNanOrInf(b));
		return (a - b) / (a + b);
	}

	real TanhPrime(real x)
	{
		real a = Tanh(x);
		real result = 1.0f - a * a;
		return result;
	}

	real ComputeActivationFunc(ActivationFunc func, real x)
	{
		switch (func)
		{
		case ActivationFunc::RELU: return ReLU(x);
		case ActivationFunc::LEAKY_RELU: return LeakyReLU(x);
		case ActivationFunc::SIGMOID: return Sigmoid(x);
		case ActivationFunc::TANH: return Tanh(x);
		}

		ENSURE_NO_ENTRY();
		return -1.0f;
	}

	real ComputeActivationFuncPrime(ActivationFunc func, real x)
	{
		switch (func)
		{
		case ActivationFunc::RELU: return ReLU(x);// TODO
		case ActivationFunc::LEAKY_RELU: return LeakyReLU(x);// TODO
		case ActivationFunc::SIGMOID: return Sigmoid(x); // TODO
		case ActivationFunc::TANH: return TanhPrime(x);
		}

		ENSURE_NO_ENTRY();
		return -1.0f;
	}

	// Returns MSE
	real CalculateLoss(const std::vector<real>& predicated, const std::vector<real>& expected)
	{
		const u32 layerSize = (u32)predicated.size();

		real diff2 = 0.0f;
		for (u32 i = 0; i < layerSize; ++i)
		{
			real diff = (predicated[i] - expected[i]);
			diff2 += diff * diff;
		}

		real mse = diff2 / layerSize;
		return mse;
	}

	std::vector<real> CalculateLossPrime(const std::vector<real>& predicated, const std::vector<real>& expected)
	{
		CHECK_EQ(predicated.size(), expected.size());
		const u32 layerSize = (u32)predicated.size();

		std::vector<real> output;
		output.resize(layerSize);

		for (u32 i = 0; i < layerSize; ++i)
		{
			output[i] = 2.0f * (predicated[i] - expected[i]) / layerSize;
		}
		return output;
	}

	// Applies weights, returns neuron values
	std::vector<real> ForwardPropagation(Layer& layer, const std::vector<real>& input)
	{
		CHECK_EQ(layer.m_InputSize, (u32)input.size());

		std::vector<real> output;
		output.resize(layer.m_OutputSize, 0.0f);

		layer.m_Inputs = input;

		switch (layer.m_Type)
		{
		case LayerType::FULLY_CONNECTED:
		{
			for (u32 n = 0; n < layer.m_OutputSize; ++n)
			{
				real result = 0.0f;
				for (u32 p = 0; p < layer.m_InputSize; ++p)
				{
					u32 weightIndex = p + n * layer.m_InputSize;
					result += input[p] * layer.m_Weights[weightIndex];
				}
				result += layer.m_Bias;

				output[n] = result;

			}
		} break;
		case LayerType::ACTIVATION:
		{
			for (u32 p = 0; p < layer.m_InputSize; ++p)
			{
				output[p] = ComputeActivationFunc(layer.m_ActivationFunc, input[p]);
			}
		} break;
		}

		return output;
	}

	std::vector<real> Dot(const std::vector<real>& a, const std::vector<real>& b, u32 aWidth, u32 aHeight)
	{
		std::vector<real> output;
		output.resize(a.size());
		for (u32 i = 0; i < (u32)a.size(); ++i)
		{
			output[i] = a[i] * b[i / aWidth];
		}

		return output;
	}

	std::vector<real> Transpose(const std::vector<real>& a, u32 width, u32 height)
	{
		CHECK_EQ(a.size(), (size_t)width * height);

		std::vector<real> output;
		output.resize(height * width);

		for (u32 n = 0; n < width * height; ++n)
		{
			u32 y = n % height;
			u32 x = n / height;
			u32 index = x + y * width;
			output[n] = a[index];
		}

		return output;
	}

	// Returns gradient of error with respect to input?
	std::vector<real> BackPropagation(Layer& layer, const std::vector<real>& outputErrorGradient, real learningRate)
	{
		std::vector<real> inputError;

		switch (layer.m_Type)
		{
		case LayerType::FULLY_CONNECTED:
		{
			//inputError.resize(layer.m_InputSize * layer.m_OutputSize);

			std::vector<real> weightsError;
			weightsError.resize(layer.m_InputSize * layer.m_OutputSize);

			std::vector<real> transposedWeights = Transpose(layer.m_Weights, layer.m_InputSize, layer.m_OutputSize);

			inputError = Dot(transposedWeights, outputErrorGradient, layer.m_InputSize, layer.m_OutputSize);
			//for (u32 n = 0; n < (u32)layer.m_Weights.size(); ++n)
			//{
			//	//
			//	// in: 3, out: 4
			//	// x x x x
			//	// x x x x
			//	// x x x x
			//	//
			//	// in: 4, out: 3
			//	// x x x
			//	// x x x
			//	// x x x
			//	// x x x
			//	//
			//	// in: 3, out: 1
			//	// x
			//	// x
			//	// x
			//	//
			//	// in: 1, out: 3
			//	// x x x
			//	//

			//	u32 x = n % layer.m_OutputSize; // ?
			//	u32 y = n / layer.m_OutputSize;
			//	u32 weightIndex = x * layer.m_OutputSize + y; // Transpose
			//	inputError[n] = layer.m_Weights[weightIndex] * outputErrorGradient[n];
			//}

			for (u32 n = 0; n < (u32)layer.m_Weights.size(); ++n)
			{
				//u32 x = n / layer.m_OutputSize;
				//u32 y = n % layer.m_OutputSize;
				//u32 inputIndex = x + y * layer.m_OutputSize; // Transpose
				// TODO: Transpose?
				weightsError[n] = layer.m_Inputs[n % layer.m_InputSize] * outputErrorGradient[n % layer.m_OutputSize];
			}

			for (u32 n = 0; n < (u32)layer.m_Weights.size(); ++n)
			{
				layer.m_Weights[n] -= learningRate * weightsError[n];
				layer.m_Bias -= learningRate * outputErrorGradient[n % layer.m_OutputSize];
			}
		} break;
		case LayerType::ACTIVATION:
		{
			inputError.resize(layer.m_InputSize);

			for (u32 n = 0; n < layer.m_InputSize; ++n)
			{
				real neuronError = ComputeActivationFuncPrime(layer.m_ActivationFunc, layer.m_Inputs[n]);
				inputError[n] = neuronError * outputErrorGradient[n % layer.m_OutputSize]; // Nothing learnable here - don't use learning rate
			}
		} break;
		}

		return inputError;
	}

	std::vector<real> RunForwardPropagation(Network& network,
		const std::vector<real>& inputNeurons)
	{
		CHECK_GE(network.m_Layers.size(), 2);
		CHECK_EQ(inputNeurons.size(), (size_t)network.m_Layers[0].m_InputSize);

		std::vector<real> intermediateVec = inputNeurons;
		for (u32 layerIndex = 0; layerIndex < (u32)network.m_Layers.size(); ++layerIndex)
		{
			intermediateVec = ForwardPropagation(network.m_Layers[layerIndex], intermediateVec);
		}
		return intermediateVec;
	}

	void RunBackPropagation(Network& network,
		const std::vector<real>& expected,
		const std::vector<real>& predicted,
		real learningRate)
	{
		//// 0 1 2 3 4 5
		//// 0 3 1 4 2 5
		//std::vector<real> a = {
		//	1, 2, 3,
		//	4, 5, 6,
		//};
		//std::vector<real> r = Transpose(a, 3, 2);
		//std::vector<real> r_exp{ 1, 4, 2, 5, 3, 6 };
		//CHECK_EQ(r, r_exp);

		//a = std::vector<real>{
		//	1, 2, 3,
		//	4, 5, 6,
		//	7, 8, 9,
		//	10, 11, 12,
		//};
		//r = Transpose(a, 3, 4);
		//r_exp = std::vector<real>{ 1, 4, 7, 10, 2, 5, 8, 11, 3, 6, 9, 12 };
		//CHECK_EQ(r, r_exp);

		//a = std::vector<real>{
		//	1,
		//	2,
		//	3,
		//};
		//r = Transpose(a, 1, 3);
		//r_exp = std::vector<real>{ 1, 2, 3 };
		//CHECK_EQ(r, r_exp);

		//std::vector<real> b = { 10 };
		//r = Dot(a, b, 3, 1);
		//r_exp = std::vector<real>{ 10, 20, 30 };
		//CHECK_EQ(r, r_exp);

		std::vector<real> errorGradient = CalculateLossPrime(predicted, expected);

		for (i32 i = (i32)network.m_Layers.size() - 1; i >= 0; --i)
		{
			errorGradient = BackPropagation(network.m_Layers[i], errorGradient, learningRate);
		}
	}
} // namespace flex
