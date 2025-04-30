// Make sure to include these at the top if you prefer not to define project-wide:
// #ifndef UNICODE
// #define UNICODE
// #endif
// #ifndef _UNICODE
// #define _UNICODE
// #endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <windows.h>
#include <gdiplus.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <ole2.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "ole32.lib") // Required for CreateStreamOnHGlobal()

//#define IDR_GIF1 101

using namespace Gdiplus;

HINSTANCE hInstance;
HWND hwnd;
HDC hdc;
GLuint textureID;
Image* gifImage;
int WIDTH = 207, HEIGHT = 243;
UINT frameCount = 1, currentFrame = 0;
bool useGDI = false;

/*void ShowGLVersion() {
    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version) {
        MessageBoxW(NULL, L"Failed to get GL version!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Convert char* (ASCII) to wchar_t* (Unicode)
    int len = MultiByteToWideChar(CP_ACP, 0, version, -1, NULL, 0);
    std::wstring wVersion(len, 0);
    MultiByteToWideChar(CP_ACP, 0, version, -1, &wVersion[0], len);

    MessageBoxW(NULL, wVersion.c_str(), L"ver", MB_OK);
}*/
void LoadGIFFromResource() {
    // Use wide-character version of FindResource and MAKEINTRESOURCEW.
        //hInstance = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(101), L"GIF");
    //HRSRC hRes = FindResource(hInstance, "mymy.gif", "GIF"); // If using a string ID

    if (!hRes) {
        MessageBoxW(NULL, L"Resource not found!", L"Error", MB_OK);
		wchar_t buf[256];
    	swprintf(buf, 256, L"FindResourceW failed! Error code: %d", GetLastError());
    	MessageBoxW(NULL, buf, L"Error", MB_OK);
        return;
    }

    DWORD dwSize = SizeofResource(hInstance, hRes);
    HGLOBAL hGlob = LoadResource(hInstance, hRes);
    LPVOID pData = LockResource(hGlob);

    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == S_OK) {
        // Write the resource data to the stream.
        pStream->Write(pData, dwSize, NULL);
        LARGE_INTEGER li = { 0 };
        pStream->Seek(li, STREAM_SEEK_SET, NULL);
        gifImage = new Image(pStream);
        pStream->Release();
    }
}

void InitOpenGL() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TransparentOverlayClass";
    RegisterClassW(&wc);
int screenWidth = GetSystemMetrics(SM_CXSCREEN);
int screenHeight = GetSystemMetrics(SM_CYSCREEN);
int xPos = screenWidth - WIDTH;                          // Keep it at the r edge
int yPos = screenHeight - HEIGHT-32;      // Move it to the bottom

    hwnd = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
    wc.lpszClassName, 
    L"Transparent Overlay", 
    WS_POPUP, 
    xPos, yPos, WIDTH, HEIGHT, 
    NULL, NULL, hInstance, NULL);


    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pixelFormat, &pfd);

    HGLRC hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);

// Check if OpenGL is actually working
    const char* version = (const char*)glGetString(GL_VERSION);
    //ShowGLVersion();
    if (!version || version[0] == '\0') {
        MessageBoxW(NULL, L"OpenGL failed! Falling back to GDI rendering.", L"Warning", MB_OK);
        useGDI = true; // Set flag to use GDI instead
        return;
    }else if (version && atof(version) < 1.2) {  // If OpenGL is too old, fallback
        useGDI = true;
        MessageBoxW(NULL, L"OpenGL is too old! Using GDI+ instead.", L"Warning", MB_OK);
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glGenTextures(1, &textureID);
}


void LoadFrameToTexture() {
    if (!gifImage) return;

    gifImage->SelectActiveFrame(&FrameDimensionTime, currentFrame);  // Ensure correct frame

    Bitmap* frame = static_cast<Bitmap*>(gifImage);
    BitmapData bmpData;
    Rect rect(0, 0, WIDTH, HEIGHT);
    frame->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bmpData);

    BYTE* pixels = new BYTE[WIDTH * HEIGHT * 4];
    UINT* src = (UINT*)bmpData.Scan0;

    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        pixels[i * 4 + 0] = (src[i] >> 16) & 0xFF; // R
        pixels[i * 4 + 1] = (src[i] >> 8)  & 0xFF; // G
        pixels[i * 4 + 2] = (src[i] >> 0)  & 0xFF; // B
        pixels[i * 4 + 3] = (src[i] >> 24) & 0xFF; // A
    }
BYTE* flippedPixels = new BYTE[WIDTH * HEIGHT * 4];  

for (int y = 0; y < HEIGHT; y++) {
    int srcIndex = y * WIDTH * 4;
    int destIndex = (HEIGHT - 1 - y) * WIDTH * 4;  
    memcpy(&flippedPixels[destIndex], &pixels[srcIndex], WIDTH * 4);
}

glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, flippedPixels);

delete[] flippedPixels;

    frame->UnlockBits(&bmpData);

/*int centerIdx = (WIDTH * (HEIGHT / 2) + (WIDTH / 2)) * 4;
wchar_t buf[256];
swprintf(buf, 256, L"Center pixel: R=%d, G=%d, B=%d, A=%d", 
         pixels[centerIdx], pixels[centerIdx+1], pixels[centerIdx+2], pixels[centerIdx+3]);
MessageBoxW(NULL, buf, L"Pixel Info", MB_OK);*/


glBindTexture(GL_TEXTURE_2D, textureID);
//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


    delete[] pixels;
}


void RenderGifOverlay() {
	if (useGDI) {
        HDC hdc = GetDC(hwnd);

        // Create a compatible DC and bitmap
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, WIDTH, HEIGHT);
        SelectObject(hdcMem, hbmMem);

        // Fill with full transparency (black with 0 alpha)
        Gdiplus::Graphics graphics(hdcMem);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Draw the GIF onto the memory DC
        graphics.DrawImage(gifImage, 0, 0, WIDTH, HEIGHT);

        // Copy the transparent buffer to the window
        BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcMem, 0, 0, SRCCOPY);

        // Cleanup
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdc);

        return;
    }
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, textureID);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();

    SwapBuffers(hdc);
}


int main() {
    hInstance = GetModuleHandleW(NULL);

    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LoadGIFFromResource();
    WIDTH = gifImage->GetWidth();
    HEIGHT = gifImage->GetHeight();
    frameCount = gifImage->GetFrameCount(&FrameDimensionTime);

    InitOpenGL();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (true) {

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        currentFrame = (currentFrame + 1) % frameCount;
        LoadFrameToTexture();
        RenderGifOverlay();
        Sleep(100);
    }

    return 0;
}

