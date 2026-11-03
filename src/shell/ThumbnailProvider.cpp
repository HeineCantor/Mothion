// Windows Explorer thumbnails of Mothion projects (.moth, and the older
// .mothion): a COM thumbnail provider, mothion_thumbnails.dll.
//
// It only shows the preview the project carries (io/ProjectThumbnail.h): it
// finds it in the first bytes of the file, without SQLite, and decodes the
// PNG with WIC. Plain Win32, no Qt, so Explorer loads nothing else.
//
// Registered for the current user, no administrator needed:
//   regsvr32 mothion_thumbnails.dll       (regsvr32 /u to take it away)
// which writes the class under HKCU\Software\Classes\CLSID and hooks it to
// the extensions' thumbnail handler key.

#include "io/ProjectThumbnail.h"

#include <windows.h>

#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// {A3F1C7B2-5E4D-4C8A-9B6E-2D7F1A0C3E59}
constexpr CLSID kClsid = {0xa3f1c7b2, 0x5e4d, 0x4c8a, {0x9b, 0x6e, 0x2d, 0x7f, 0x1a, 0x0c, 0x3e, 0x59}};
constexpr wchar_t kClsidText[] = L"{A3F1C7B2-5E4D-4C8A-9B6E-2D7F1A0C3E59}";
constexpr wchar_t kThumbnailHandler[] = L"{e357fccd-a995-4576-b01f-234630154e96}"; // IThumbnailProvider
constexpr const wchar_t *kExtensions[] = {L".moth", L".mothion"};

HINSTANCE g_module = nullptr;
std::atomic<long> g_objects{0}; // providers alive
std::atomic<long> g_locks{0};   // IClassFactory::LockServer

// The PNG decoded and scaled to fit `cx`, as a top-down 32-bit DIB.
HRESULT decodePng(const unsigned char *data, std::size_t size, UINT cx, HBITMAP *out)
{
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IWICStream> stream;
    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(const_cast<BYTE *>(data), DWORD(size));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(hr))
        hr = frame->GetSize(&width, &height);
    if (FAILED(hr))
        return hr;
    if (width == 0 || height == 0)
        return E_FAIL;

    // Down to the size asked for, never up.
    ComPtr<IWICBitmapSource> source = frame;
    if (cx > 0 && std::max(width, height) > cx) {
        const double k = double(cx) / double(std::max(width, height));
        const UINT w = std::max(1u, UINT(width * k + 0.5)), h = std::max(1u, UINT(height * k + 0.5));
        ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr))
            hr = scaler->Initialize(frame.Get(), w, h, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
            return hr;
        source = scaler;
        width = w;
        height = h;
    }
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return hr;

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(width);
    info.bmiHeader.biHeight = -LONG(height); // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return E_OUTOFMEMORY;
    const UINT stride = width * 4;
    hr = converter->CopyPixels(nullptr, stride, stride * height, static_cast<BYTE *>(bits));
    if (FAILED(hr)) {
        DeleteObject(bitmap);
        return hr;
    }
    *out = bitmap;
    return S_OK;
}

