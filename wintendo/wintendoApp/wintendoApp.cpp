#define IMGUI_ENABLE
#define MAX_LOADSTRING ( 100 )

#include <windows.h>
#include <combaseapi.h>
#include <wrl/client.h>
#include <shobjidl.h> 
#include <ole2.h>
#include <ObjBase.h>
#include <comdef.h>
#include <wincodec.h>
#include "stdafx.h"
#include "..\wintendoCore\common.h"
#include "..\wintendoCore\NesSystem.h"
#include "..\wintendoCore\input.h"
#include "wintendoApp.h"
#include "wintendoApp_dx12.h"
#ifdef IMGUI_ENABLE
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_memory_editor/imgui_memory_editor.h"
#endif

using namespace DirectX;
using Microsoft::WRL::ComPtr;

HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

ATOM                RegisterWindow(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

struct DisplayConstantBuffer
{
	// Hardness of scanline.
	//  -8.0 = soft
	// -16.0 = medium
	float		hardScan;
	// Hardness of pixels in scanline.
	// -2.0 = soft
	// -4.0 = hard
	float		hardPix;
	// Display warp.
	// 0.0 = none
	// 1.0/8.0 = extreme
	XMFLOAT2	warp;
	XMFLOAT4	imageDim;
	XMFLOAT4	destImageDim;
	// Amount of shadow mask.
	float		maskDark;
	float		maskLight;
	bool		enable;
};

struct Vertex
{
	XMFLOAT3 position;
	XMFLOAT4 color;
	XMFLOAT2 uv;
};

struct wtApp_D3D12TextureResource
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE	cpuHandle;
	CD3DX12_GPU_DESCRIPTOR_HANDLE	gpuHandle;

	ComPtr<ID3D12Resource>			srv;
	ComPtr<ID3D12Resource>			uploadBuffer;

	const wtRawImageInterface* srcImage;
	D3D12_RESOURCE_ALLOCATION_INFO	allocInfo;
	D3D12_RESOURCE_DESC				desc;
};

struct wtAppDisplay
{
	HWND hWnd;
};

template<uint32_t T>
struct wtImguiGraphBuffer
{
	static const uint32_t bufferSize = T;
	float samples[T];
	uint32_t start;

	wtImguiGraphBuffer()
	{
		start = 0;
	}

	void Record( const float sample )
	{
		samples[start] = sample;
		start = ( start + 1 ) % bufferSize;
	}

	float* GetSamplesBuffer()
	{
		return samples;
	}

	uint32_t GetBufferStart()
	{
		return start;
	}

	uint32_t GetBufferSize()
	{
		return bufferSize;
	}

	float GetSample( const int32_t index )
	{
		return GetSample( samples, index % bufferSize );
	}

	static float GetSample( void* data, int32_t idx )
	{
		return ( (float*)data )[idx];
	}
};

ComPtr<IDXGIAdapter1>				dxgiAdapter;
ComPtr<IDXGIFactory4>				dxgiFactory;
ComPtr<ID3D12Device>				d3d12device;

static const uint32_t				FrameCount = 2;
uint32_t							currentFrame = 0;
CD3DX12_VIEWPORT					viewport;
CD3DX12_RECT						scissorRect;
DXGI_SWAP_CHAIN_DESC1				swapChainDesc = { 0 };
ComPtr<IDXGISwapChain3>				dxgiSwapChain;
ComPtr<ID3D12Resource>				renderTargets[FrameCount];
ComPtr<ID3D12DescriptorHeap>		rtvHeap;
uint32_t							rtvDescriptorSize;
ComPtr<ID3D12RootSignature>			rootSignature;
ComPtr<ID3D12PipelineState>			pipelineState;

HANDLE								fenceEvent;
ComPtr<ID3D12Fence>					fence;
UINT64								fenceValues[FrameCount] = { 0, 0 };
volatile uint64_t					frameBufferCpyLock = 0;
HANDLE								frameCopySemaphore;

ComPtr<ID3D12CommandQueue>			d3d12CommandQueue;
ComPtr<ID3D12CommandQueue>			d3d12CopyQueue;
ComPtr<ID3D12CommandAllocator>		commandAllocator[FrameCount];
ComPtr<ID3D12CommandAllocator>		cpyCommandAllocator[FrameCount];
ComPtr<ID3D12CommandAllocator>		imguiCommandAllocator[FrameCount];
ComPtr<ID3D12GraphicsCommandList>	commandList[FrameCount];
ComPtr<ID3D12GraphicsCommandList>	cpyCommandList[FrameCount];
ComPtr<ID3D12GraphicsCommandList>	imguiCommandList[FrameCount];

