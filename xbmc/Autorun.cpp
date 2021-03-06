/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "system.h"

#ifdef HAS_DVD_DRIVE

#include "Autorun.h"
#include "Application.h"
#include "Util.h"
#include "GUIPassword.h"
#include "GUIUserMessages.h"
#include "PlayListPlayer.h"
#include "filesystem/StackDirectory.h"
#include "filesystem/Directory.h"
#include "filesystem/FactoryDirectory.h"
#include "settings/GUISettings.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "playlists/PlayList.h"
#include "guilib/GUIWindowManager.h"
#include "storage/MediaManager.h"
#include "video/VideoDatabase.h"

using namespace std;
using namespace XFILE;
using namespace PLAYLIST;
using namespace MEDIA_DETECT;

CAutorun::CAutorun()
{
  m_bEnable = true;
}

CAutorun::~CAutorun()
{}

void CAutorun::ExecuteAutorun( bool bypassSettings, bool ignoreplaying, bool restart )
{
  if ((!ignoreplaying && (g_application.IsPlayingAudio() || g_application.IsPlayingVideo() || g_windowManager.HasModalDialog())) || g_windowManager.GetActiveWindow() == WINDOW_LOGIN_SCREEN)
    return ;

  CCdInfo* pInfo = g_mediaManager.GetCdInfo();

  if ( pInfo == NULL )
    return ;

  g_application.ResetScreenSaver();
  g_application.WakeUpScreenSaverAndDPMS();  // turn off the screensaver if it's active

  if ( pInfo->IsAudio( 1 ) )
  {
    if( !bypassSettings && !g_guiSettings.GetBool("audiocds.autorun") )
      return;

    if (!g_passwordManager.IsMasterLockUnlocked(false))
      if (g_settings.GetCurrentProfile().musicLocked())
        return ;

    RunCdda();
  }
  else
  {
    RunMedia(bypassSettings, restart);
  }
}

void CAutorun::RunCdda()
{
  CFileItemList vecItems;

  auto_ptr<IDirectory> pDir ( CFactoryDirectory::Create( "cdda://local/" ) );
  if ( !pDir->GetDirectory( "cdda://local/", vecItems ) )
    return ;

  if ( vecItems.Size() <= 0 )
    return ;

  g_playlistPlayer.ClearPlaylist(PLAYLIST_MUSIC);
  g_playlistPlayer.Add(PLAYLIST_MUSIC, vecItems);
  g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_MUSIC);
  g_playlistPlayer.Play();
}

void CAutorun::RunMedia(bool bypassSettings, bool restart)
{
  if ( !bypassSettings && !g_guiSettings.GetBool("audiocds.autorun") && !g_guiSettings.GetBool("dvds.autorun"))
    return ;

  int nSize = g_playlistPlayer.GetPlaylist( PLAYLIST_MUSIC ).size();
  int nAddedToPlaylist = 0;
#ifdef _WIN32
  auto_ptr<IDirectory> pDir ( CFactoryDirectory::Create( g_mediaManager.TranslateDevicePath("") ));
  bool bPlaying = RunDisc(pDir.get(), g_mediaManager.TranslateDevicePath(""), nAddedToPlaylist, true, bypassSettings, restart);
#else
  CCdInfo* pInfo = g_mediaManager.GetCdInfo();

  if ( pInfo == NULL )
    return ;

  bool bPlaying;
  if (pInfo->IsISOUDF(1) || pInfo->IsISOHFS(1) || pInfo->IsIso9660(1) || pInfo->IsIso9660Interactive(1))
  {
    auto_ptr<IDirectory> pDir ( CFactoryDirectory::Create( "iso9660://" ));
    bPlaying = RunDisc(pDir.get(), "iso9660://", nAddedToPlaylist, true, bypassSettings, restart);
  }
  else
  {
    auto_ptr<IDirectory> pDir ( CFactoryDirectory::Create( "D:\\" ) );
    bPlaying = RunDisc(pDir.get(), "D:\\", nAddedToPlaylist, true, bypassSettings, restart);
  }
#endif
  if ( !bPlaying && nAddedToPlaylist > 0 )
  {
    CGUIMessage msg( GUI_MSG_PLAYLIST_CHANGED, 0, 0 );
    g_windowManager.SendMessage( msg );
    g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_MUSIC);
    // Start playing the items we inserted
    g_playlistPlayer.Play(nSize);
  }
}