class ThumbnailProvider final : public IInitializeWithStream, public IThumbnailProvider
{
public:
    ThumbnailProvider() { ++g_objects; }
    ~ThumbnailProvider()
    {
        if (m_stream)
            m_stream->Release();
        --g_objects;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        static const QITAB interfaces[] = {
            QITABENT(ThumbnailProvider, IInitializeWithStream),
            QITABENT(ThumbnailProvider, IThumbnailProvider),
            {nullptr, 0},
        };
        return QISearch(this, interfaces, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ULONG(++m_refs); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const long refs = --m_refs;
        if (refs == 0)
            delete this;
        return ULONG(refs);
    }

    IFACEMETHODIMP Initialize(IStream *stream, DWORD) override
    {
        if (!stream)
            return E_INVALIDARG;
        if (m_stream)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        m_stream = stream;
        m_stream->AddRef();
        return S_OK;
    }

    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *bitmap, WTS_ALPHATYPE *alpha) override
    {
        if (!m_stream || !bitmap || !alpha)
            return E_INVALIDARG;
        *bitmap = nullptr;
        // The start of the file: the preview lies in it.
        std::vector<unsigned char> bytes(mothion::io::kThumbnailSearchBytes);
        std::size_t total = 0;
        while (total < bytes.size()) {
            ULONG read = 0;
            const HRESULT hr = m_stream->Read(bytes.data() + total, ULONG(bytes.size() - total), &read);
            if (FAILED(hr))
                return hr;
            total += read;
            if (read == 0 || hr == S_FALSE)
                break;
        }
        const auto span = mothion::io::findThumbnail(bytes.data(), total);
        if (!span)
            return E_FAIL; // an older project, saved before it had one
        const HRESULT hr = decodePng(bytes.data() + span->offset, span->size, cx, bitmap);
        if (SUCCEEDED(hr))
            *alpha = WTSAT_RGB; // the paper is always there
        return hr;
    }

private:
    std::atomic<long> m_refs{1};
    IStream *m_stream = nullptr;
};

class ClassFactory final : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        static const QITAB interfaces[] = {
            QITABENT(ClassFactory, IClassFactory),
            {nullptr, 0},
        };
        return QISearch(this, interfaces, riid, ppv);
    }
    // One static factory for the life of the DLL.
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }

    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto *provider = new (std::nothrow) ThumbnailProvider;
        if (!provider)
            return E_OUTOFMEMORY;
        const HRESULT hr = provider->QueryInterface(riid, ppv);
        provider->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            ++g_locks;
        else
            --g_locks;
        return S_OK;
    }
};

HRESULT setString(const std::wstring &key, const wchar_t *name, const std::wstring &value)
{
    const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
                                           DWORD((value.size() + 1) * sizeof(wchar_t)));
    return HRESULT_FROM_WIN32(status);
}

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, kClsid))
        return CLASS_E_CLASSNOTAVAILABLE;
    static ClassFactory factory;
    return factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring clsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidText;
    HRESULT hr = setString(clsidKey, nullptr, L"Mothion project thumbnails");
    if (SUCCEEDED(hr))
        hr = setString(clsidKey + L"\\InprocServer32", nullptr, path);
    if (SUCCEEDED(hr))
        hr = setString(clsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    for (const wchar_t *extension : kExtensions) {
        const std::wstring extensionKey = std::wstring(L"Software\\Classes\\") + extension;
        if (SUCCEEDED(hr))
            hr = setString(extensionKey + L"\\ShellEx\\" + kThumbnailHandler, nullptr, kClsidText);
        // The preview carries its own icon: none drawn over it, no frame around it.
        if (SUCCEEDED(hr))
            hr = setString(extensionKey, L"TypeOverlay", L"");
        if (SUCCEEDED(hr)) {
            const DWORD none = 0;
            hr = HRESULT_FROM_WIN32(RegSetKeyValueW(HKEY_CURRENT_USER, extensionKey.c_str(), L"Treatment", REG_DWORD,
                                                    &none, sizeof(none)));
        }
    }
    if (SUCCEEDED(hr))
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return hr;
}

STDAPI DllUnregisterServer()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + kClsidText).c_str());
    for (const wchar_t *extension : kExtensions) {
        const std::wstring extensionKey = std::wstring(L"Software\\Classes\\") + extension;
        RegDeleteTreeW(HKEY_CURRENT_USER, (extensionKey + L"\\ShellEx\\" + kThumbnailHandler).c_str());
        RegDeleteKeyValueW(HKEY_CURRENT_USER, extensionKey.c_str(), L"TypeOverlay");
        RegDeleteKeyValueW(HKEY_CURRENT_USER, extensionKey.c_str(), L"Treatment");
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