ComPtr<ID3D12Resource>				constantBuffer;
DisplayConstantBuffer				constantBufferData;
ComPtr<ID3D12Resource>				vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW			vertexBufferView;
UINT8*								pCbvDataBegin;
UINT								cbvSrvUavDescriptorSize;
ComPtr<ID3D12DescriptorHeap>		cbvSrvUavHeap;
CD3DX12_CPU_DESCRIPTOR_HANDLE		cbvSrvCpuHandle;
CD3DX12_GPU_DESCRIPTOR_HANDLE		cbvSrvGpuHandle;

const uint32_t texturePixelSize		= 4;
const int32_t displayScalar			= 2;
const int32_t nesWidth				= 256;
const int32_t nesHeight				= 240;
const int32_t debugAreaX			= 1024;
const int32_t debugAreaY			= 0;
const int32_t defaultWidth			= displayScalar * nesWidth + debugAreaX;
const int32_t defaultHeight			= displayScalar * nesHeight + debugAreaY;

static bool reset = true;
static bool pause = false;
static bool emulatorRunning = true;

std::wstring nesFilePath( L"Games/Contra.nes" );
std::wstring shaderPath( L"Shaders/" );

wtAppDisplay	appDisplay;
wtFrameResult	frameResult;

using frameSampleBuffer = wtImguiGraphBuffer<500>;
frameSampleBuffer frameTimePlot;

std::vector<wtApp_D3D12TextureResource> textureResources;

wtSystem nesSystem;

enum ShaderResources
{
	SHADER_RESOURES_IMGUI,
	SHADER_RESOURES_SRV0,
	SHADER_RESOURES_SRV1,
	SHADER_RESOURES_SRV2,
	SHADER_RESOURES_SRV3,
	SHADER_RESOURES_SRV4,
	SHADER_RESOURES_SRV_CNT,
	SHADER_RESOURES_CBV0 = SHADER_RESOURES_SRV_CNT,
	SHADER_RESOURES_COUNT,
};

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

static inline const float NormalizeCoordinate( const uint32_t x, const uint32_t length )
{
	return ( x / static_cast<float>( length ) );
}

DWORD WINAPI EmulatorThread( LPVOID lpParameter )
{
	while( emulatorRunning )
	{
		if ( reset )
		{
			nesSystem.InitSystem( nesFilePath );
			reset = false;
		}

		if( !pause )
		{
			emulatorRunning = emulatorRunning && nesSystem.RunFrame();
		}

		//DWORD dwWaitResult = WaitForSingleObject( frameCopySemaphore, 16 );

		while ( InterlockedCompareExchange( &frameBufferCpyLock, 1, 0 ) == 1 )
		{
			RedrawWindow( appDisplay.hWnd, NULL, NULL, RDW_INVALIDATE );
			Sleep(1);
		}

		nesSystem.GetFrameResult( frameResult ); // FIXME: Timing issue, can be writing while doing GPU upload buffer copy

		RedrawWindow( appDisplay.hWnd, NULL, NULL, RDW_INVALIDATE );
	}

	return 0;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

	using namespace std;

	unsigned int sharedData = 0;

	frameCopySemaphore = CreateSemaphore( NULL,	0, 1, NULL );

	DWORD threadID;
	HANDLE emulatorThreadHandle = CreateThread( 0, 0, EmulatorThread, &sharedData, 0, &threadID );

	if ( emulatorThreadHandle <= 0 )
		return 0;

    LoadStringW( hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING );
    LoadStringW( hInstance, IDC_WINTENDOAPP, szWindowClass, MAX_LOADSTRING );
	RegisterWindow( hInstance );

    if ( !InitInstance ( hInstance, nCmdShow ) )
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators( hInstance, MAKEINTRESOURCE( IDC_WINTENDOAPP ) );

    MSG msg;

	// Main message loop:
	while ( GetMessage( &msg, nullptr, 0, 0 ) )
	{
		if ( !TranslateAccelerator( msg.hwnd, hAccelTable, &msg ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
	}

	emulatorRunning = false;
	WaitForSingleObject( emulatorThreadHandle, 16 );

	CloseHandle( emulatorThreadHandle );
	CloseHandle( frameCopySemaphore );

    return (int)msg.wParam;
}


ATOM RegisterWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof( WNDCLASSEX );

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon( hInstance, MAKEINTRESOURCE( IDI_WINTENDOAPP ) );
    wcex.hCursor        = LoadCursor( nullptr, IDC_ARROW );
    wcex.hbrBackground  = (HBRUSH)( COLOR_WINDOW + 1 );
    wcex.lpszMenuName   = MAKEINTRESOURCEW( IDC_WINTENDOAPP );
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon( wcex.hInstance, MAKEINTRESOURCE( IDI_SMALL ) );

    return RegisterClassExW( &wcex );
}


std::wstring GetAssetFullPath( LPCWSTR assetName )
{
	return shaderPath + assetName;
}


void WaitForGpu()
{
	ThrowIfFailed( d3d12CommandQueue->Signal( fence.Get(), fenceValues[currentFrame] ) );

	ThrowIfFailed( fence->SetEventOnCompletion( fenceValues[currentFrame], fenceEvent ) );
	WaitForSingleObjectEx( fenceEvent, INFINITE, FALSE );

	fenceValues[currentFrame]++;
}


void AdvanceNextFrame()
{
	const UINT64 currentFence = fenceValues[currentFrame];
	ThrowIfFailed( d3d12CommandQueue->Signal( fence.Get(), currentFence ) );

	currentFrame = dxgiSwapChain->GetCurrentBackBufferIndex();

	if ( fence->GetCompletedValue() < fenceValues[currentFrame] )
	{
		ThrowIfFailed( fence->SetEventOnCompletion( fenceValues[currentFrame], fenceEvent ) );
		WaitForSingleObject( fenceEvent, INFINITE );
	}

	fenceValues[currentFrame] = currentFence + 1;
}


void CreateFrameBuffers()
{
	swapChainDesc.Width = 0;
	swapChainDesc.Height = 0;
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = 0;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed( dxgiFactory->CreateSwapChainForHwnd( d3d12CommandQueue.Get(), appDisplay.hWnd, &swapChainDesc, nullptr, nullptr, &swapChain ) );

	ThrowIfFailed( swapChain.As( &dxgiSwapChain ) );
	currentFrame = dxgiSwapChain->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed( d3d12device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( &rtvHeap ) ) );

	rtvDescriptorSize = d3d12device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvHeap->GetCPUDescriptorHandleForHeapStart() );
	for ( uint32_t i = 0; i < FrameCount; ++i )
	{
		ThrowIfFailed( dxgiSwapChain->GetBuffer( i, IID_PPV_ARGS( &renderTargets[i] ) ) );
		d3d12device->CreateRenderTargetView( renderTargets[i].Get(), nullptr, rtvHandle );
		rtvHandle.Offset( 1, rtvDescriptorSize );
	}
}


