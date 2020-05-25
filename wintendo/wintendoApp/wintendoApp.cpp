// wintendoApp.cpp : Defines the entry point for the application.
//


#include <windows.h>
#include <combaseapi.h>
#include <shobjidl.h> 
#include <ole2.h>
#include <ObjBase.h>
#include <map>
#include <comdef.h>
#include "stdafx.h"
#include "wintendoApp.h"
#include "..\wintendoCore\wintendo_api.h"
#include "..\wintendoCore\input.h"


#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

HWND hWnd;

typedef std::pair<ControllerId, ButtonFlags> KeyBinding_t;
std::map<uint32_t, KeyBinding_t> keyMap;
std::map<ButtonFlags, std::string> keyToAscii;

int32_t key = 0;

int32_t displayScalar = 2;
int32_t nesWidth = 256;
int32_t nesHeight = 240;
int32_t debugAreaX = 1024;
int32_t debugAreaY = 0;
int32_t defaultWidth = displayScalar * nesWidth + debugAreaX;
int32_t defaultHeight = displayScalar * nesHeight + debugAreaY;

struct wtAppFrame
{
	float x0;
	float y0;
	float x1;
	float y1;
	uint32_t width;
	uint32_t height;
	const char* name;
	HBITMAP imgHandle;
};


struct wtAppDisplay
{
	static const uint32_t maxDisplayFrames = 12;

	uint32_t displayFramesCount;
	wtAppFrame frames[maxDisplayFrames];
};

wtAppDisplay appDisplay;

unsigned frameSwap = 0;

static bool reset = true;
std::wstring nesFilePath( L"Games/Castlevania.nes" );

static inline const float NormalizeCoordinate( const uint32_t x, const uint32_t length )
{
	return ( x / static_cast<float>( length ) );
}

