#pragma once

/*
 *      Copyright (C) 2018 Team XBMC
 *      http://kodi.tv
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <queue>

#include "threads/Thread.h"
#include "threads/CriticalSection.h"
#include "FileItem.h"

class CDVDInputStream;
class CDVDDemux;
class CDVDVideoCodec;
class CProcessInfo;

class CProgressThumbnailer
    : public CThread
{
public:
  CProgressThumbnailer(const CFileItem& item, int width, int controlId);
 ~CProgressThumbnailer();

  bool IsInitialized() { return m_videoCodec != nullptr; }
  void RequestThumb(int posMs);

private:
  void Process();
  void QueueExtractThumb(int seekTime);

  int m_controlId;
  int m_width;
  std::string m_path;
  std::string m_redactPath;
  double m_aspect;
  bool m_forced_aspect;
  int m_seekTimeMilliSeconds = -1;
  int m_totalTimeMilliSeconds = -1;
  CEvent m_processSleep;
  std::queue<int> m_seekQueue;
  CCriticalSection m_seekQueueCritical;
  int64_t m_demuxerId = -1;
  int m_videoStream = -1;
  std::shared_ptr<CDVDInputStream> m_inputStream;
  std::unique_ptr<CDVDDemux> m_videoDemuxer;
  std::unique_ptr<CDVDVideoCodec> m_videoCodec;
  std::unique_ptr<CProcessInfo> m_processInfo;
};