void InitImgui()
{
#ifdef IMGUI_ENABLE
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init( appDisplay.hWnd );
	ImGui_ImplDX12_Init( d3d12device.Get(), FrameCount,
		DXGI_FORMAT_R8G8B8A8_UNORM, cbvSrvUavHeap.Get(),
		cbvSrvUavHeap.Get()->GetCPUDescriptorHandleForHeapStart(),
		cbvSrvUavHeap.Get()->GetGPUDescriptorHandleForHeapStart() );
#endif
}


uint32_t InitD3D12()
{
	RECT rc;
	GetClientRect( appDisplay.hWnd, &rc );

	const int32_t windowWidth = ( rc.right - rc.left );
	const int32_t windowHeight = ( rc.bottom - rc.top );

	viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( windowWidth ), static_cast<float>( windowHeight ) );
	scissorRect = CD3DX12_RECT( 0, 0, windowWidth, windowHeight );

	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
	{
		debugController->EnableDebugLayer();
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	ThrowIfFailed( CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &dxgiFactory ) ) );
	GetHardwareAdapter( dxgiFactory.Get(), &dxgiAdapter );
	ThrowIfFailed( D3D12CreateDevice( dxgiAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &d3d12device ) ) );

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed( d3d12device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &d3d12CommandQueue ) ) );

	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	ThrowIfFailed( d3d12device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &d3d12CopyQueue ) ) );

	CreateFrameBuffers();

	D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
	cbvSrvUavHeapDesc.NumDescriptors = SHADER_RESOURES_COUNT;
	cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed( d3d12device->CreateDescriptorHeap( &cbvSrvUavHeapDesc, IID_PPV_ARGS( &cbvSrvUavHeap ) ) );
	cbvSrvUavDescriptorSize = d3d12device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

	for ( uint32_t i = 0; i < FrameCount; ++i )
	{
		ThrowIfFailed( d3d12device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &commandAllocator[i] ) ) );
		ThrowIfFailed( d3d12device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_COPY,	IID_PPV_ARGS( &cpyCommandAllocator[i] ) ) );
		ThrowIfFailed( d3d12device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &imguiCommandAllocator[i] ) ) );
	}

	return 0;
}


void CreatePSO()
{
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

	// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if ( FAILED( d3d12device->CheckFeatureSupport( D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof( featureData ) ) ) )
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	const uint32_t srvCnt = ( SHADER_RESOURES_SRV4 - SHADER_RESOURES_SRV0 ) + 1;
	CD3DX12_DESCRIPTOR_RANGE1 ranges[2] = {};
	ranges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCnt, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE );
	ranges[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE );

	CD3DX12_ROOT_PARAMETER1 rootParameters[2] = {};
	rootParameters[0].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL );
	rootParameters[1].InitAsDescriptorTable( 1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL );

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1( _countof( rootParameters ), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT );

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	ThrowIfFailed( D3DX12SerializeVersionedRootSignature( &rootSignatureDesc, featureData.HighestVersion, &signature, &error ) );
	ThrowIfFailed( d3d12device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &rootSignature ) ) );

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ThrowIfFailed( D3DCompileFromFile( GetAssetFullPath( L"crt.hlsl" ).c_str(), nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr ) );
	ThrowIfFailed( D3DCompileFromFile( GetAssetFullPath( L"crt.hlsl" ).c_str(), nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr ) );

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof( inputElementDescs ) };
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE( vertexShader.Get() );
	psoDesc.PS = CD3DX12_SHADER_BYTECODE( pixelShader.Get() );
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
	psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed( d3d12device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pipelineState ) ) );
}


