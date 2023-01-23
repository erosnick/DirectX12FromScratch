#include "pch.h"
#include "WICImage.h"
#include "Utils.h"

ComPtr<IWICImagingFactory>			WICImagingFactory;
ComPtr<IWICBitmapDecoder>			WICBitmapDecoder;
ComPtr<IWICBitmapFrameDecode>		WICBitmapFrameDecode;

bool GetTargetPixelFormat(const GUID* pSourceFormat, GUID* pTargetFormat)
{	
	//查表确定兼容的最接近格式是哪个
	*pTargetFormat = *pSourceFormat;

	for (size_t i = 0; i < _countof(g_WICConvert); ++i)
	{
		if (InlineIsEqualGUID(g_WICConvert[i].source, *pSourceFormat))
		{
			*pTargetFormat = g_WICConvert[i].target;
			return true;
		}
	}
	return false;
}

DXGI_FORMAT GetDXGIFormatFromPixelFormat(const GUID* pPixelFormat)
{
	//查表确定最终对应的DXGI格式是哪一个
	for (size_t i = 0; i < _countof(g_WICFormats); ++i)
	{
		if (InlineIsEqualGUID(g_WICFormats[i].wic, *pPixelFormat))
		{
			return g_WICFormats[i].format;
		}
	}
	return DXGI_FORMAT_UNKNOWN;
}

ImageData loadImage(const std::wstring& path)
{
	// 使用WIC创建并加载一个2D纹理
	// 使用纯COM方式创建WIC类厂对象，也是调用WIC第一步要做的事情
	DXCheck(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&WICImagingFactory)), L"CoCreateInstance failed!");
	
	DXCheck(WICImagingFactory->CreateDecoderFromFilename(
		path.c_str(),						// 图片文件名
	   nullptr,							// 不指定解码器，使用默认
				 GENERIC_READ,						// 访问权限
				 WICDecodeMetadataCacheOnDemand,	// 若需要就缓存数据
				 &WICBitmapDecoder					// 解码器对象
				), L"WICImagingFactory::CreateDecoderFromFilename failed!");

	// 获取第一帧图片(因为GIF等格式文件可能会有多帧图片，其他的格式一般只有一帧图片)
	// 实际解析出来的往往是位图格式数据
	DXCheck(WICBitmapDecoder->GetFrame(0, &WICBitmapFrameDecode), L"IWICBitmapDecoder::GetFrame failed!");

	WICPixelFormatGUID pixelFormatGUID{};

	// 获取WIC图片格式
	DXCheck(WICBitmapFrameDecode->GetPixelFormat(&pixelFormatGUID), L"IWICBitmapFrameDecode::GetPixelFormat failed!");

	GUID format{};

	DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;

	// 通过第一道转换之后获取DXGI的等价格式
	if (GetTargetPixelFormat(&pixelFormatGUID, &format))
	{
		textureFormat = GetDXGIFormatFromPixelFormat(&format);
	}

	if (textureFormat == DXGI_FORMAT_UNKNOWN)
	{
		// 不支持的图片格式 目前退出了事 
		// 一般 在实际的引擎当中都会提供纹理格式转换工具，
		// 图片都需要提前转换好，所以不会出现不支持的现象
		DXThrow(HRESULT_FROM_WIN32(GetLastError()), L"Unknow texture format!");
	}

	// 定义一个位图格式的图片数据对象接口
	ImageData imageData;

	imageData.format = textureFormat;

	if (!InlineIsEqualGUID(pixelFormatGUID, format))
	{
		// 这个判断很重要，如果原WIC格式不是直接能转换为DXGI格式的图片时
		// 我们需要做的就是转换图片格式为能够直接对应DXGI格式的形式
		// 创建图片格式转换器
		ComPtr<IWICFormatConverter> converter;
		DXCheck(WICImagingFactory->CreateFormatConverter(&converter), L"IWICImagingFactory::CreateFormatConverter failed!");

		//初始化一个图片转换器，实际也就是将图片数据进行了格式转换
		DXCheck(converter->Initialize(
			WICBitmapFrameDecode.Get(),		// 输入原图片数据
		   format,							// 指定待转换的目标格式
					WICBitmapDitherTypeNone,		// 指定位图是否有调色板，现在都为真彩位图，不用调色板，所以为None
				    nullptr,				// 指定调色板指针
					0.0f,			// 指定Alpha阈值
	  WICBitmapPaletteTypeCustom		// 调色板类型，实际没有使用，所以指定为Custom
		), L"IWICFormatConverter::Initialize failed!");

		// 调用QueryInterface方法获得对象的位图数据源接口
		DXCheck(converter.As(&imageData.data), L"Convert bitmap failed!");
	}
	else
	{
		//图片数据格式不需要转换，直接获取其位图数据源接口
		DXCheck(WICBitmapFrameDecode.As(&imageData.data), L"Convert bitmap failed!");
	}

	// 获得图片大小（单位：像素）
	DXCheck(imageData.data->GetSize(&imageData.width, &imageData.height), L"IWICBitmapSource::GetSize failed!");

	// 获取图片像素的位大小的BPP（Bits Per Pixel）信息，用以计算图片行数据的真实大小（单位：字节）
	ComPtr<IWICComponentInfo> WICComponentInfo;
	DXCheck(WICImagingFactory->CreateComponentInfo(format, WICComponentInfo.GetAddressOf()), L"IWICImagingFactory::CreateComponentInfo failed!");

	WICComponentType type;
	DXCheck(WICComponentInfo->GetComponentType(&type), L"IWICComponentInfo::GetComponentType failed!");

	if (type != WICPixelFormat)
	{
		DXThrow(HRESULT_FROM_WIN32(GetLastError()), L"Type mismatch!");
	}

	ComPtr<IWICPixelFormatInfo> WICPixelInfo;
	DXCheck(WICComponentInfo.As(&WICPixelInfo), L"Get pixel info failed!");

	uint32_t bpp = 0;

	// 到这里终于可以得到BPP了，这也是我看的比较吐血的地方，为了BPP居然饶了这么多环节
	DXCheck(WICPixelInfo->GetBitsPerPixel(&imageData.bpp), L"IWICComponentInfo::GetBitsPerPixel failed!");

	// 计算图片实际的行大小（单位：字节），这里使用了一个上取整除法即（A + B - 1）/ B ，
	// 这曾经被传说是微软的面试题,希望你已经对它了如指掌
	imageData.rowPitch = UPPER_DIV(imageData.width * imageData.bpp, 8);

	return imageData;
}