DWORD WINAPI EmulatorThread( LPVOID lpParameter )
{
	while( true )
	{
		if ( reset )
		{
			InitSystem( nesFilePath );
			reset = false;
		}

		if( !RunFrame() )
			break;

		wtRawImage frameBuffer;
		wtRawImage nameTable;
		wtRawImage paletteDebug;
		wtRawImage patternTable0Debug;
		wtRawImage patternTable1Debug;

		CopyImageBuffer( frameBuffer,			wtImageTag::FRAME_BUFFER );
		CopyImageBuffer( nameTable,				wtImageTag::NAMETABLE );
		CopyImageBuffer( paletteDebug,			wtImageTag::PALETTE );
		CopyImageBuffer( patternTable0Debug,	wtImageTag::PATTERN_TABLE_0 );
		CopyImageBuffer( patternTable1Debug,	wtImageTag::PATTERN_TABLE_1 );

		const float normalizedMarginX = NormalizeCoordinate( 20, defaultWidth );
		const float normalizedMarginY = NormalizeCoordinate( 20, defaultHeight );

		appDisplay.frames[0].name = "Game";
		appDisplay.frames[0].x0 = 0.0f;
		appDisplay.frames[0].y0 = 0.0f;
		appDisplay.frames[0].x1 = NormalizeCoordinate( displayScalar * frameBuffer.GetWidth(), defaultWidth );
		appDisplay.frames[0].y1 = NormalizeCoordinate( displayScalar * frameBuffer.GetHeight(), defaultHeight );
		appDisplay.frames[0].width = frameBuffer.GetWidth();
		appDisplay.frames[0].height = frameBuffer.GetHeight();
		appDisplay.frames[0].imgHandle = CreateBitmap( appDisplay.frames[0].width, appDisplay.frames[0].height, 1, 32, frameBuffer.GetRawBuffer() );

		appDisplay.frames[1].name = "NameTable";
		appDisplay.frames[1].x0 = appDisplay.frames[0].x1 + normalizedMarginX;
		appDisplay.frames[1].y0 = 0.0f;
		appDisplay.frames[1].x1 = NormalizeCoordinate( nameTable.GetWidth(), defaultWidth );
		appDisplay.frames[1].y1 = NormalizeCoordinate( nameTable.GetHeight(), defaultHeight );
		appDisplay.frames[1].width = nameTable.GetWidth();
		appDisplay.frames[1].height = nameTable.GetHeight();
		appDisplay.frames[1].imgHandle = CreateBitmap( appDisplay.frames[1].width, appDisplay.frames[1].height, 1, 32, nameTable.GetRawBuffer() );

		appDisplay.frames[2].name = "Palette";
		appDisplay.frames[2].x0 = ( appDisplay.frames[1].x0 + appDisplay.frames[1].x1 ) + normalizedMarginX;
		appDisplay.frames[2].y0 = 0.0f;
		appDisplay.frames[2].x1 = NormalizeCoordinate( 10 * paletteDebug.GetWidth(), defaultWidth );
		appDisplay.frames[2].y1 = NormalizeCoordinate( 10 * paletteDebug.GetHeight(), defaultHeight );
		appDisplay.frames[2].width = paletteDebug.GetWidth();
		appDisplay.frames[2].height = paletteDebug.GetHeight();
		appDisplay.frames[2].imgHandle = CreateBitmap( appDisplay.frames[2].width, appDisplay.frames[2].height, 1, 32, paletteDebug.GetRawBuffer() );
		
		appDisplay.frames[3].name = "PatternTable0";
		appDisplay.frames[3].x0 = ( appDisplay.frames[1].x0 + appDisplay.frames[1].x1 ) + normalizedMarginX;
		appDisplay.frames[3].y0 = ( appDisplay.frames[2].y0 + appDisplay.frames[2].y1 ) + normalizedMarginY;
		appDisplay.frames[3].x1 = NormalizeCoordinate( patternTable0Debug.GetWidth(), defaultWidth );
		appDisplay.frames[3].y1 = NormalizeCoordinate( patternTable0Debug.GetHeight(), defaultHeight );
		appDisplay.frames[3].width = patternTable0Debug.GetWidth();
		appDisplay.frames[3].height = patternTable0Debug.GetHeight();
		appDisplay.frames[3].imgHandle = CreateBitmap( appDisplay.frames[3].width, appDisplay.frames[3].height, 1, 32, patternTable0Debug.GetRawBuffer() );
		
		appDisplay.frames[4].name = "PatternTable1";
		appDisplay.frames[4].x0 = ( appDisplay.frames[3].x0 + appDisplay.frames[3].x1 ) + normalizedMarginX;
		appDisplay.frames[4].y0 = ( appDisplay.frames[2].y0 + appDisplay.frames[2].y1 ) + normalizedMarginY;
		appDisplay.frames[4].x1 = NormalizeCoordinate( patternTable0Debug.GetWidth(), defaultWidth );
		appDisplay.frames[4].y1 = NormalizeCoordinate( patternTable0Debug.GetHeight(), defaultHeight );
		appDisplay.frames[4].width = patternTable1Debug.GetWidth();
		appDisplay.frames[4].height = patternTable1Debug.GetHeight();
		appDisplay.frames[4].imgHandle = CreateBitmap( appDisplay.frames[4].width, appDisplay.frames[4].height, 1, 32, patternTable1Debug.GetRawBuffer() );

		appDisplay.displayFramesCount = 5;

		RedrawWindow( hWnd, NULL, NULL, RDW_INVALIDATE );
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

	DWORD threadID;
	HANDLE emulatorThreadHandle = CreateThread( 0, 0, EmulatorThread, &sharedData, 0, &threadID );

	if ( emulatorThreadHandle <= 0 )
		return 0;

    // Initialize global strings
    LoadStringW( hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING );
    LoadStringW( hInstance, IDC_WINTENDOAPP, szWindowClass, MAX_LOADSTRING );
    MyRegisterClass( hInstance );

    // Perform application initialization:
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

	CloseHandle( emulatorThreadHandle );

    return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINTENDOAPP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_WINTENDOAPP);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   keyMap['A'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_LEFT );
   keyMap['D'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_RIGHT );
   keyMap['W'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_UP );
   keyMap['S'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_DOWN );
   keyMap['G'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_SELECT );
   keyMap['H'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_START );
   keyMap['J'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_B );
   keyMap['K'] = KeyBinding_t( ControllerId::CONTROLLER_0, ButtonFlags::BUTTON_A );

   keyMap['1'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_LEFT );
   keyMap['2'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_RIGHT );
   keyMap['3'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_UP );
   keyMap['4'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_DOWN );
   keyMap['5'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_SELECT );
   keyMap['6'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_START );
   keyMap['7'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_B );
   keyMap['8'] = KeyBinding_t( ControllerId::CONTROLLER_1, ButtonFlags::BUTTON_A );

   keyToAscii[ButtonFlags::BUTTON_A]		= "A";
   keyToAscii[ButtonFlags::BUTTON_B]		= "B";
   keyToAscii[ButtonFlags::BUTTON_START]	= "STR";
   keyToAscii[ButtonFlags::BUTTON_SELECT]	= "SEL";
   keyToAscii[ButtonFlags::BUTTON_LEFT]		= "L";
   keyToAscii[ButtonFlags::BUTTON_RIGHT]	= "R";
   keyToAscii[ButtonFlags::BUTTON_UP]		= "U";
   keyToAscii[ButtonFlags::BUTTON_DOWN]		= "D";

   int dwStyle = ( WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME );

   RECT wr = { 0, 0, defaultWidth, defaultHeight };
   AdjustWindowRect( &wr, dwStyle, FALSE );

   hWnd = CreateWindowW( szWindowClass, szTitle, dwStyle,
      CW_USEDEFAULT, 0, ( wr.right - wr.left ), ( wr.bottom - wr.top ), nullptr, nullptr, hInstance, nullptr);

   if ( !hWnd )
   {
      return FALSE;
   }

   ShowWindow( hWnd, nCmdShow );
   UpdateWindow( hWnd );

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


static void PaintWindow()
{
	PAINTSTRUCT     ps;
	HDC             hdc;
	HDC             hdcMem;

	hdc = ::BeginPaint( hWnd, &ps );

	hdcMem = CreateCompatibleDC( hdc );

	RECT rc;
	GetClientRect( hWnd, &rc );

	const int32_t windowWidth = ( rc.right - rc.left );
	const int32_t windowHeight = ( rc.bottom - rc.top );

	for ( uint32_t i = 0; i < appDisplay.displayFramesCount; ++i )
	{
		BITMAP imgBitmap;

		::SelectObject( hdcMem, appDisplay.frames[i].imgHandle );

		const wtAppFrame& viewFrame = appDisplay.frames[i];

		GetObject( viewFrame.imgHandle, sizeof( imgBitmap ), &imgBitmap );
		StretchBlt( hdc, static_cast<int>( windowWidth * viewFrame.x0 ), static_cast<int>( windowHeight * viewFrame.y0 ),
			static_cast<int>( windowWidth * viewFrame.x1 ), static_cast<int>( windowHeight * viewFrame.y1 ),
			hdcMem, 0, 0, viewFrame.width, viewFrame.height, SRCCOPY );
	}

	DeleteDC( hdcMem );
	EndPaint( hWnd, &ps );

	for ( uint32_t i = 0; i < appDisplay.displayFramesCount; ++i )
	{
		DeleteObject( appDisplay.frames[i].imgHandle );
	}

	::ReleaseDC( NULL, hdc );
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
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
		PaintWindow();
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
		const KeyBinding_t& key = keyMap[capKey];

		StoreKey( key.first, key.second );
	}
	break;

	case WM_KEYUP:
	{
		const uint32_t capKey = toupper( (int)wParam );
		const KeyBinding_t& key = keyMap[capKey];

		ReleaseKey( key.first, key.second );
	}
	break;

	case WM_DESTROY:
	{
		for( uint32_t i = 0; i < appDisplay.displayFramesCount; ++i )
		{
			DeleteObject( appDisplay.frames[i].imgHandle );
		}
		PostQuitMessage( 0 );
	}
	break;

	default:
		return DefWindowProc( hWnd, message, wParam, lParam );
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