void CreateCommandLists()
{
	for ( uint32_t i = 0; i < FrameCount; ++i )
	{
		ThrowIfFailed( d3d12device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[i].Get(), pipelineState.Get(), IID_PPV_ARGS( &commandList[i] ) ) );
		ThrowIfFailed( d3d12device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_COPY, cpyCommandAllocator[i].Get(), pipelineState.Get(), IID_PPV_ARGS( &cpyCommandList[i] ) ) );
		ThrowIfFailed( d3d12device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, imguiCommandAllocator[i].Get(), pipelineState.Get(), IID_PPV_ARGS( &imguiCommandList[i] ) ) );

		ThrowIfFailed( commandList[i]->Close() );
		ID3D12CommandList* ppCommandLists[] = { commandList[i].Get() };
		d3d12CommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );

		ThrowIfFailed( imguiCommandList[i]->Close() );
		ThrowIfFailed( cpyCommandList[i]->Close() );
	}
}


void CreateVertexBuffers()
{
	const float x0 = -1.0f;
	const float x1 = 2.0f * NormalizeCoordinate( displayScalar * frameResult.frameBuffer.GetWidth(), defaultWidth ) - 1.0f;
	const float y0 = -1.0f;
	const float y1 = 2.0f * NormalizeCoordinate( displayScalar * frameResult.frameBuffer.GetHeight(), defaultHeight ) - 1.0f;

	const XMFLOAT4 tintColor = { 1.0f, 0.0f, 1.0f, 1.0f };

	Vertex triangleVertices[] =
	{
		{ { x0, y0, 0.0f }, tintColor, { 0.0f, 1.0f } },
		{ { x0, y1, 0.0f }, tintColor, { 0.0f, 0.0f } },
		{ { x1, y1, 0.0f }, tintColor, { 1.0f, 0.0f } },

		{ { x0, y0, 0.0f }, tintColor, { 0.0f, 1.0f } },
		{ { x1, y1, 0.0f }, tintColor, { 1.0f, 0.0f } },
		{ { x1, y0, 0.0f }, tintColor, { 1.0f, 1.0f } },
	};

	const UINT vertexBufferSize = sizeof( triangleVertices );

	ThrowIfFailed( d3d12device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer( vertexBufferSize ),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS( &vertexBuffer ) ) );

	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange( 0, 0 );
	ThrowIfFailed( vertexBuffer->Map( 0, &readRange, reinterpret_cast<void**>( &pVertexDataBegin ) ) );
	memcpy( pVertexDataBegin, triangleVertices, sizeof( triangleVertices ) );
	vertexBuffer->Unmap( 0, nullptr );

	vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	vertexBufferView.StrideInBytes = sizeof( Vertex );
	vertexBufferView.SizeInBytes = vertexBufferSize;
}


void CreateConstantBuffers()
{
	const UINT constantBufferDataSize = ( sizeof( DisplayConstantBuffer ) + 255 ) & ~255;

	ThrowIfFailed( d3d12device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer( constantBufferDataSize ),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS( &constantBuffer ) ) );

	NAME_D3D12_OBJECT( constantBuffer );

	constantBufferData.hardPix = -3.0f;
	constantBufferData.hardScan = -8.0f;
	constantBufferData.maskDark = 0.9f;
	constantBufferData.maskLight = 1.1f;
	constantBufferData.warp= { 1.0f / 256.0f, 1.0f / 128.0f };
	constantBufferData.imageDim = { 0.0f, 0.0f, 512.0f, 480.0f };
	constantBufferData.destImageDim = { 0.0f, 0.0f, 512.0f, 480.0f };
	constantBufferData.enable = false;

	cbvSrvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE( cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), SHADER_RESOURES_CBV0, cbvSrvUavDescriptorSize );
	cbvSrvGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE( cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), SHADER_RESOURES_CBV0, cbvSrvUavDescriptorSize );

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = constantBufferDataSize;
	d3d12device->CreateConstantBufferView( &cbvDesc, cbvSrvCpuHandle );

	CD3DX12_RANGE readRange( 0, 0 );
	ThrowIfFailed( constantBuffer->Map( 0, &readRange, reinterpret_cast<void**>( &pCbvDataBegin ) ) );
	memcpy( pCbvDataBegin, &constantBufferData, sizeof( constantBuffer ) );
}


