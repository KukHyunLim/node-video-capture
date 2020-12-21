// 
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
// 
// MIT license 
// 
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include "PipelineDefines.h"
#include "TranscodePipeline.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGVideoDecoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/FFMPEGFileMuxer.h"

#pragma warning(disable:4355)



const wchar_t* TranscodePipeline::PARAM_NAME_SCALE_WIDTH  = L"WIDTH";
const wchar_t* TranscodePipeline::PARAM_NAME_SCALE_HEIGHT = L"HEIGHT";
const wchar_t* TranscodePipeline::PARAM_NAME_FRAMES       = L"FRAMES";


// NOTE: AAC codec ID for ffmpeg 4.1.3 - id can change with different ffmpeg versions
#pragma message("NOTE: AAC codec ID for ffmpeg 4.1.3 - id might change on other versions of ffmpeg")
#define CODEC_ID_AAC  0x15002



class TranscodePipeline::PipelineElementEncoder : public AMFComponentElement
{
public:
    PipelineElementEncoder(amf::AMFComponentPtr pComponent, ParametersStorage* pParams, amf_int64 frameParameterFreq, amf_int64 dynamicParameterFreq)
        :AMFComponentElement(pComponent),
        m_pParams(pParams),
        m_framesSubmitted(0),
        m_frameParameterFreq(frameParameterFreq),
        m_dynamicParameterFreq(dynamicParameterFreq)
    {

    }

    virtual ~PipelineElementEncoder(){}

    AMF_RESULT SubmitInput(amf::AMFData* pData) 
    {
        AMF_RESULT res = AMF_OK;
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            if(m_frameParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_frameParameterFreq) == 0)
            { // apply frame-specific properties to the current frame
                PushParamsToPropertyStorage(m_pParams, ParamEncoderFrame, pData);
            }
            if(m_dynamicParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_dynamicParameterFreq) == 0)
            { // apply dynamic properties to the encoder
                PushParamsToPropertyStorage(m_pParams, ParamEncoderDynamic, m_pComponent);
            }


            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }
            m_framesSubmitted++;
        }
        return res; 
    }

protected:
    ParametersStorage*      m_pParams;
    amf_int                 m_framesSubmitted;
    amf_int64               m_frameParameterFreq;
    amf_int64               m_dynamicParameterFreq;
};


TranscodePipeline::TranscodePipeline()
    :m_pContext(),
    m_eDecoderFormat(amf::AMF_SURFACE_NV12)
{
}

TranscodePipeline::~TranscodePipeline()
{
    Terminate();
}

void TranscodePipeline::Terminate()
{
    Pipeline::Stop();

    m_pStreamIn = NULL;
    m_pStreamOut = NULL;

    if(m_pAudioDecoder != NULL)
    {
        m_pAudioDecoder->Terminate();
        m_pAudioDecoder = NULL;
    }
    if(m_pAudioConverter != NULL)
    {
        m_pAudioConverter->Terminate();
        m_pAudioConverter= NULL;
    }
    if(m_pAudioEncoder != NULL)
    {
        m_pAudioEncoder->Terminate();
        m_pAudioEncoder = NULL;
    }
    if(m_pDecoder != NULL)
    {
        m_pDecoder->Terminate();
        m_pDecoder = NULL;
    }
    if(m_pConverter != NULL)
    {
        m_pConverter->Terminate();
        m_pConverter = NULL;
    }
    if(m_pPreProcFilter != NULL)
    {
        m_pPreProcFilter->Terminate();
        m_pPreProcFilter = NULL;
    }
    if(m_pEncoder != NULL)
    {
        m_pEncoder->Terminate();
        m_pEncoder = NULL;
    }
    m_pStreamWriter = NULL;

    m_pSplitter = NULL;
    m_pPresenter = NULL;
    if(m_pConverter2)
    {
        m_pConverter2->Terminate();
        m_pConverter2 = NULL;
    }

    if(m_pMuxer != NULL)
    {
        m_pMuxer->Terminate();
        m_pMuxer = NULL;
    }
    if(m_pDemuxer != NULL)
    {
        m_pDemuxer->Terminate();
        m_pDemuxer = NULL;
    }
    if(m_pRawStreamReader != NULL)
    {
//      m_pRawStreamReader->Terminate();
        m_pRawStreamReader = NULL;
    }
    if(m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }
#if defined(_WIN32)
#if !defined(METRO_APP)
    m_deviceDX9.Terminate();
#endif//#if !defined(METRO_APP)
    m_deviceDX11.Terminate();
#endif    
    m_deviceVulkan.Terminate();

}