/**
 * This method tries to determine what type of disc is located in the given drive and starts to play the content appropriately.
 */
bool CAutorun::RunDisc(IDirectory* pDir, const CStdString& strDrive, int& nAddedToPlaylist, bool bRoot, bool bypassSettings /* = false */, bool restart /* = false */)
{
  bool bPlaying(false);
  CFileItemList vecItems;
  char szSlash = '\\';
  if (strDrive.Find("iso9660") != -1) szSlash = '/';

  if ( !pDir->GetDirectory( strDrive, vecItems ) )
  {
    return false;
  }

  bool bAllowVideo = true;
  bool bAllowPictures = true;
  bool bAllowMusic = true;
  if (!g_passwordManager.IsMasterLockUnlocked(false))
  {
    bAllowVideo = !g_settings.GetCurrentProfile().videoLocked();
    bAllowPictures = !g_settings.GetCurrentProfile().picturesLocked();
    bAllowMusic = !g_settings.GetCurrentProfile().musicLocked();
  }

  // is this a root folder we have to check the content to determine a disc type
  if( bRoot )
  {

    // check root folders first, for normal structured dvd's
    for (int i = 0; i < vecItems.Size(); i++)
    {
      CFileItemPtr pItem = vecItems[i];

      // is the current item a (non system) folder?
      if (pItem->m_bIsFolder && pItem->GetPath() != "." && pItem->GetPath() != "..")
      {
        // Check if the current foldername indicates a DVD structure (name is "VIDEO_TS")
        if (pItem->GetPath().Find( "VIDEO_TS" ) != -1 && bAllowVideo
        && (bypassSettings || g_guiSettings.GetBool("dvds.autorun")))
        {
          CUtil::PlayDVD("dvd", restart);
          bPlaying = true;
          return true;
        }

        // Check if the current foldername indicates a Blu-Ray structure (default is "BDMV").
        // A BR should also include an "AACS" folder for encryption, Sony-BRs can also include update folders for PS3 (PS3_UPDATE / PS3_VPRM).
        // ToDo: for the time beeing, the DVD autorun settings are used to determine if the BR should be started automatically.
        if (pItem->GetPath().Find( "BDMV" ) != -1 && bAllowVideo
        && (bypassSettings || g_guiSettings.GetBool("dvds.autorun")))
        {
          CUtil::PlayDVD("bd", restart);
          bPlaying = true;
          return true;
        }

        // Video CDs can have multiple file formats. First we need to determine which one is used on the CD
        CStdString strExt;
        if (pItem->GetPath().Find("MPEGAV") != -1)
          strExt = ".dat";
        if (pItem->GetPath().Find("MPEG2") != -1)
          strExt = ".mpg";

        // If a file format was extracted we are sure this is a VCD. Autoplay if settings indicate we should.
        if (!strExt.IsEmpty() && bAllowVideo
             && (bypassSettings || g_guiSettings.GetBool("dvds.autorun")))
        {
          CFileItemList items;
          CDirectory::GetDirectory(pItem->GetPath(), items, strExt);
          if (items.Size())
          {
            items.Sort(SORT_METHOD_LABEL, SORT_ORDER_ASC);
            g_playlistPlayer.ClearPlaylist(PLAYLIST_VIDEO);
            g_playlistPlayer.Add(PLAYLIST_VIDEO, items);
            g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
            g_playlistPlayer.Play(0);
            bPlaying = true;
            return true;
          }
        }
        /* Probably want this if/when we add some automedia action dialog...
        else if (pItem->GetPath().Find("PICTURES") != -1 && bAllowPictures
              && (bypassSettings))
        {
          bPlaying = true;
          CStdString strExec;
          strExec.Format("XBMC.RecursiveSlideShow(%s)", pItem->GetPath().c_str());
          CBuiltins::Execute(strExec);
          return true;
        }
        */
      }
    }
  }

  // check video first
  if (!nAddedToPlaylist && !bPlaying && (bypassSettings || g_guiSettings.GetBool("dvds.autorun")))
  {
    // stack video files
    CFileItemList tempItems;
    tempItems.Append(vecItems);
    if (g_settings.m_videoStacking)
      tempItems.Stack();
    CFileItemList itemlist;

    for (int i = 0; i < tempItems.Size(); i++)
    {
      CFileItemPtr pItem = tempItems[i];
      if (!pItem->m_bIsFolder && pItem->IsVideo())
      {
        bPlaying = true;
        if (pItem->IsStack())
        {
          // TODO: remove this once the app/player is capable of handling stacks immediately
          CStackDirectory dir;
          CFileItemList items;
          dir.GetDirectory(pItem->GetPath(), items);
          itemlist.Append(items);
        }
        else
          itemlist.Add(pItem);
      }
    }
    if (itemlist.Size())
    {
      if (!bAllowVideo)
      {
        if (!bypassSettings)
          return false;

        if (g_windowManager.GetActiveWindow() != WINDOW_VIDEO_FILES)
          if (!g_passwordManager.IsMasterLockUnlocked(true))
            return false;
      }
      g_playlistPlayer.ClearPlaylist(PLAYLIST_VIDEO);
      g_playlistPlayer.Add(PLAYLIST_VIDEO, itemlist);
      g_playlistPlayer.SetCurrentPlaylist(PLAYLIST_VIDEO);
      g_playlistPlayer.Play(0);
    }
  }
  // then music
  if (!bPlaying && (bypassSettings || g_guiSettings.GetBool("audiocds.autorun")) && bAllowMusic)
  {
    for (int i = 0; i < vecItems.Size(); i++)
    {
      CFileItemPtr pItem = vecItems[i];
      if (!pItem->m_bIsFolder && pItem->IsAudio())
      {
        nAddedToPlaylist++;
        g_playlistPlayer.Add(PLAYLIST_MUSIC, pItem);
      }
    }
  }
  /* Probably want this if/when we add some automedia action dialog...
  // and finally pictures
  if (!nAddedToPlaylist && !bPlaying && bypassSettings && bAllowPictures)
  {
    for (int i = 0; i < vecItems.Size(); i++)
    {
      CFileItemPtr pItem = vecItems[i];
      if (!pItem->m_bIsFolder && pItem->IsPicture())
      {
        bPlaying = true;
        CStdString strExec;
        strExec.Format("XBMC.RecursiveSlideShow(%s)", strDrive.c_str());
        CBuiltins::Execute(strExec);
        break;
      }
    }
  }
  */

  // check subdirs if we are not playing yet
  if (!bPlaying)
  {
    for (int i = 0; i < vecItems.Size(); i++)
    {
      CFileItemPtr  pItem = vecItems[i];
      if (pItem->m_bIsFolder)
      {
        if (pItem->GetPath() != "." && pItem->GetPath() != ".." )
        {
          if (RunDisc(pDir, pItem->GetPath(), nAddedToPlaylist, false, bypassSettings, restart))
          {
            bPlaying = true;
            break;
          }
        }
      } // if (non system) folder
    } // for all items in directory
  } // if root folder

  return bPlaying;
}

void CAutorun::HandleAutorun()
{
#ifndef _WIN32
  if (!m_bEnable)
  {
    CDetectDVDMedia::m_evAutorun.Reset();
    return ;
  }

  if (CDetectDVDMedia::m_evAutorun.WaitMSec(0))
  {
    ExecuteAutorun();
    CDetectDVDMedia::m_evAutorun.Reset();
  }
#endif
}

void CAutorun::Enable()
{
  m_bEnable = true;
}

void CAutorun::Disable()
{
  m_bEnable = false;
}

bool CAutorun::IsEnabled() const
{
  return m_bEnable;
}

bool CAutorun::PlayDisc(bool restart)
{
  ExecuteAutorun(true,true, restart);
  return true;
}

bool CAutorun::CanResumePlayDVD()
{
  CStdString strPath = "removable://"; // need to put volume label for resume point in videoInfoTag
  strPath += g_mediaManager.GetDiskLabel();
  CVideoDatabase dbs;
  dbs.Open();
  CBookmark bookmark;
  if (dbs.GetResumeBookMark(strPath, bookmark))
    return true;
  return false;
}

#endif
