/*
 *      Copyright (C) 2018 Christian Browet
 *      Copyright (C) 2018 Team MrMC (https://github.com/MrMC)
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <string>

#include "ProgressThumbnailer.h"

#include "DVDFileInfo.h"
#include "DVDStreamInfo.h"
#include "FileItem.h"
#include "cores/FFmpeg.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStream.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStreamBluray.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDFactoryInputStream.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemux.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemuxUtils.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDFactoryDemuxer.h"
#include "cores/VideoPlayer/DVDDemuxers/DVDDemuxFFmpeg.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecs.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecUtils.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecFFmpeg.h"
#include "cores/VideoPlayer/DVDClock.h"
#include "filesystem/StackDirectory.h"
#include "video/VideoInfoTag.h"
#include "utils/log.h"

#include "guilib/GUIMessage.h"
#include "messaging/ApplicationMessenger.h"
#include "TextureCache.h"
#include "pictures/Picture.h"

using namespace KODI::MESSAGING;


CProgressThumbnailer::CProgressThumbnailer(const CFileItem& item, int width, int controlId)
  : CThread("ProgressThumbNailer")
  , m_width(width)
  , m_controlId(controlId)
{
  m_path = item.GetPath();
  m_redactPath = CURL::GetRedacted(m_path);

  if (item.IsVideoDb() && item.HasVideoInfoTag())
    m_path = item.GetVideoInfoTag()->m_strFileNameAndPath;

  if (item.IsStack())
    m_path = XFILE::CStackDirectory::GetFirstStackedFile(item.GetPath());

  if (item.HasVideoInfoTag())
    m_totalTimeMilliSeconds = 1000 * lrint(item.GetVideoInfoTag()->m_streamDetails.GetVideoDuration());

  CThread::Create();
}

CProgressThumbnailer::~CProgressThumbnailer()
{
  if (m_videoDemuxer)
    m_videoDemuxer->Abort();
  if (m_inputStream)
    m_inputStream->Abort();

  if (IsRunning())
  {
    m_bStop = true;
    m_processSleep.Set();
    StopThread();
  }
}

void CProgressThumbnailer::RequestThumb(int posMs)
{
  m_seekQueue.push(posMs);
  m_processSleep.Set();
}

void CProgressThumbnailer::Process()
{
  CFileItem item(m_path, false);
  m_inputStream = CDVDFactoryInputStream::CreateInputStream(NULL, item);
  if (!m_inputStream)
  {
    CLog::Log(LOGERROR, "CProgressThumbnailer::ExtractThumb: Error creating stream for %s", m_redactPath.c_str());
    return;
  }

  if (m_inputStream->IsStreamType(DVDSTREAM_TYPE_DVD)
   || m_inputStream->IsStreamType(DVDSTREAM_TYPE_BLURAY))
  {
    CLog::Log(LOGDEBUG, "CProgressThumbnailer::ExtractThumb: disc streams not supported for thumb extraction, file: %s", m_redactPath.c_str());
    return;
  }

  if (m_inputStream->IsStreamType(DVDSTREAM_TYPE_TV))
  {
    return;
  }

  if (m_bStop)
    return;

  if (!m_inputStream->Open())
  {
    CLog::Log(LOGERROR, "InputStream: Error opening, %s", m_redactPath.c_str());
    return;
  }

  if (m_bStop)
    return;

  try
  {
    m_videoDemuxer.reset(CDVDFactoryDemuxer::CreateDemuxer(m_inputStream, false));
    if(!m_videoDemuxer)
    {
      CLog::Log(LOGERROR, "%s - Error creating demuxer", __FUNCTION__);
      return;
    }
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown when opening demuxer", __FUNCTION__);
    return;
  }

  if (m_bStop)
    return;

  for (CDemuxStream* pStream : m_videoDemuxer->GetStreams())
  {
    if (pStream)
    {
      // ignore if it's a picture attachment (e.g. jpeg artwork)
      if (pStream->type == STREAM_VIDEO && !(pStream->flags & AV_DISPOSITION_ATTACHED_PIC))
      {
        m_videoStream = pStream->uniqueId;
        m_demuxerId = pStream->demuxerId;
      }
      else
        m_videoDemuxer->EnableStream(pStream->demuxerId, pStream->uniqueId, false);
    }
  }

  if (m_videoStream != -1)
  {
    m_processInfo.reset(CProcessInfo::CreateInstance());
    std::vector<AVPixelFormat> pixFmts;
    pixFmts.push_back(AV_PIX_FMT_YUV420P);
    m_processInfo->SetPixFormats(pixFmts);

    CDVDStreamInfo hint(*m_videoDemuxer->GetStream(m_demuxerId, m_videoStream), true);
    hint.codecOptions = CODEC_FORCE_SOFTWARE;

    m_videoCodec.reset(CDVDFactoryCodec::CreateVideoCodec(hint, *m_processInfo));
    if (!m_videoCodec)
    {
      CLog::Log(LOGERROR, "%s - Error creating codec", __FUNCTION__);
      return;
    }
    m_aspect = hint.aspect;
    m_forced_aspect = hint.forced_aspect;
  }

  if (m_totalTimeMilliSeconds <= 0)
    m_totalTimeMilliSeconds = m_videoDemuxer->GetStreamLength();

  while (!m_bStop)
  {
    if (!m_seekQueue.empty())
    {
      CSingleLock lock(m_seekQueueCritical);
      m_seekTimeMilliSeconds = m_seekQueue.front();
      m_seekQueue.pop();
      lock.Leave();

#if enableDebugLogging
      CLog::Log(LOGDEBUG, "QueueExtractThumb - requested(%d)", m_seekTimeMilliSeconds);
#endif
      QueueExtractThumb(m_seekTimeMilliSeconds);
    }
    m_processSleep.WaitMSec(10);
    m_processSleep.Reset();
  }
}

void CProgressThumbnailer::QueueExtractThumb(int seekTime)
{
  if (!m_videoDemuxer || !m_videoCodec)
    return;

#if enableDebugLogging
  unsigned int nTime = XbmcThreads::SystemClockMillis();
#endif

  // reset codec on entry, we have no
  // clue about previous state.
  m_videoCodec->Reset();

  int packetsTried = 0;
  // timebase is ms
  if (m_videoDemuxer->SeekTime(seekTime, true))
  {
    CDVDVideoCodec::VCReturn iDecoderState = CDVDVideoCodec::VC_NONE;
    VideoPicture picture = {};

    if (m_bStop)
      return;

    // num streams * 160 frames, should get a valid frame, if not abort.
    int abort_index = m_videoDemuxer->GetNrOfStreams() * 160;
    do
    {
      DemuxPacket* pPacket = m_videoDemuxer->Read();
      packetsTried++;

      if (!pPacket)
        break;

      if (pPacket->iStreamId != m_videoStream)
      {
        CDVDDemuxUtils::FreeDemuxPacket(pPacket);
        continue;
      }

      m_videoCodec->AddData(*pPacket);
      CDVDDemuxUtils::FreeDemuxPacket(pPacket);

      iDecoderState = CDVDVideoCodec::VC_NONE;
      while (iDecoderState == CDVDVideoCodec::VC_NONE && !m_bStop)
      {
        iDecoderState = m_videoCodec->GetPicture(&picture);
      }

      if (iDecoderState == CDVDVideoCodec::VC_PICTURE)
      {
        if(!(picture.iFlags & DVP_FLAG_DROPPED))
          break;
      }

    } while (!m_bStop && abort_index--);

    if (m_bStop)
      return;

    if (iDecoderState == CDVDVideoCodec::VC_PICTURE && !(picture.iFlags & DVP_FLAG_DROPPED))
    {
      unsigned int nWidth = m_width;
      double aspect = (double)picture.iDisplayWidth / (double)picture.iDisplayHeight;
      if(m_forced_aspect && m_aspect != 0)
        aspect = m_aspect;
      unsigned int nHeight = (unsigned int)((double)m_width / aspect);

      uint8_t *pOutBuf = (uint8_t*)av_malloc(nWidth * nHeight * 4);
      struct SwsContext *context = sws_getContext(picture.iWidth, picture.iHeight,
        AV_PIX_FMT_YUV420P, nWidth, nHeight, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, NULL, NULL, NULL);

      if (context)
      {
        uint8_t *planes[YuvImage::MAX_PLANES];
        int stride[YuvImage::MAX_PLANES];
        picture.videoBuffer->GetPlanes(planes);
        picture.videoBuffer->GetStrides(stride);
        uint8_t *src[4] = { planes[0], planes[1], planes[2], 0 };
        int srcStride[] = { stride[0], stride[1], stride[2], 0 };
        uint8_t *dst[] = { pOutBuf, 0, 0, 0 };
        int dstStride[] = { (int)nWidth * 4, 0, 0, 0 };
        sws_scale(context, src, srcStride, 0, picture.iHeight, dst, dstStride);
        sws_freeContext(context);


        if (m_bStop)
          return;

        std::string outfile = CTextureCache::GetInstance().GetCachedPath(CTextureCache::GetCacheFile(StringUtils::Format("%s#%d", m_path.c_str(), seekTime)) + ".jpg");
        CPicture::CacheTexture(pOutBuf, nWidth, nHeight, nWidth * 4, 0, nWidth, nHeight, outfile);
        CLog::Log(LOGDEBUG, "%s - created thumb %s", __FUNCTION__, outfile.c_str());

        CGUIMessage m(GUI_MSG_REFRESH_LIST, m_controlId, 0, 2, seekTime);
        CApplicationMessenger::GetInstance().SendGUIMessage(m);
      }
      av_free(pOutBuf);
    }
    else
    {
      CLog::Log(LOGDEBUG,"%s - decode failed in %s after %d packets.", __FUNCTION__, m_redactPath.c_str(), packetsTried);
    }
  }

#if enableDebugLogging
  unsigned int nTotalTime = XbmcThreads::SystemClockMillis() - nTime;
  CLog::Log(LOGDEBUG,"%s - measured %u ms to extract thumb at %d in %d packets. ", __FUNCTION__, nTotalTime, thumbNailerImage.time, packetsTried);
#endif
  return;
}