double TranscodePipeline::GetProgressSize()
{
    amf_int64 size = 100;
    if(m_pStreamIn)
    {
        m_pStreamIn->GetSize(&size);
    }
    return (double)size;
}

double TranscodePipeline::GetProgressPosition()
{
    amf_int64 pos = 0;
    if(m_pStreamIn)
    {
        m_pStreamIn->GetPosition(&pos);
    }
    return (double)pos;}

#if !defined(METRO_APP)
AMF_RESULT TranscodePipeline::Init(ParametersStorage* pParams, amf_handle previewTarget, amf_handle display, int threadID)
#else
AMF_RESULT TranscodePipeline::Init(const wchar_t* path, IRandomAccessStream^ inputStream, IRandomAccessStream^ outputStream, 
                                   ISwapChainBackgroundPanelNative* previewTarget, AMFSize swapChainPanelSize, ParametersStorage* pParams)
#endif
{
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Read Options
    amf_uint32 adapterID = 0;
    pParams->GetParam(PARAM_NAME_ADAPTERID, adapterID);


    std::wstring inputPath = L"";
#if !defined(METRO_APP)
    res = pParams->GetParamWString(PARAM_NAME_INPUT, inputPath);
    CHECK_AMF_ERROR_RETURN(res, L"Input Path");
    std::wstring outputPath = L"";
    res= pParams->GetParamWString(PARAM_NAME_OUTPUT, outputPath);
    CHECK_AMF_ERROR_RETURN(res, L"Output Path");
#if defined(_WIN32)    
    std::wstring engineStr = L"DX9";
#else
    std::wstring engineStr = L"VULKAN";
#endif    
    pParams->GetParamWString(PARAM_NAME_ENGINE, engineStr);
    engineStr = toUpper(engineStr);
#else
    std::wstring engineStr = L"DX11";
    inputPath = path;
#endif//#if !defined(METRO_APP)


    amf::AMF_MEMORY_TYPE engineMemoryType = amf::AMF_MEMORY_UNKNOWN;
    if(engineStr == L"DX9")
    {
        engineMemoryType = amf::AMF_MEMORY_DX9;
    }
    else if(engineStr == L"DX11")
    {
        engineMemoryType = amf::AMF_MEMORY_DX11;
    }
    else if(engineStr == L"VULKAN")
    {
        engineMemoryType = amf::AMF_MEMORY_VULKAN;
    }
	else if (engineStr == L"DX12")
	{
		engineMemoryType = amf::AMF_MEMORY_DX12;
	}
    else
    {
        CHECK_RETURN(false, AMF_INVALID_ARG, L"Wrong parameter " << engineStr);
    }
   

    // decode options to be played with
    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = AMF_VIDEO_DECODER_MODE_COMPLIANT; //amf:::AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;
    bool                    decodeAsAnnexBStream = false; // switches between Annex B and AVCC types of decode input.

    // frequency of dynamic changes in the encoder parameters
    amf_int frameParameterFreq = 0; 
    amf_int dynamicParameterFreq = 0;
    pParams->GetParam(SETFRAMEPARAMFREQ_PARAM_NAME, frameParameterFreq);
    pParams->GetParam(SETDYNAMICPARAMFREQ_PARAM_NAME, dynamicParameterFreq);

    amf_int64 frames = 0;
    pParams->GetParam(PARAM_NAME_FRAMES, frames);
    

    //---------------------------------------------------------------------------------------------
    // Init context and devices

    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);

    switch(engineMemoryType)
    {
#if defined(_WIN32)
#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX9:

        res = m_deviceDX9.Init(true, adapterID, false, 1, 1);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");

        res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
        break;
#endif//#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX11:
        res = m_deviceDX11.Init(adapterID);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX11.Init() failed");

        res = m_pContext->InitDX11(m_deviceDX11.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX11() failed");
        break;
	case amf::AMF_MEMORY_DX12:
		res = amf::AMFContext2Ptr(m_pContext)->InitDX12(NULL);
		CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX12() failed");
		break;
#endif        
    case amf::AMF_MEMORY_VULKAN:
//        res = m_deviceVulkan.Init(adapterID, m_pContext);
        res = amf::AMFContext1Ptr(m_pContext)->InitVulkan(NULL);
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitVulkan() failed");
        break;
    }


    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser or demuxer
    BitStreamType inStreamType = GetStreamType(inputPath.c_str());
    BitStreamType outStreamType = GetStreamType(outputPath.c_str());

    amf_int32 iVideoStreamIndex = -1;
    amf_int32 iAudioStreamIndex = -1;

    amf_int32 outVideoStreamIndex = -1;
    amf_int32 outAudioStreamIndex = -1;

    BitStreamParserPtr      pParser;
    amf::AMFOutputPtr       pAudioOutput;
    amf::AMFOutputPtr       pVideoOutput;
    if( inStreamType != BitStreamUnknown)
    { 
#if !defined(METRO_APP)
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStreamIn);
#else
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStreamIn);
#endif
        CHECK_RETURN(m_pStreamIn != NULL, AMF_FILE_NOT_OPEN, "Open File");

        pParser = BitStreamParser::Create(m_pStreamIn, inStreamType, m_pContext);
        CHECK_RETURN(pParser != NULL, AMF_FILE_NOT_OPEN, "BitStreamParser::Create");

        iVideoStreamIndex = 0;

        if(frames != 0)
        {
            pParser->SetMaxFramesNumber((amf_size)frames);
        }
    }
    else
    {
        amf_int32               width  = 0;
        amf_int32               height = 0;
        amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_UNKNOWN;
        RawStreamReader::ParseRawFileFormat(inputPath, width, height, format);
        if ((format != amf::AMF_SURFACE_UNKNOWN) && (width > 0) && (height > 0))
        {
            m_pRawStreamReader = RawStreamReaderPtr(new RawStreamReader());

            // make a copy of the properties and then update width/height
            amf::AMFVariantStruct  origWidth;
            amf::AMFVariantStruct  origHeight;
            pParams->GetParam(L"WIDTH", &origWidth);
            pParams->GetParam(L"HEIGHT", &origHeight);
            pParams->SetParam(L"WIDTH", width);
            pParams->SetParam(L"HEIGHT", height);

            // initialize raw stream reader with the correct 
            // width/height obtained from the string name 
            res = m_pRawStreamReader->Init(pParams, m_pContext);
            CHECK_AMF_ERROR_RETURN(res, L"m_pRawStreamReader->Init() failed");

            pParams->SetParam(L"WIDTH", origWidth);
            pParams->SetParam(L"HEIGHT", origHeight);

            iVideoStreamIndex = 0;
        }
        else
        {
            amf::AMFComponentPtr  pDemuxer;
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_DEMUXER, &pDemuxer);
            CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
            m_pDemuxer = amf::AMFComponentExPtr(pDemuxer);

            m_pDemuxer->SetProperty(FFMPEG_DEMUXER_PATH, inputPath.c_str());
            res = m_pDemuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->Init() failed");

            if(frames != 0)
            {
                m_pDemuxer->SetProperty(FFMPEG_DEMUXER_FRAME_COUNT, frames);
            }

            amf_int32 outputs = m_pDemuxer->GetOutputCount();
            for(amf_int32 output = 0; output < outputs; output++)
            {
                amf::AMFOutputPtr pOutput;
                res = m_pDemuxer->GetOutput(output, &pOutput);
                CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->GetOutput() failed");

                amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
                pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


                if(iVideoStreamIndex < 0 && eStreamType == AMF_STREAM_VIDEO)
                {
                    iVideoStreamIndex = output;
                    pVideoOutput = pOutput;
                }

                if(iAudioStreamIndex < 0 && eStreamType == AMF_STREAM_AUDIO)
                {
                    iAudioStreamIndex = output;
                    pAudioOutput = pOutput;
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // init streams
    if(iVideoStreamIndex >= 0)
    {
        InitVideo(pParser, m_pRawStreamReader, pVideoOutput, engineMemoryType,  previewTarget, display, pParams);
    }
    if(iAudioStreamIndex >= 0 && outStreamType == BitStreamUnknown) // do not process audio if we write elmentary stream
    {
        InitAudio(pAudioOutput);
    }

    //---------------------------------------------------------------------------------------------
    // Init Stream Writer or Muxer

#if !defined(METRO_APP)
    if(threadID != -1)
    {
        std::wstring::size_type pos_dot = outputPath.rfind(L'.');
        if(pos_dot == std::wstring::npos)
        {
            LOG_ERROR(L"Bad file name (no extension): " << outputPath);
            return AMF_FAIL;
        }
        std::wstringstream prntstream;
        prntstream << threadID;
        outputPath = outputPath.substr(0, pos_dot) + L"_" + prntstream.str() + outputPath.substr(pos_dot);
    }

    if( outStreamType != BitStreamUnknown)
    { 
        amf::AMFDataStream::OpenDataStream(outputPath.c_str(), amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &m_pStreamOut);
        CHECK_RETURN(m_pStreamOut != NULL, AMF_FILE_NOT_OPEN, "Open File");
#else
        m_pStreamOut = AMFDataStream::Create(outputStream);
        CHECK_RETURN(m_pStreamOut != NULL, AMF_FILE_NOT_OPEN, "Open File");
#endif//#if !defined(METRO_APP)
        m_pStreamWriter = StreamWriterPtr(new StreamWriter(m_pStreamOut));
    }
    else
    {
        amf::AMFComponentPtr  pMuxer;
        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_MUXER, &pMuxer);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
        m_pMuxer = amf::AMFComponentExPtr(pMuxer);

        m_pMuxer->SetProperty(FFMPEG_MUXER_PATH, outputPath.c_str());
        if(iVideoStreamIndex >= 0)
        {
            m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_VIDEO, true);

        }
        if(iAudioStreamIndex >= 0)
        {
            m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_AUDIO, true);
        }

        amf_int32 inputs = m_pMuxer->GetInputCount();
        for(amf_int32 input = 0; input < inputs; input++)
        {
            amf::AMFInputPtr pInput;
            res = m_pMuxer->GetInput(input, &pInput);
            CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->GetInput() failed");

            amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
            pInput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


            if(eStreamType == AMF_STREAM_VIDEO)
            {
                outVideoStreamIndex = input;

                pInput->SetProperty(AMF_STREAM_ENABLED, true);
                amf_int32 bitrate = 0;
                if(m_EncoderID == AMFVideoEncoderVCE_AVC || m_EncoderID == AMFVideoEncoderVCE_SVC)
                { 
                    pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H264_AVC); // default
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &bitrate);
                    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                    amf::AMFInterfacePtr pExtraData;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
                    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                    AMFSize frameSize;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &frameSize);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                    AMFRate frameRate;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &frameRate);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
                }
                else
                {
                    pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H265_HEVC);
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &bitrate);
                    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                    amf::AMFInterfacePtr pExtraData;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &pExtraData);
                    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                    AMFSize frameSize;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, &frameSize);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                    AMFRate frameRate;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &frameRate);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
                }
            }
            else if(eStreamType == AMF_STREAM_AUDIO)
            {
                outAudioStreamIndex = input;
                pInput->SetProperty(AMF_STREAM_ENABLED, true);


                amf_int64 codecID = 0;
                amf_int64 streamBitRate = 0;
                amf_int64 streamSampleRate = 0;
                amf_int64 streamChannels = 0;
                amf_int64 streamFormat = 0;
                amf_int64 streamLayout = 0;
                amf_int64 streamBlockAlign = 0;
                amf_int64 streamFrameSize = 0;

                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID); // currently the same codec as input
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, &streamChannels);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, &streamFrameSize);


                amf::AMFInterfacePtr pExtraData;
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, &pExtraData);
                pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                pInput->SetProperty(AMF_STREAM_CODEC_ID, codecID);
                pInput->SetProperty(AMF_STREAM_BIT_RATE, streamBitRate);
                pInput->SetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, streamSampleRate);
                pInput->SetProperty(AMF_STREAM_AUDIO_CHANNELS, streamChannels);
                pInput->SetProperty(AMF_STREAM_AUDIO_FORMAT, streamFormat);
                pInput->SetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, streamLayout);
                pInput->SetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, streamBlockAlign);
                pInput->SetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, streamFrameSize);
            }
        }
        res = m_pMuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->Init() failed");

    }
    //---------------------------------------------------------------------------------------------
    // Connect pipeline
    if(previewTarget != NULL)
    {
        m_pSplitter = SplitterPtr(new Splitter(true, 2));
    }

    PipelineElementPtr pPipelineElementDemuxer;
    PipelineElementPtr pPipelineElementEncoder;

    if(pParser != NULL)
    { 
        pPipelineElementDemuxer = pParser;
    }
    else if (m_pRawStreamReader != NULL)
    {
        pPipelineElementDemuxer = PipelineElementPtr(m_pRawStreamReader);
    }
    else
    {
        pPipelineElementDemuxer = PipelineElementPtr(new AMFComponentExElement(m_pDemuxer));
    }
    Connect(pPipelineElementDemuxer, 10);

    // video
    if(iVideoStreamIndex >= 0)
    {
        SetStatSlot( pPipelineElementDemuxer, iVideoStreamIndex);
        if (m_pRawStreamReader == NULL)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pDecoder)), 0, pPipelineElementDemuxer, iVideoStreamIndex, 4, CT_Direct);
        }
        if(m_pSplitter != 0)
        {
            Connect(m_pSplitter, 4, CT_Direct);
        }
        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4, CT_Direct);
        if (m_pPreProcFilter != NULL)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pPreProcFilter)), 4, CT_Direct);
        }

        pPipelineElementEncoder = PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, pParams, frameParameterFreq, dynamicParameterFreq));
        Connect(pPipelineElementEncoder, 10, CT_Direct);
    }
    // 
    if(m_pStreamWriter != NULL)
    {
        Connect(m_pStreamWriter, 5, CT_ThreadQueue);
    }
    else
    {
        PipelineElementPtr pPipelineElementMuxer = PipelineElementPtr(new AMFComponentExElement(m_pMuxer));
        Connect(pPipelineElementMuxer, outVideoStreamIndex, pPipelineElementEncoder, 0, 10, CT_ThreadQueue);
        SetStatSlot( pPipelineElementMuxer, 0);

        // audio
        if(iAudioStreamIndex >= 0)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioDecoder)), 0, pPipelineElementDemuxer, iAudioStreamIndex, 4, CT_Direct);
            Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 4, CT_Direct);
            PipelineElementPtr pPipelineElementAudioEncoder = PipelineElementPtr(new AMFComponentElement(m_pAudioEncoder));
            Connect(pPipelineElementAudioEncoder, 10, CT_Direct);
            Connect(pPipelineElementMuxer, outAudioStreamIndex, pPipelineElementAudioEncoder, 0, 10, CT_ThreadQueue);
        }
    }
    if(m_pSplitter != NULL)
    {
        PipelineElementPtr pConverterElement = PipelineElementPtr(new AMFComponentElement(m_pConverter));

        CHECK_AMF_ERROR_RETURN(
            BackBufferPresenter::Create(m_pPresenter, engineMemoryType, previewTarget, m_pContext),
            "Failed to create a video presenter"
        );

        AMFSize frame;
        m_pDecoder->GetProperty(AMF_VIDEO_DECODER_CURRENT_SIZE, &frame);
        m_pPresenter->Init(frame.width, frame.height);

        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter2);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");
        m_pConverter2->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, m_pPresenter->GetMemoryType());
        m_pConverter2->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_pPresenter->GetInputFormat());
        res = m_pConverter2->Init(m_eDecoderFormat, frame.width, frame.height);
        CHECK_AMF_ERROR_RETURN(res, L"m_pConverter->Init() failed");

        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter2)), 0, m_pSplitter, 1, 4, CT_ThreadQueue);
        Connect(m_pPresenter, 4, CT_ThreadQueue);
    }
    return res;
}