void CreateTextureResources()
{
	const wtRawImageInterface* sourceImages[5];
	sourceImages[0] = &frameResult.frameBuffer;
	sourceImages[1] = &frameResult.nameTableSheet;
	sourceImages[2] = &frameResult.paletteDebug;
	sourceImages[3] = &frameResult.patternTable0;
	sourceImages[4] = &frameResult.patternTable1;

	const uint32_t textureCount = _countof( sourceImages );
	assert( textureCount <= SHADER_RESOURES_COUNT );

	textureResources.clear();
	textureResources.resize( textureCount );

	const UINT cbvSrvUavHeapIncrement = d3d12device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	const CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHeapStart( cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart() );
	const CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHeapStart( cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart() );

	for ( uint32_t i = 0; i < textureCount; ++i )
	{
		textureResources[i].desc.MipLevels = 1;
		textureResources[i].desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureResources[i].desc.Alignment = 0;
		textureResources[i].desc.Width = sourceImages[i]->GetWidth();
		textureResources[i].desc.Height = sourceImages[i]->GetHeight();
		textureResources[i].desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureResources[i].desc.DepthOrArraySize = 1;
		textureResources[i].desc.SampleDesc.Count = 1;
		textureResources[i].desc.SampleDesc.Quality = 0;
		textureResources[i].desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		textureResources[i].allocInfo = d3d12device->GetResourceAllocationInfo( 0, 1, &textureResources[i].desc );
		textureResources[i].srcImage = sourceImages[i];

		ThrowIfFailed( d3d12device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
			D3D12_HEAP_FLAG_NONE,
			&textureResources[i].desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS( &textureResources[i].srv ) ) );

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize( textureResources[i].srv.Get(), 0, 1 );

		ThrowIfFailed( d3d12device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( uploadBufferSize ),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS( &textureResources[i].uploadBuffer ) ) );

		textureResources[i].cpuHandle.InitOffsetted( cpuHeapStart, i + SHADER_RESOURES_SRV0, cbvSrvUavHeapIncrement );
		textureResources[i].gpuHandle.InitOffsetted( gpuHeapStart, i + SHADER_RESOURES_SRV0, cbvSrvUavHeapIncrement );

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureResources[i].desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		d3d12device->CreateShaderResourceView( textureResources[i].srv.Get(), &srvDesc, textureResources[i].cpuHandle );
	}
}


void CreateSyncObjects()
{
	ThrowIfFailed( d3d12device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &fence ) ) );
	fenceValues[currentFrame]++;

	fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if ( fenceEvent == nullptr )
	{
		ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
	}
}


void CreateD3D12Pipeline()
{
	CreatePSO();
	CreateCommandLists();
	CreateVertexBuffers();
	CreateTextureResources();
	CreateConstantBuffers();
	CreateSyncObjects();
	WaitForGpu();
}


void UpdateD3D12()
{
	memcpy( pCbvDataBegin, &constantBufferData, sizeof( constantBufferData ) );
}


void DestroyD3D12()
{
	WaitForGpu();
	CloseHandle( fenceEvent );
}


void IssueTextureCopyCommands()
{
	HANDLE event = CreateEvent( 0, 0, 0, 0 );
	IM_ASSERT( event != NULL );

	ID3D12Fence* cpyFence = NULL;
	ThrowIfFailed( d3d12device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &cpyFence ) ) );

	ThrowIfFailed( cpyCommandAllocator[currentFrame]->Reset() );
	ThrowIfFailed( cpyCommandList[currentFrame]->Reset( cpyCommandAllocator[currentFrame].Get(), pipelineState.Get() ) );
	
	const uint32_t textureCount = textureResources.size();
	std::vector<D3D12_SUBRESOURCE_DATA> textureData( textureCount );

	for ( uint32_t i = 0; i < textureCount; ++i )
	{
		cpyCommandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( textureResources[i].srv.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST ) );
	}

	for ( uint32_t i = 0; i < textureCount; ++i )
	{
		D3D12_SUBRESOURCE_DATA textureData;

		textureData.pData = textureResources[i].srcImage->GetRawBuffer();
		textureData.RowPitch = textureResources[i].srcImage->GetWidth() * static_cast<uint64_t>( texturePixelSize );
		textureData.SlicePitch = textureData.RowPitch * textureResources[i].srcImage->GetHeight();

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT pLayouts = {};
		pLayouts.Offset = 0;
		pLayouts.Footprint.Depth = 1;
		pLayouts.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		pLayouts.Footprint.Width = textureResources[i].srcImage->GetWidth();
		pLayouts.Footprint.Height = textureResources[i].srcImage->GetHeight();
		pLayouts.Footprint.RowPitch = textureData.RowPitch;

		UpdateSubresources( cpyCommandList[currentFrame].Get(), textureResources[i].srv.Get(), textureResources[i].uploadBuffer.Get(), 0, 0, 1, &textureData );
	}

	for ( uint32_t i = 0; i < textureCount; ++i )
	{
		cpyCommandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( textureResources[i].srv.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ) );
	}

	InterlockedExchange( &frameBufferCpyLock, 0 );

	ThrowIfFailed( cpyCommandList[currentFrame]->Close() );

	ID3D12CommandList* ppCommandLists[] = { cpyCommandList[currentFrame].Get() };
	d3d12CopyQueue->ExecuteCommandLists( 1, ppCommandLists );

	d3d12CopyQueue->Signal( cpyFence, 1 );
	fence->SetEventOnCompletion( 1, event );
	WaitForSingleObject( event, INFINITE );

	cpyFence->Release();
	CloseHandle( event );
}


