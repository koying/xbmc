#pragma once
/*
 *      Copyright (C) 2018 Christian Browet
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

#include <pthread.h>

#include <androidjni/Service.h>
#include <androidjni/Context.h>
#include <androidjni/BroadcastReceiver.h>
#include <androidjni/Intent.h>

#include "threads/Event.h"
#include "threads/SharedSection.h"
#include "platform/android/JNIXBMCBroadcastReceiver.h"

class CXBMCService
    : public CJNIService
    , public CJNIBroadcastReceiver
{
  friend class XBMCApp;

public:
  CXBMCService(jobject thiz);

  static CXBMCService* get() { return m_xbmcserviceinstance; }
  static void _launchApplication(JNIEnv*, jobject thiz);
  static jint _getStatus(JNIEnv*, jobject thiz);
  int android_printf(const char* format...);

  void InitDirectories();
  void Deinitialize();

  static bool GetExternalStorage(std::string &path, const std::string &type = "");
  static bool GetStorageUsage(const std::string &path, std::string &usage);
  static void SetStatus(int status) { m_status = status; }

  // CJNIBroadcastReceiver interface
  void onReceive(CJNIIntent intent) override;

  bool IsHeadsetPlugged();
  bool IsHDMIPlugged();

protected:
  void run();
  void SetupEnv();

  CEvent m_appReady;
  std::unique_ptr<jni::CJNIXBMCBroadcastReceiver> m_broadcastReceiver;
  bool m_headsetPlugged;
  bool m_hdmiPlugged;

private:
  static CCriticalSection m_SvcMutex;
  static bool m_SvcThreadCreated;
  static pthread_t m_SvcThread;
  static CXBMCService* m_xbmcserviceinstance;
  static int m_status;

  void StartApplication();
  void StopApplication();
};
