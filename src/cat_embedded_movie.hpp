#pragma once
// Offline authored RGBA movies: RGB over a separately encoded grey alpha plane.
// Windows decodes the local resource only. No URL, audio or external codec is used.
// Decode off the UI thread before starting an action; playback never seeks or
// decodes in the timer/render path. Each actor retains its current bounded
// movie; at most one cancelled decode can still be finishing in the worker.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mfidl.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "cat_decode_queue.hpp"

namespace cat::movie {
template<class T> struct Com {
    T* p=nullptr;
    ~Com(){Reset();}
    void Reset(){if(p)p->Release();p=nullptr;}
    T** Put(){Reset();return &p;}
    T* operator->()const{return p;}
    operator bool()const{return p!=nullptr;}
};
struct Frame {std::uint64_t time100ns=0;std::vector<std::uint32_t> pixels;};
struct Movie {
    unsigned width=0,height=0;HRESULT error=E_PENDING;
    std::vector<Frame> frames;
    std::size_t Bytes()const{return std::size_t(width)*height*frames.size()*4;}
};
struct Specification {
    int resource=0;unsigned width=0,height=0,frames=0;
    std::uint64_t durationMs=0;
};
struct Api {
    HMODULE platform=nullptr,reader=nullptr;
    decltype(&MFStartup) startup=nullptr;
    decltype(&MFShutdown) shutdown=nullptr;
    decltype(&MFCreateAttributes) attributes=nullptr;
    decltype(&MFCreateMediaType) mediaType=nullptr;
    using StreamFunction=HRESULT(WINAPI*)(IStream*,IMFByteStream**);
    StreamFunction stream=nullptr;
    decltype(&MFCreateSourceReaderFromByteStream) source=nullptr;
    Api(){
        // Resolve optional Windows media services lazily. Other app behavior
        // still starts on a Windows edition without the media feature pack.
        platform=LoadLibraryExW(L"mfplat.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        reader=LoadLibraryExW(L"mfreadwrite.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(platform){
            startup=reinterpret_cast<decltype(startup)>(GetProcAddress(platform,"MFStartup"));
            shutdown=reinterpret_cast<decltype(shutdown)>(GetProcAddress(platform,"MFShutdown"));
            attributes=reinterpret_cast<decltype(attributes)>(GetProcAddress(platform,"MFCreateAttributes"));
            mediaType=reinterpret_cast<decltype(mediaType)>(GetProcAddress(platform,"MFCreateMediaType"));
            stream=reinterpret_cast<decltype(stream)>(GetProcAddress(platform,"MFCreateMFByteStreamOnStream"));
        }
        if(reader)source=reinterpret_cast<decltype(source)>(GetProcAddress(reader,"MFCreateSourceReaderFromByteStream"));
    }
    bool Valid()const{return startup&&shutdown&&attributes&&mediaType&&stream&&source;}
};
inline Api& Functions(){static Api api;return api;}
inline std::shared_ptr<Movie> Decode(const unsigned char* data,std::size_t size,Specification spec) {
    auto movie=std::make_shared<Movie>();auto& api=Functions();
    if(!api.Valid()||!data||!size||!spec.width||!spec.height||spec.frames>360){movie->error=E_NOTIMPL;return movie;}
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const auto started=api.startup(MF_VERSION,MFSTARTUP_NOSOCKET);
    if(FAILED(started)){movie->error=started;if(SUCCEEDED(com))CoUninitialize();return movie;}
    {
        Com<IStream> input;Com<IMFByteStream> bytes;Com<IMFSourceReader> reader;Com<IMFAttributes> attributes;
        Com<IMFMediaType> requested,current;
        auto fail=[&](HRESULT hr){movie->error=hr;};
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);
        if(!memory)fail(E_OUTOFMEMORY);
        else {
            void* target=GlobalLock(memory);std::memcpy(target,data,size);GlobalUnlock(memory);
            HRESULT hr=CreateStreamOnHGlobal(memory,TRUE,input.Put());if(FAILED(hr))GlobalFree(memory);
            if(SUCCEEDED(hr))hr=api.stream(input.p,bytes.Put());
            if(SUCCEEDED(hr))hr=api.attributes(attributes.Put(),4);
            if(SUCCEEDED(hr))hr=attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
            if(SUCCEEDED(hr))hr=api.source(bytes.p,attributes.p,reader.Put());
            if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
            if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
            if(SUCCEEDED(hr))hr=api.mediaType(requested.Put());
            if(SUCCEEDED(hr))hr=requested->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
            if(SUCCEEDED(hr))hr=requested->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
            if(SUCCEEDED(hr))hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,requested.p);
            if(SUCCEEDED(hr))hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,current.Put());
            UINT64 dimensions=0;if(SUCCEEDED(hr))hr=current->GetUINT64(MF_MT_FRAME_SIZE,&dimensions);
            const unsigned width=unsigned(dimensions>>32),height=unsigned(dimensions&0xffffffff);
            if(SUCCEEDED(hr)&&(width!=spec.width||height!=spec.height*2))hr=MF_E_INVALIDMEDIATYPE;
            UINT32 encodedStride=width*4;if(SUCCEEDED(hr))current->GetUINT32(MF_MT_DEFAULT_STRIDE,&encodedStride);
            const LONG defaultStride=static_cast<LONG>(encodedStride);
            movie->width=spec.width;movie->height=spec.height;movie->frames.reserve(spec.frames);
            while(SUCCEEDED(hr)) {
                Com<IMFSample> sample;DWORD flags=0;LONGLONG time=0;
                hr=reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&time,sample.Put());
                if(FAILED(hr)||(flags&MF_SOURCE_READERF_ENDOFSTREAM))break;
                if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){hr=MF_E_INVALIDMEDIATYPE;break;}
                if(!sample)continue;
                Com<IMFMediaBuffer> buffer;hr=sample->ConvertToContiguousBuffer(buffer.Put());if(FAILED(hr))break;
                Com<IMF2DBuffer> two;BYTE* scan=nullptr;LONG stride=defaultStride;bool locked2d=false,locked=false;
                if(SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer,reinterpret_cast<void**>(two.Put())))) {
                    hr=two->Lock2D(&scan,&stride);locked2d=SUCCEEDED(hr);
                } else {
                    DWORD length=0;hr=buffer->Lock(&scan,nullptr,&length);locked=SUCCEEDED(hr);
                    if(SUCCEEDED(hr)&&length<std::size_t(std::abs(stride))*height)hr=MF_E_BUFFERTOOSMALL;
                    if(SUCCEEDED(hr)&&stride<0)scan+=(height-1)*std::size_t(-stride);
                }
                if(SUCCEEDED(hr)) {
                    Frame frame;frame.time100ns=std::max<LONGLONG>(0,time);frame.pixels.resize(std::size_t(width)*spec.height);
                    for(unsigned y=0;y<spec.height;++y) {
                        const auto* color=scan+std::ptrdiff_t(y)*stride;
                        const auto* alpha=scan+std::ptrdiff_t(y+spec.height)*stride;
                        auto* output=reinterpret_cast<unsigned char*>(frame.pixels.data()+std::size_t(y)*width);
                        for(unsigned x=0;x<width;++x) {
                            unsigned a=alpha[x*4+1];if(a<8)a=0;else if(a>247)a=255;
                            for(unsigned c=0;c<3;++c)output[x*4+c]=static_cast<unsigned char>((color[x*4+c]*a+127)/255);
                            output[x*4+3]=static_cast<unsigned char>(a);
                        }
                    }
                    if(!movie->frames.empty()&&frame.time100ns<=movie->frames.back().time100ns)hr=MF_E_INVALID_TIMESTAMP;
                    else movie->frames.push_back(std::move(frame));
                    if(movie->Bytes()>96ull*1024*1024||movie->frames.size()>spec.frames)hr=E_OUTOFMEMORY;
                }
                if(locked2d)two->Unlock2D();else if(locked)buffer->Unlock();
            }
            if(SUCCEEDED(hr)&&movie->frames.size()!=spec.frames)hr=MF_E_INVALID_FILE_FORMAT;
            fail(hr);
        }
    }
    api.shutdown();if(SUCCEEDED(com))CoUninitialize();
    if(FAILED(movie->error))movie->frames.clear();return movie;
}