void IssueImguiCommands()
{
#ifdef IMGUI_ENABLE
	ThrowIfFailed( imguiCommandAllocator[currentFrame]->Reset() );
	ThrowIfFailed( imguiCommandList[currentFrame]->Reset( imguiCommandAllocator[currentFrame].Get(), pipelineState.Get() ) );
	
	imguiCommandList[currentFrame]->SetGraphicsRootSignature( rootSignature.Get() );

	ID3D12DescriptorHeap* ppHeaps[] = { cbvSrvUavHeap.Get() };
	imguiCommandList[currentFrame]->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );
	imguiCommandList[currentFrame]->SetGraphicsRootDescriptorTable( 0, cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart() );

	imguiCommandList[currentFrame]->RSSetViewports( 1, &viewport );
	imguiCommandList[currentFrame]->RSSetScissorRects( 1, &scissorRect );

	imguiCommandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( renderTargets[currentFrame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ) );

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvHeap->GetCPUDescriptorHandleForHeapStart(), currentFrame, rtvDescriptorSize );
	imguiCommandList[currentFrame]->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin( "System Info" );
	ImGui::Text( "A: %i", frameResult.state.A );
	ImGui::Text( "X: %i", frameResult.state.X );
	ImGui::Text( "Y: %i", frameResult.state.Y );
	ImGui::Text( "PC: %i", frameResult.state.PC );
	ImGui::Text( "SP: %i", frameResult.state.SP );
	ImGui::Text( "PRG ROM Banks: %i", frameResult.romHeader.prgRomBanks );
	ImGui::Text( "CHR ROM Banks: %i", frameResult.romHeader.chrRomBanks );
	ImGui::Text( "Mirror Mode: %i", frameResult.mirrorMode );
	ImGui::Text( "Mapper ID: %i", frameResult.mapperId );
	ImGui::End();

	ImGui::Begin( "Graphics Debug" );
	if( ImGui::CollapsingHeader( "Frame Buffer", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		const uint32_t imageId = 0;
		const wtRawImageInterface* srcImage = textureResources[imageId].srcImage;
		ImGui::Image( (ImTextureID)textureResources[imageId].gpuHandle.ptr, ImVec2( (float)srcImage->GetWidth(), (float)srcImage->GetHeight() ) );
		ImGui::Text( "%.3f ms/frame (%.1f FPS)", frameResult.dbgInfo.frameTimeUs / 1000.0f, 1000000.0f / frameResult.dbgInfo.frameTimeUs );
		frameTimePlot.Record( 1000000.0f / frameResult.dbgInfo.frameTimeUs );

		ImGui::PlotLines( "", &frameSampleBuffer::GetSample, 
							frameTimePlot.GetSamplesBuffer(), 
							frameTimePlot.GetBufferSize(),
							frameTimePlot.GetBufferStart(),
							NULL,
							FLT_MIN,
							FLT_MAX,
							ImVec2( 0, 0 ) );
	}

	if ( ImGui::CollapsingHeader( "Name Table", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		static float ntScale = 1.0f;
		ImGui::SliderFloat( "Scale", &ntScale, 0.1f, 10.0f );
		const uint32_t imageId = 1;
		const wtRawImageInterface* srcImage = textureResources[imageId].srcImage;
		ImGui::Image( (ImTextureID)textureResources[imageId].gpuHandle.ptr, ImVec2( ntScale * srcImage->GetWidth(), ntScale * srcImage->GetHeight() ) );
		//ImGui::End();
	}

	if ( ImGui::CollapsingHeader( "Pattern Table", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		static float ptScale = 1.0f;
		static int patternTableSelect = 0;
		ImGui::SliderFloat( "Scale", &ptScale, 0.1f, 10.0f );
		if ( ImGui::Button( "Switch Table" ) )
			patternTableSelect ^= 1;
		const uint32_t imageId = ( patternTableSelect == 0 ) ? 3 : 4;
		const wtRawImageInterface* srcImage = textureResources[imageId].srcImage;
		ImGui::Image( (ImTextureID)textureResources[imageId].gpuHandle.ptr, ImVec2( ptScale * srcImage->GetWidth(), ptScale * srcImage->GetHeight() ) );
		ImGui::Text( "Table=%i", patternTableSelect );
	}

	if ( ImGui::CollapsingHeader( "Palette", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		const uint32_t imageId = 2;
		const wtRawImageInterface* srcImage = textureResources[imageId].srcImage;
		ImGui::Image( (ImTextureID)textureResources[imageId].gpuHandle.ptr, ImVec2( 10.0f * srcImage->GetWidth(), 10.0f * srcImage->GetHeight() ) );
	}
	ImGui::End();

	static MemoryEditor cpuMemEdit;
	cpuMemEdit.DrawWindow( "CPU Memory Editor", frameResult.state.cpuMemory, wtState::CpuMemorySize );

	static MemoryEditor ppuMemEdit;
	ppuMemEdit.DrawWindow( "PPU Memory Editor", frameResult.state.ppuMemory, wtState::PpuMemorySize );

	ImGui::Begin( "Shader Parms" );
	ImGui::Checkbox( "Enable", &constantBufferData.enable );
	ImGui::SliderFloat( "Hard Pix", &constantBufferData.hardPix, -4.0f, -2.0f );
	ImGui::SliderFloat( "Hard Scan", &constantBufferData.hardScan, -16.0f, -8.0f );
	ImGui::SliderFloat( "Warp X", &constantBufferData.warp.x, 0.0f, 1.0f / 8.0f );
	ImGui::SliderFloat( "Warp Y", &constantBufferData.warp.y, 0.0f, 1.0f / 8.0f );
	ImGui::SliderFloat( "Mask Light", &constantBufferData.maskLight, 0.0f, 2.0f );
	ImGui::SliderFloat( "Mask Dark", &constantBufferData.maskDark, 0.0f, 2.0f );
	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), imguiCommandList[currentFrame].Get() );

	imguiCommandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( renderTargets[currentFrame].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ) );

	ThrowIfFailed( imguiCommandList[currentFrame]->Close() );
	ID3D12CommandList* ppCommandLists[] = { imguiCommandList[currentFrame].Get() };
	d3d12CommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
#endif
}