AMF_RESULT TranscodePipeline::Run()
{
    AMF_RESULT res = AMF_OK;
    Pipeline::Start();
    return res;
}

AMF_RESULT  TranscodePipeline::InitAudio(amf::AMFOutput* pOutput)
{
    AMF_RESULT res = AMF_OK;

    amf_int64 codecID = 0;
    amf_int64 streamBitRate = 0;
    amf_int64 streamSampleRate = 0;
    amf_int64 streamChannels = 0;
    amf_int64 streamFormat = 0;
    amf_int64 streamLayout = 0;
    amf_int64 streamBlockAlign = 0;
    amf_int64 streamFrameSize = 0;
    amf::AMFInterfacePtr pExtradata;

    pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);

    pOutput->GetProperty(AMF_STREAM_BIT_RATE, &streamBitRate);
    pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pExtradata);

    pOutput->GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &streamSampleRate);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNELS, &streamChannels);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FORMAT, &streamFormat);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    pOutput->GetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, &streamFrameSize);

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_DECODER, &m_pAudioDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_DECODER << L") failed");

    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CODEC_ID, codecID);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, pExtradata);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, streamFrameSize);

    res = m_pAudioDecoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioDecoder->Init() failed");

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_CONVERTER << L") failed");

    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNELS, &streamChannels);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);


    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);



    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_ENCODER, &m_pAudioEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_ENCODER << L") failed");

    m_pAudioEncoder->SetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, CODEC_ID_AAC); // switch to AAC for output
    res = m_pAudioEncoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioEncoder->Init() failed");

    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &streamChannels);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &streamBlockAlign);

    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    res = m_pAudioConverter->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioConverter->Init() failed");

    pOutput->SetProperty(AMF_STREAM_ENABLED, true);

   return AMF_OK;
}
AMF_RESULT  TranscodePipeline::InitVideoDecoder(const wchar_t *pDecoderID, amf_int64 codecID, amf_int32 videoWidth, amf_int32 videoHeight, AMFRate frameRate, amf::AMFBuffer* pExtraData)
{
    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = AMF_VIDEO_DECODER_MODE_COMPLIANT; //amf:::AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;
    AMF_RESULT res = AMF_OK;

    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, pDecoderID, &m_pDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoDecoderUVD_H264_AVC << L") failed");

    // check resolution

    amf::AMFCapsPtr pCaps;
    m_pDecoder->GetCaps(&pCaps);
    if (pCaps != nullptr)
    {
        amf::AMFIOCapsPtr pInputCaps;
        pCaps->GetInputCaps(&pInputCaps);
        if (pInputCaps != nullptr)
        {
            amf_int32 minWidth = 0;
            amf_int32 maxWidth = 0;
            amf_int32 minHeight = 0;
            amf_int32 maxHeight = 0;
            pInputCaps->GetWidthRange(&minWidth, &maxWidth);
            pInputCaps->GetHeightRange(&minHeight, &maxHeight);
            if (minWidth <= videoWidth && videoWidth <= maxWidth && minHeight <= videoHeight && videoHeight <= maxHeight)
            {
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));
                m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
                m_eDecoderFormat = amf::AMF_SURFACE_NV12;
                if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
                {
                    m_eDecoderFormat = amf::AMF_SURFACE_P010;
                }
                res = m_pDecoder->Init(m_eDecoderFormat, videoWidth, videoHeight);
                CHECK_AMF_ERROR_RETURN(res, L"m_pDecoder->Init(" << videoWidth << videoHeight << L") failed " << pDecoderID);
				if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
				{
					//Only up to above m_pVideoDecoder->Init(), got/updated actual eDecoderFormat, then also update m_eDecoderFormat here.
					amf_int64 format = 0;
					if (m_pDecoder->GetProperty(L"AV1 Stream Format", &format) == AMF_OK)
					{
						if (m_pDecoder->GetProperty(L"AV1 Stream Format", &format) == AMF_OK)
						{
							m_eDecoderFormat = (amf::AMF_SURFACE_FORMAT)format;
						}
					}
				}
                //m_bCPUDecoder = false;
            }
            else
            {
                m_pDecoder = nullptr;
            }
        }
    }

    if (m_pDecoder == nullptr)
    {
        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_VIDEO_DECODER, &m_pDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_VIDEO_DECODER << L") failed");

        if (codecID == 0)
        {
            if (std::wstring(pDecoderID) == AMFVideoDecoderUVD_H264_AVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H264_AVC;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_MAIN10;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_HEVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_HEVC;
            }
			else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
			{
				codecID = AMF_STREAM_CODEC_ID_AV1;
			}
        }
        m_pDecoder->SetProperty(VIDEO_DECODER_CODEC_ID, codecID);
        m_pDecoder->SetProperty(VIDEO_DECODER_BITRATE, 0);
        m_pDecoder->SetProperty(VIDEO_DECODER_FRAMERATE, frameRate);

        //      m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
        m_pDecoder->SetProperty(VIDEO_DECODER_EXTRA_DATA, amf::AMFVariant(pExtraData));

        m_eDecoderFormat = amf::AMF_SURFACE_NV12;
        if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
        {
            m_eDecoderFormat = amf::AMF_SURFACE_P010;
        }
        res = m_pDecoder->Init(m_eDecoderFormat, videoWidth, videoHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pDecoder->Init(" << videoWidth << videoHeight << L") failed " << pDecoderID);
        //m_bCPUDecoder = true;
    }
    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitVideoProcessor(amf::AMF_MEMORY_TYPE presenterEngine, amf_int32 inWidth, amf_int32 inHeight, amf_int32 outWidth, amf_int32 outHeight)
{
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

    
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, presenterEngine);
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_NV12);
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(outWidth, outHeight));

    m_pConverter->Init(m_eDecoderFormat, inWidth, inHeight);

    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitPreProcessFilter(ParametersStorage* pParams)
{
    amf::AMFComponentPtr  spPreProcFilter;
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFPreProcessing, &spPreProcFilter);
    CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing creation failed");
    m_pPreProcFilter = spPreProcFilter;

    // get memory type for the edge filter
    amf::AMF_MEMORY_TYPE  engineType = amf::AMF_MEMORY_HOST;
    if (pParams->GetParam(AMF_PP_ENGINE_TYPE, (int&)engineType) == AMF_OK)
    {
        res = m_pPreProcFilter->SetProperty(AMF_PP_ENGINE_TYPE, (int)engineType);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set engine type");
    }

    // get tor and sigma values
    double sigma = 0;
    if (pParams->GetParam(AMF_PP_ADAPTIVE_FILTER_STRENGTH, sigma) == AMF_OK)
    {
        res = m_pPreProcFilter->SetProperty(AMF_PP_ADAPTIVE_FILTER_STRENGTH, sigma);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set sigma");
    }
    double tor = 0;
    if (pParams->GetParam(AMF_PP_ADAPTIVE_FILTER_SENSITIVITY, tor) == AMF_OK)
    {
        m_pPreProcFilter->SetProperty(AMF_PP_ADAPTIVE_FILTER_SENSITIVITY, tor);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set tor");
    }

    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitVideo(BitStreamParserPtr pParser, RawStreamReaderPtr pRawReader, amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine, amf_handle hwnd, amf_handle display, ParametersStorage* pParams)
{
    bool decodeAsAnnexBStream = false; // switches between Annex B and AVCC types of decode input.

    AMF_RESULT res = AMF_OK; 
    amf::AMFBufferPtr pExtraData;
    amf_int32 videoWidth = 0;
    amf_int32 videoHeight = 0;
    std::wstring pVideoDecoderID;
    AMFRate frameRate = {};

    amf_int64 codecID = 0;
    if(pParser != NULL)
    {
        if(!decodeAsAnnexBStream) // need to provide SPS/PPS if input stream will be AVCC ( not Annex B)
        {
            const unsigned char* extraData = pParser->GetExtraData();
            size_t extraDataSize = pParser->GetExtraDataSize();
            if (extraData && extraDataSize)
            {
                m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, extraDataSize, &pExtraData);

                memcpy(pExtraData->GetNative(), extraData, extraDataSize);
            }
        }
        videoWidth = pParser->GetPictureWidth();
        videoHeight = pParser->GetPictureHeight();
        pParser->GetFrameRate(&frameRate);
    
        pVideoDecoderID = pParser->GetCodecComponent();

    }
    else if(pOutput != NULL)
    { 
        amf::AMFInterfacePtr pInterface;
        pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pInterface);
        pExtraData = amf::AMFBufferPtr(pInterface);

        AMFSize frameSize;
        pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &frameSize);
        videoWidth = frameSize.width;
        videoHeight = frameSize.height;
        
        res= pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
        pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));

        pOutput->SetProperty(AMF_STREAM_ENABLED, true);
        pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &frameRate);
    }
    else if (pRawReader != NULL)
    {
        videoWidth = pRawReader->GetWidth();
        videoHeight = pRawReader->GetHeight();
        frameRate.num = 30;
        frameRate.den = 1;
        m_eDecoderFormat = pRawReader->GetFormat();
    }
    //---------------------------------------------------------------------------------------------
    if ((pParser != NULL) || (pOutput != NULL))
    {
        // Init Video Decoder
        res = InitVideoDecoder(pVideoDecoderID.c_str(), codecID, videoWidth, videoHeight, frameRate, pExtraData);
        CHECK_AMF_ERROR_RETURN(res, L"InitVideoDecoder() failed");
    }

    //---------------------------------------------------------------------------------------------

    amf_int scaleWidth = 0;    // if 0 - no scaling
    amf_int scaleHeight = 0;   // if 0 - no scaling

    pParams->GetParam(PARAM_NAME_SCALE_WIDTH, scaleWidth);
    pParams->GetParam(PARAM_NAME_SCALE_HEIGHT, scaleHeight);

    if(scaleWidth == 0)
    {
        scaleWidth = videoWidth;
    }
    if(scaleHeight == 0)
    {
        scaleHeight = videoHeight;
    }

    //---------------------------------------------------------------------------------------------
    // Init Video Converter/Processor
    res = InitVideoProcessor(presenterEngine, videoWidth, videoHeight, scaleWidth, scaleHeight);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoProcessor() failed");

    if(hwnd != NULL)
    { 
        // Init Presenter
        CHECK_AMF_ERROR_RETURN(
            BackBufferPresenter::Create(m_pPresenter, presenterEngine, hwnd, m_pContext, display),
            "Failed to create a video presenter"
        );

        m_pPresenter->Init(scaleWidth, scaleHeight);
    }

    //---------------------------------------------------------------------------------------------
    // init pre processing filter
    amf_bool  preProcfilterEnable = false;
    pParams->GetParam(AMF_VIDEO_PRE_ENCODE_FILTER_ENABLE, preProcfilterEnable);
    if (preProcfilterEnable)
    {
        InitPreProcessFilter(pParams);

        res = m_pPreProcFilter->Init(amf::AMF_SURFACE_NV12, scaleWidth, scaleHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pPreProcFilter->Init() failed");
    }

    //---------------------------------------------------------------------------------------------
    // Init Video Encoder


    m_EncoderID = AMFVideoEncoderVCE_AVC;
    pParams->GetParamWString(PARAM_NAME_CODEC, m_EncoderID);

    if(m_EncoderID == AMFVideoEncoderVCE_AVC)
    { 
        amf_int64 usage = 0;
        if(pParams->GetParam(AMF_VIDEO_ENCODER_USAGE, usage) == AMF_OK)
        {
            if(usage == amf_int64(AMF_VIDEO_ENCODER_USAGE_WEBCAM))
            {
                m_EncoderID = AMFVideoEncoderVCE_SVC;
            }
        }
    }

    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, m_EncoderID.c_str(), &m_pEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << m_EncoderID << L") failed");

    // Usage is preset that will set many parameters
    PushParamsToPropertyStorage(pParams, ParamEncoderUsage, m_pEncoder); 

    // if we enable PA, we need to make sure RateCotrolMode gets set first
    // otherwise setting the PA properties might not work...
    if (m_EncoderID == AMFVideoEncoderVCE_AVC)
    {
        amf::AMFVariant  enablePA;
        if (pParams->GetParam(AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, enablePA) == AMF_OK)
        {
            amf::AMFVariant  rateControlMode;
            if (pParams->GetParam(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, rateControlMode) == AMF_OK)
            {
                m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, rateControlMode);
            }

            m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, enablePA);
        }
    }
    else if (m_EncoderID == AMFVideoEncoder_HEVC)
    {
        amf::AMFVariant  enablePA;
        if (pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, enablePA) == AMF_OK)
        {
            amf::AMFVariant  rateControlMode;
            if (pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, rateControlMode) == AMF_OK)
            {
                m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, rateControlMode);
            }

            m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, enablePA);
        }
    }

	// override some usage parameters
	if (frameRate.den != 0 && frameRate.num != 0)
	{
		if (m_EncoderID == AMFVideoEncoderVCE_AVC || m_EncoderID == AMFVideoEncoderVCE_SVC)
		{
			m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
		}
		else
		{
			m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, frameRate);
		}
	}

    PushParamsToPropertyStorage(pParams, ParamEncoderStatic, m_pEncoder);

//    m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(scaleWidth, scaleHeight));


    PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_pEncoder);

    res = m_pEncoder->Init(amf::AMF_SURFACE_NV12, scaleWidth, scaleHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pEncoder->Init() failed");

//    PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_pEncoder);
//    m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_EXTRADATA, NULL); //samll way - around  forces to regenerate extradata

    return AMF_OK;
}