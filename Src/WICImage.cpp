#include "pch.h"
#include "WICImage.h"
#include "Utils.h"

ComPtr<IWICImagingFactory>			WICImagingFactory;
ComPtr<IWICBitmapDecoder>			WICBitmapDecoder;
ComPtr<IWICBitmapFrameDecode>		WICBitmapFrameDecode;

bool GetTargetPixelFormat(const GUID* pSourceFormat, GUID* pTargetFormat)
{	
	//���ȷ�����ݵ���ӽ���ʽ���ĸ�
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
	//���ȷ�����ն�Ӧ��DXGI��ʽ����һ��
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
	// ʹ��WIC����������һ��2D����
	// ʹ�ô�COM��ʽ����WIC�೧����Ҳ�ǵ���WIC��һ��Ҫ��������
	DXCheck(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&WICImagingFactory)), L"CoCreateInstance failed!");
	
	DXCheck(WICImagingFactory->CreateDecoderFromFilename(
		path.c_str(),						// ͼƬ�ļ���
	   nullptr,							// ��ָ����������ʹ��Ĭ��
				 GENERIC_READ,						// ����Ȩ��
				 WICDecodeMetadataCacheOnDemand,	// ����Ҫ�ͻ�������
				 &WICBitmapDecoder					// ����������
				), L"WICImagingFactory::CreateDecoderFromFilename failed!");

	// ��ȡ��һ֡ͼƬ(��ΪGIF�ȸ�ʽ�ļ����ܻ��ж�֡ͼƬ�������ĸ�ʽһ��ֻ��һ֡ͼƬ)
	// ʵ�ʽ���������������λͼ��ʽ����
	DXCheck(WICBitmapDecoder->GetFrame(0, &WICBitmapFrameDecode), L"IWICBitmapDecoder::GetFrame failed!");

	WICPixelFormatGUID pixelFormatGUID{};

	// ��ȡWICͼƬ��ʽ
	DXCheck(WICBitmapFrameDecode->GetPixelFormat(&pixelFormatGUID), L"IWICBitmapFrameDecode::GetPixelFormat failed!");

	GUID format{};

	DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;

	// ͨ����һ��ת��֮���ȡDXGI�ĵȼ۸�ʽ
	if (GetTargetPixelFormat(&pixelFormatGUID, &format))
	{
		textureFormat = GetDXGIFormatFromPixelFormat(&format);
	}

	if (textureFormat == DXGI_FORMAT_UNKNOWN)
	{
		// ��֧�ֵ�ͼƬ��ʽ Ŀǰ�˳����� 
		// һ�� ��ʵ�ʵ����浱�ж����ṩ�����ʽת�����ߣ�
		// ͼƬ����Ҫ��ǰת���ã����Բ�����ֲ�֧�ֵ�����
		DXThrow(HRESULT_FROM_WIN32(GetLastError()), L"Unknow texture format!");
	}

	// ����һ��λͼ��ʽ��ͼƬ���ݶ���ӿ�
	ImageData imageData;

	imageData.format = textureFormat;

	if (!InlineIsEqualGUID(pixelFormatGUID, format))
	{
		// ����жϺ���Ҫ�����ԭWIC��ʽ����ֱ����ת��ΪDXGI��ʽ��ͼƬʱ
		// ������Ҫ���ľ���ת��ͼƬ��ʽΪ�ܹ�ֱ�Ӷ�ӦDXGI��ʽ����ʽ
		// ����ͼƬ��ʽת����
		ComPtr<IWICFormatConverter> converter;
		DXCheck(WICImagingFactory->CreateFormatConverter(&converter), L"IWICImagingFactory::CreateFormatConverter failed!");

		//��ʼ��һ��ͼƬת������ʵ��Ҳ���ǽ�ͼƬ���ݽ����˸�ʽת��
		DXCheck(converter->Initialize(
			WICBitmapFrameDecode.Get(),		// ����ԭͼƬ����
		   format,							// ָ����ת����Ŀ���ʽ
					WICBitmapDitherTypeNone,		// ָ��λͼ�Ƿ��е�ɫ�壬���ڶ�Ϊ���λͼ�����õ�ɫ�壬����ΪNone
				    nullptr,				// ָ����ɫ��ָ��
					0.0f,			// ָ��Alpha��ֵ
	  WICBitmapPaletteTypeCustom		// ��ɫ�����ͣ�ʵ��û��ʹ�ã�����ָ��ΪCustom
		), L"IWICFormatConverter::Initialize failed!");

		// ����QueryInterface������ö����λͼ����Դ�ӿ�
		DXCheck(converter.As(&imageData.data), L"Convert bitmap failed!");
	}
	else
	{
		//ͼƬ���ݸ�ʽ����Ҫת����ֱ�ӻ�ȡ��λͼ����Դ�ӿ�
		DXCheck(WICBitmapFrameDecode.As(&imageData.data), L"Convert bitmap failed!");
	}

	// ���ͼƬ��С����λ�����أ�
	DXCheck(imageData.data->GetSize(&imageData.width, &imageData.height), L"IWICBitmapSource::GetSize failed!");

	// ��ȡͼƬ���ص�λ��С��BPP��Bits Per Pixel����Ϣ�����Լ���ͼƬ�����ݵ���ʵ��С����λ���ֽڣ�
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

	// ���������ڿ��Եõ�BPP�ˣ���Ҳ���ҿ��ıȽ���Ѫ�ĵط���Ϊ��BPP��Ȼ������ô�໷��
	DXCheck(WICPixelInfo->GetBitsPerPixel(&imageData.bpp), L"IWICComponentInfo::GetBitsPerPixel failed!");

	// ����ͼƬʵ�ʵ��д�С����λ���ֽڣ�������ʹ����һ����ȡ����������A + B - 1��/ B ��
	// ����������˵��΢���������,ϣ�����Ѿ���������ָ��
	imageData.rowPitch = UPPER_DIV(imageData.width * imageData.bpp, 8);

	return imageData;
}