void IssueDrawCommands()
{
	ThrowIfFailed( commandAllocator[currentFrame]->Reset() );
	ThrowIfFailed( commandList[currentFrame]->Reset( commandAllocator[currentFrame].Get(), pipelineState.Get() ) );

	commandList[currentFrame]->SetGraphicsRootSignature( rootSignature.Get() );
	
	ID3D12DescriptorHeap* ppHeaps[] = { cbvSrvUavHeap.Get() };
	commandList[currentFrame]->SetDescriptorHeaps( _countof( ppHeaps ), ppHeaps );
	commandList[currentFrame]->SetGraphicsRootDescriptorTable( 0, textureResources[0].gpuHandle );
	commandList[currentFrame]->SetGraphicsRootDescriptorTable( 1, cbvSrvGpuHandle );

	commandList[currentFrame]->RSSetViewports( 1, &viewport );
	commandList[currentFrame]->RSSetScissorRects( 1, &scissorRect );

	commandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( renderTargets[currentFrame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET ) );

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvHeap->GetCPUDescriptorHandleForHeapStart(), currentFrame, rtvDescriptorSize );
	commandList[currentFrame]->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );
	
	const float clearColor[] = { 0.1f, 0.2f, 0.2f, 1.0f };
	commandList[currentFrame]->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
	commandList[currentFrame]->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	commandList[currentFrame]->IASetVertexBuffers( 0, 1, &vertexBufferView );
	commandList[currentFrame]->DrawInstanced( 6, 1, 0, 0 );

	commandList[currentFrame]->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( renderTargets[currentFrame].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT ) );

	ThrowIfFailed( commandList[currentFrame]->Close() );

	ID3D12CommandList* ppCommandLists[] = { commandList[currentFrame].Get() };
	d3d12CommandQueue->ExecuteCommandLists( _countof( ppCommandLists ), ppCommandLists );
}


void InitKeyBindings()
{
	BindKey( 'A', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_LEFT );
	BindKey( 'D', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_RIGHT );
	BindKey( 'W', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_UP );
	BindKey( 'S', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_DOWN );
	BindKey( 'G', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_SELECT );
	BindKey( 'H', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_START );
	BindKey( 'J', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_B );
	BindKey( 'K', ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_A );

	BindKey( '1', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_LEFT );
	BindKey( '2', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_RIGHT );
	BindKey( '3', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_UP );
	BindKey( '4', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_DOWN );
	BindKey( '5', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_SELECT );
	BindKey( '6', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_START );
	BindKey( '7', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_B );
	BindKey( '8', ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_A );
}


