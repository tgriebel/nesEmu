// wintendoApp.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "wintendoApp.h"
#include "..\wintendoCore\wintendo_api.h"

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

HBITMAP hBitmap;

DWORD WINAPI EmulatorThread( LPVOID lpParameter )
{
	InitSystem();

	while( true )
	{
		unsigned frameBuffer[0xFFFF];
		RunFrame( frameBuffer );

		hBitmap = CreateBitmap( 256, 240, 1, 32, frameBuffer );

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

    // TODO: Place code here.
	using namespace std;

	unsigned int sharedData = 0;

	DWORD threadID;
	HANDLE emulatorThreadHandle = CreateThread( 0, 0, EmulatorThread, &sharedData, 0, &threadID );

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WINTENDOAPP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINTENDOAPP));

    MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	CloseHandle( emulatorThreadHandle );

    return (int) msg.wParam;
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

   int dwStyle = ( WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX );

   RECT wr = { 0, 0, 256, 260 };
   AdjustWindowRect( &wr, dwStyle, FALSE );

   hWnd = CreateWindowW( szWindowClass, szTitle, dwStyle,
      CW_USEDEFAULT, 0, ( wr.right - wr.left ), ( wr.bottom - wr.top ), nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
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
	wchar_t msg[32];

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
		PAINTSTRUCT     ps;
		HDC             hdc;
		BITMAP          bitmap;
		HDC             hdcMem;
		HGDIOBJ         oldBitmap;

		hdc = BeginPaint( hWnd, &ps );
		
		// hBitmap = (HBITMAP)LoadImage( hInst, L"C:\\Users\\Thomas\\source\\repos\\NES Emulator\\wintendo\\wintendoApp\\Render Dumps\\nt_.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE );

		hdcMem = CreateCompatibleDC( hdc );
		oldBitmap = SelectObject( hdcMem, hBitmap );

		GetObject( hBitmap, sizeof( bitmap ), &bitmap );
		BitBlt( hdc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, hdcMem, 0, 0, SRCCOPY );

		SelectObject( hdcMem, oldBitmap );
		DeleteDC( hdcMem );

		EndPaint( hWnd, &ps );

		::ReleaseDC( NULL, hdc );
	}
	break;

	case WM_KEYDOWN:
	{
		swprintf_s( msg, L"WM_SYSKEYDOWN: 0x%x\n", wParam );
		OutputDebugString( msg );
	}
	break;

	case WM_DESTROY:
	{
		DeleteObject( hBitmap );
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