// Weak reuse never retains completed scenes. A hidden/paused actor retains its
// own immutable movie until the complete source and recovery have finished.
inline DecodeQueue<Movie>& Decoder(){static DecodeQueue<Movie> queue;return queue;}
class Loader {
public:
    void Reset(){pending_.reset();movie_.reset();resource_=0;}
    ~Loader(){Reset();}
    void Prepare(HINSTANCE instance,Specification spec) {
        if(resource_==spec.resource)return;
        Reset();resource_=spec.resource;
        const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(spec.resource),RT_RCDATA);
        const auto handle=resource?LoadResource(instance,resource):nullptr;
        const auto* data=handle?static_cast<const unsigned char*>(LockResource(handle)):nullptr;
        const auto size=resource?SizeofResource(instance,resource):0;
        pending_=Decoder().Prepare(spec.resource,[data,size,spec]{return Decode(data,size,spec);});
    }
    bool Ready() {
        if(pending_&&pending_->ready.load(std::memory_order_acquire))movie_=pending_->value;
        return movie_&&SUCCEEDED(movie_->error)&&!movie_->frames.empty();
    }
    bool Failed(){Ready();return movie_?FAILED(movie_->error):pending_&&pending_->ready.load(std::memory_order_acquire);}
    HRESULT Error()const{return movie_?movie_->error:pending_&&pending_->ready.load(std::memory_order_acquire)?E_FAIL:E_PENDING;}
    const Movie* Get(){return Ready()?movie_.get():nullptr;}
    const Frame* Sample(std::uint64_t milliseconds,unsigned* index=nullptr) {
        const auto* movie=Get();if(!movie)return nullptr;
        const auto time=milliseconds*10000;
        auto found=std::upper_bound(movie->frames.begin(),movie->frames.end(),time,[](auto t,const auto& frame){return t<frame.time100ns;});
        if(found!=movie->frames.begin())--found;
        if(index)*index=static_cast<unsigned>(found-movie->frames.begin());return &*found;
    }
private:
    int resource_=0;DecodeQueue<Movie>::Request pending_;std::shared_ptr<Movie> movie_;
};
} // namespace cat::movie