BOOL InitInstance( HINSTANCE hInstance, int nCmdShow )
{
	hInst = hInstance;

	InitKeyBindings();

	int dwStyle = ( WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX /*| WS_THICKFRAME*/ );

	RECT wr = { 0, 0, defaultWidth, defaultHeight };
	AdjustWindowRect( &wr, dwStyle, FALSE );

	appDisplay.hWnd = CreateWindowW( szWindowClass, szTitle, dwStyle,
		CW_USEDEFAULT, 0, ( wr.right - wr.left ), ( wr.bottom - wr.top ), nullptr, nullptr, hInstance, nullptr );

	if ( !appDisplay.hWnd )
	{
		return FALSE;
	}

	InitD3D12();
	InitImgui();
	CreateD3D12Pipeline();

	ShowWindow( appDisplay.hWnd, nCmdShow );
	UpdateWindow( appDisplay.hWnd );

	return TRUE;
}


void OpenNesGame()
{
	// https://docs.microsoft.com/en-us/windows/win32/learnwin32/example--the-open-dialog-box
	HRESULT hr = CoInitializeEx( NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE );

	if ( !SUCCEEDED(hr) )
		return;
	
	IFileOpenDialog* pFileOpen;

	// Create the FileOpenDialog object.
	hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen) );

	if ( !SUCCEEDED( hr ) )
		return;

	// Show the Open dialog box.
	hr = pFileOpen->Show(NULL);

	// Get the file name from the dialog box.
	if ( !SUCCEEDED( hr ) )
		return;
	
	IShellItem* pItem;
	hr = pFileOpen->GetResult( &pItem );

	if ( !SUCCEEDED( hr ) )
		return;

	PWSTR filePath = nullptr;
	hr = pItem->GetDisplayName( SIGDN_FILESYSPATH, &filePath );

	// Display the file name to the user.
	if ( !SUCCEEDED( hr ) )
		return;

	if( filePath == nullptr )
		return;
	
	nesFilePath = std::wstring( filePath );
	CoTaskMemFree( filePath );

	reset = true;

	pItem->Release();

	pFileOpen->Release();
	CoUninitialize();
}


LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	if ( ImGui_ImplWin32_WndProcHandler( hWnd, message, wParam, lParam ) )
		return true;

	switch ( message )
	{
	case WM_CREATE:
	{
		
	}
	break;

	case WM_COMMAND:
	{
		int wmId = LOWORD( wParam );
		// Parse the menu selections:
		switch ( wmId )
		{
		case IDM_ABOUT:
			DialogBox( hInst, MAKEINTRESOURCE( IDD_ABOUTBOX ), hWnd, About );
			break;
		case ID_FILE_OPEN:
			OpenNesGame();
			break;
		case IDM_EXIT:
			DestroyWindow( hWnd );
			break;
		default:
			return DefWindowProc( hWnd, message, wParam, lParam );
		}
	}
	break;

	case WM_PAINT:
	{
		UpdateD3D12();

		IssueTextureCopyCommands();
		
		IssueDrawCommands();
		IssueImguiCommands();

		ThrowIfFailed( dxgiSwapChain->Present( 1, 0 ) );
		AdvanceNextFrame();
	}
	break;

	case WM_LBUTTONDOWN:
	{
		POINT p;
		if ( GetCursorPos( &p ) )
		{
			//cursor position now in p.x and p.y
		}
		if ( ScreenToClient( hWnd, &p ) )
		{
			//p.x and p.y are now relative to hwnd's client area
		}

		RECT rc;
		GetClientRect( appDisplay.hWnd, &rc );

		const int32_t clientWidth = ( rc.right - rc.left );
		const int32_t clientHeight = ( rc.bottom - rc.top );
		const int32_t displayAreaX = displayScalar * nesWidth;
		const int32_t displayAreaY = clientHeight; // height minus title bar

		if( ( p.x >= 0 ) && ( p.x < displayAreaX ) && (p.y >= 0 ) && ( p.y < displayAreaY ) )
		{
			p.x /= displayScalar;
			p.y /= displayScalar;
			StoreMouseClick( wtPoint{ p.x, p.y } );
		}
	}
	break;

	case WM_LBUTTONUP:
	{
	//	ClearMouseClick();
	}
	break;

	case WM_SIZE:
	{
		InvalidateRect( hWnd, NULL, FALSE );
	}
	break;

	case WM_KEYDOWN:
	{
		const uint32_t capKey = toupper( (int)wParam );

		if( capKey == 'T' )
		{
			pause = true;
		}

		StoreKey( capKey );
	}
	break;

	case WM_KEYUP:
	{
		const uint32_t capKey = toupper( (int)wParam );

		if ( capKey == 'T' )
		{
			pause = false;
		}

		ReleaseKey( capKey );
	}
	break;

	case WM_DESTROY:
	{
#ifdef IMGUI_ENABLE
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
#endif

		DestroyD3D12();

		PostQuitMessage( 0 );
	}
	break;

	default:
		return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam )
{
    UNREFERENCED_PARAMETER(lParam);
    switch ( message )
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if ( LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL )
        {
            EndDialog( hDlg, LOWORD(wParam) );
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}