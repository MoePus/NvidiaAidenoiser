// AIdenoiser.cpp: 定义控制台应用程序的入口点。
//
#define NOMINMAX
#define WIN32
#include <optix_world.h>
#include <iostream>
#include "..\LodePng\lodepng.h"
#include "..\LodePng\lodepng_util.h"

unsigned int decodeU32BE(unsigned char* buffer)
{
	return (*(buffer) << 24) +
		((*(buffer + 1)) << 16) +
		((*(buffer + 2)) << 8) +
		(*(buffer + 3));
}

UINT16 TranslateU16BE(UINT16 in)
{
	return ((in & 0xff) << 8) | (in >> 8);
}

template<typename T>
void T2float(T* in, float* out,size_t len)
{
	constexpr float maxT = (1 << (sizeof(T) * 8)) - 1;
	for (size_t i = 0; i < len; i++)
	{
		if (maxT == 65535)
			out[i] = TranslateU16BE(in[i]) / maxT;
		else
			out[i] = in[i] / maxT;
	}
}

template<typename T>
void float2T(T* out, float* in, size_t len)
{
	constexpr float maxT = (1 << (sizeof(T) * 8)) - 1;
	for (size_t i = 0; i < len; i++)
	{
		if(maxT == 65535)
			out[i] = TranslateU16BE(fminf(maxT,roundf(in[i] * maxT)));
		else
			out[i] = fminf(maxT, roundf(in[i] * maxT));
	}
}

void fillOptixBuffer(optix::Buffer buffer, UINT8* raw, size_t len,int depth)
{
	if (depth == 8)
	{
		T2float<UINT8>((UINT8*)raw, (float*)buffer->map(), len);
	}
	else
	{
		T2float<UINT16>((UINT16*)raw, (float*)buffer->map(), len);
	}
	buffer->unmap();
}

void pourOptixBuffer(optix::Buffer buffer, UINT8* raw, size_t len, int depth)
{
	if (depth == 8)
	{
		float2T<UINT8>((UINT8*)raw, (float*)buffer->map(), len);
	}
	else
	{
		float2T<UINT16>((UINT16*)raw, (float*)buffer->map(), len);
	}
	buffer->unmap();
}

int main(int argc,char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Must specify the input." << std::endl;
	}
	unsigned width, height, depth;
	unsigned char* png = 0, *image = 0;
	size_t pngsize;

	int error = 1;
	if (!lodepng_load_file(&png, &pngsize, argv[1]))
	{
		if (pngsize > 0x20)
		{
			if (!memcmp(&png[0xC], "IHDR", 4))
			{
				depth = png[0x18];
				if (depth == 8 || depth == 16)
				{
					error = 0;
				}
			}
		}
	}
	if (error)
	{
		std::cout << "Something error." << std::endl;
		return EXIT_FAILURE;
	}

	lodepng_decode_memory(&image, &width, &height, png, pngsize, LodePNGColorType::LCT_RGBA, depth);
	free(png);

	if(true)
	try
	{
		optix::Context optix_context = optix::Context::create();
		optix::Buffer noisy = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);
		optix::Buffer albedo = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, 0, 0);
		optix::Buffer normal = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, 0, 0);
		optix::Buffer clean = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);

		fillOptixBuffer(noisy, image, width * height * 4, depth);

		optix::PostprocessingStage denoiserStage = optix_context->createBuiltinPostProcessingStage("DLDenoiser");
		denoiserStage->declareVariable("input_buffer")->set(noisy);
		denoiserStage->declareVariable("output_buffer")->set(clean);
		//denoiserStage->declareVariable("blend")->setFloat(0);
		//denoiserStage->declareVariable("input_albedo_buffer")->set(albedo);
		//denoiserStage->declareVariable("input_normal_buffer")->set(normal);

		optix::CommandList commandList = optix_context->createCommandList();
		commandList->appendPostprocessingStage(denoiserStage, width, height);
		commandList->finalize();
		commandList->execute();
		commandList->destroy();
		pourOptixBuffer(clean, image, width * height * 4, depth);

		clean->destroy();
		noisy->destroy();
		//normal->destroy();
		//albedo->destroy();
		optix_context->destroy();
	}
	catch (std::exception e)
	{
		std::cerr << "[OptiX]: " << e.what() << std::endl;

		return EXIT_FAILURE;
	}

	png = 0; pngsize = 0;

	lodepng_encode_memory(&png, &pngsize, image, width, height, LodePNGColorType::LCT_RGBA, depth);
	lodepng_save_file(png, pngsize, argv[1]);
	std::cout << "Successfully processed " << argv[1] << "." << std::endl;
	free(png);
    return 0;
}

