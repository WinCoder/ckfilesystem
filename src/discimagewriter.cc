/*
 * Copyright (C) 2006-2008 Christian Kindahl, christian dot kindahl at gmail dot com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <ckcore/string.hh>
#include "stringtable.hh"
#include "sectormanager.hh"
#include "iso9660writer.hh"
#include "udfwriter.hh"
#include "dvdvideo.hh"
#include "discimagewriter.hh"

namespace ckFileSystem
{
	CDiscImageWriter::CDiscImageWriter(ckcore::Log *pLog,eFileSystem FileSystem) : m_FileSystem(FileSystem),
		m_pLog(pLog),m_ElTorito(pLog),m_Udf(FileSystem == FS_DVDVIDEO)
	{
	}

	CDiscImageWriter::~CDiscImageWriter()
	{
	}

	/*
		Calculates file system specific data such as extent location and size for a
		single file.
	*/
	bool CDiscImageWriter::CalcLocalFileSysData(std::vector<std::pair<CFileTreeNode *,int> > &DirNodeStack,
		CFileTreeNode *pLocalNode,int iLevel,ckcore::tuint64 &uiSecOffset,ckcore::Progress &Progress)
	{
		std::vector<CFileTreeNode *>::const_iterator itFile;
		for (itFile = pLocalNode->m_Children.begin(); itFile !=
			pLocalNode->m_Children.end(); itFile++)
		{
			if ((*itFile)->m_ucFileFlags & CFileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (iLevel >= m_Iso9660.GetMaxDirLevel())
					continue;
				else
					DirNodeStack.push_back(std::make_pair(*itFile,iLevel + 1));
			}
			else
			{
				// Validate file size.
				if ((*itFile)->m_uiFileSize > ISO9660_MAX_EXTENT_SIZE && !m_Iso9660.AllowsFragmentation())
				{
					if (m_FileSystem == FS_ISO9660 || m_FileSystem == FS_ISO9660_JOLIET || m_FileSystem == FS_DVDVIDEO)
					{
						m_pLog->PrintLine(ckT("  Warning: Skipping \"%s\", the file is larger than 4 GiB."),
							(*itFile)->m_FileName.c_str());
						Progress.Notify(ckcore::Progress::ckWARNING,g_StringTable.GetString(WARNING_SKIP4GFILE),
							(*itFile)->m_FileName.c_str());

						continue;
					}
					else if (m_FileSystem == FS_ISO9660_UDF || m_FileSystem == FS_ISO9660_UDF_JOLIET)
					{
						m_pLog->PrintLine(ckT("  Warning: The file \"%s\" is larger than 4 GiB. It will not be visible in the ISO9660/Joliet file system."),
							(*itFile)->m_FileName.c_str());
						Progress.Notify(ckcore::Progress::ckWARNING,g_StringTable.GetString(WARNING_SKIP4GFILEISO),
							(*itFile)->m_FileName.c_str());
					}
				}

				// If imported, use the imported information.
				if ((*itFile)->m_ucFileFlags & CFileTreeNode::FLAG_IMPORTED)
				{
					CIso9660ImportData *pImportNode = (CIso9660ImportData *)(*itFile)->m_pData;
					if (pImportNode == NULL)
					{
						m_pLog->PrintLine(ckT("  Error: The file \"%s\" does not contain imported session data like advertised."),
							(*itFile)->m_FileName.c_str());
						return false;
					}

					(*itFile)->m_uiDataSizeNormal = pImportNode->m_ulExtentLength;
					(*itFile)->m_uiDataSizeJoliet = pImportNode->m_ulExtentLength;

					(*itFile)->m_uiDataPosNormal = pImportNode->m_ulExtentLocation;
					(*itFile)->m_uiDataPosJoliet = pImportNode->m_ulExtentLocation;
				}
				else
				{
					(*itFile)->m_uiDataSizeNormal = (*itFile)->m_uiFileSize;
					(*itFile)->m_uiDataSizeJoliet = (*itFile)->m_uiFileSize;

					(*itFile)->m_uiDataPosNormal = uiSecOffset;
					(*itFile)->m_uiDataPosJoliet = uiSecOffset;

					uiSecOffset += (*itFile)->m_uiDataSizeNormal/ISO9660_SECTOR_SIZE;
					if ((*itFile)->m_uiDataSizeNormal % ISO9660_SECTOR_SIZE != 0)
						uiSecOffset++;

					// Pad if necessary.
					uiSecOffset += (*itFile)->m_ulDataPadLen;
				}

				/*
				(*itFile)->m_uiDataSizeNormal = (*itFile)->m_uiFileSize;
				(*itFile)->m_uiDataSizeJoliet = (*itFile)->m_uiFileSize;

				(*itFile)->m_uiDataPosNormal = uiSecOffset;
				(*itFile)->m_uiDataPosJoliet = uiSecOffset;

				uiSecOffset += (*itFile)->m_uiDataSizeNormal/ISO9660_SECTOR_SIZE;
				if ((*itFile)->m_uiDataSizeNormal % ISO9660_SECTOR_SIZE != 0)
					uiSecOffset++;

				// Pad if necessary.
				uiSecOffset += (*itFile)->m_ulDataPadLen;*/
			}
		}

		return true;
	}

	/*
		Calculates file system specific data such as location of extents and sizes of
		extents.
	*/
	bool CDiscImageWriter::CalcFileSysData(CFileTree &FileTree,ckcore::Progress &Progress,
		ckcore::tuint64 uiStartSec,ckcore::tuint64 &uiLastSec)
	{
		CFileTreeNode *pCurNode = FileTree.GetRoot();
		ckcore::tuint64 uiSecOffset = uiStartSec;

		std::vector<std::pair<CFileTreeNode *,int> > DirNodeStack;
		if (!CalcLocalFileSysData(DirNodeStack,pCurNode,0,uiSecOffset,Progress))
			return false;

		while (DirNodeStack.size() > 0)
		{ 
			pCurNode = DirNodeStack[DirNodeStack.size() - 1].first;
			int iLevel = DirNodeStack[DirNodeStack.size() - 1].second;
			DirNodeStack.pop_back();

			if (!CalcLocalFileSysData(DirNodeStack,pCurNode,iLevel,uiSecOffset,Progress))
				return false;
		}

		uiLastSec = uiSecOffset;
		return true;
	}

	int CDiscImageWriter::WriteFileNode(CSectorOutStream &OutStream,CFileTreeNode *pNode,
										ckcore::Progresser &FileProgresser)
	{
		ckcore::FileInStream FileStream(pNode->m_FileFullPath.c_str());
		if (!FileStream.Open())
		{
			m_pLog->PrintLine(ckT("  Error: Unable to obtain file handle to \"%s\"."),
				pNode->m_FileFullPath.c_str());
			FileProgresser.Notify(ckcore::Progress::ckERROR,g_StringTable.GetString(ERROR_OPENREAD),
				pNode->m_FileFullPath.c_str());
			return RESULT_FAIL;
		}

		/*char szBuffer[DISCIMAGEWRITER_IO_BUFFER_SIZE];
		ckcore::tint64 iProcessed = 0;

		ckcore::tuint64 uiReadSize = 0;
		while (uiReadSize < pNode->m_uiFileSize)
		{
			// Check if we should abort.
			if (Progress.Cancelled())
				return RESULT_CANCEL;

			iProcessed = FileStream.Read(szBuffer,DISCIMAGEWRITER_IO_BUFFER_SIZE);
			if (iProcessed == -1)
			{
				m_pLog->PrintLine(ckT("  Error: Unable read file: %s."),pNode->m_FileFullPath.c_str());
				return RESULT_FAIL;
			}

			if (iProcessed == 0)
			{
				// We may have a problem. The file size may have changed since specied in file list.
				m_pLog->PrintLine(ckT("  Error: File size missmatch on \"%s\". Reported size %I64d bytes versus actual size %I64d bytes."),
					pNode->m_FileFullPath.c_str(),pNode->m_uiFileSize,uiReadSize);
				return RESULT_FAIL;
			}

			uiReadSize += iProcessed;

			// Check if we should abort.
			if (Progress.Cancelled())
				return RESULT_CANCEL;

			iProcessed = OutStream.Write(szBuffer,(ckcore::tuint32)iProcessed);
			if (iProcessed == -1)
			{
				m_pLog->PrintLine(ckT("  Error: Unable write to disc image."));
				return RESULT_FAIL;
			}

			Progress.SetProgress(FilesProgress.UpdateProcessed((unsigned long)iProcessed));
		}*/

		if (!ckcore::stream::copy(FileStream,OutStream,FileProgresser))
		{
			m_pLog->PrintLine(ckT("  Error: Unable write file to disc image."));
			return RESULT_FAIL;
		}

		// Pad the sector.
		if (OutStream.GetAllocated() != 0)
			OutStream.PadSector();

		return RESULT_OK;
	}

	int CDiscImageWriter::WriteLocalFileData(CSectorOutStream &OutStream,
		std::vector<std::pair<CFileTreeNode *,int> > &DirNodeStack,
		CFileTreeNode *pLocalNode,int iLevel,ckcore::Progresser &FileProgresser)
	{
		std::vector<CFileTreeNode *>::const_iterator itFile;
		for (itFile = pLocalNode->m_Children.begin(); itFile !=
			pLocalNode->m_Children.end(); itFile++)
		{
			// Check if we should abort.
			if (FileProgresser.Cancelled())
				return RESULT_CANCEL;

			if ((*itFile)->m_ucFileFlags & CFileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (iLevel >= m_Iso9660.GetMaxDirLevel())
					continue;
				else
					DirNodeStack.push_back(std::make_pair(*itFile,iLevel + 1));
			}
			else if (!((*itFile)->m_ucFileFlags & CFileTreeNode::FLAG_IMPORTED))	// We don't have any data to write for imported files.
			{
				// Validate file size.
				if (m_FileSystem == FS_ISO9660 || m_FileSystem == FS_ISO9660_JOLIET || m_FileSystem == FS_DVDVIDEO)
				{
					if ((*itFile)->m_uiFileSize > ISO9660_MAX_EXTENT_SIZE && !m_Iso9660.AllowsFragmentation())
						continue;
				}

				switch (WriteFileNode(OutStream,*itFile,FileProgresser))
				{
					case RESULT_FAIL:
						m_pLog->PrintLine(ckT("  Error: Unable to write node \"%s\" to (%I64d,%I64d)."),
							(*itFile)->m_FileName.c_str(),(*itFile)->m_uiDataPosNormal,(*itFile)->m_uiDataSizeNormal);
						return RESULT_FAIL;

					case RESULT_CANCEL:
						return RESULT_CANCEL;
				}

				// Pad if necessary.
				char szTemp[1] = { 0 };
				for (unsigned int i = 0; i < (*itFile)->m_ulDataPadLen; i++)
				{
					for (unsigned int j = 0; j < ISO9660_SECTOR_SIZE; j++)
						OutStream.Write(szTemp,1);
				}
			}
		}

		return RESULT_OK;
	}

	int CDiscImageWriter::WriteFileData(CSectorOutStream &OutStream,CFileTree &FileTree,
		ckcore::Progresser &FileProgresser)
	{
		CFileTreeNode *pCurNode = FileTree.GetRoot();

		std::vector<std::pair<CFileTreeNode *,int> > DirNodeStack;
		int iResult = WriteLocalFileData(OutStream,DirNodeStack,pCurNode,1,FileProgresser);
		if (iResult != RESULT_OK)
			return iResult;

		while (DirNodeStack.size() > 0)
		{ 
			pCurNode = DirNodeStack[DirNodeStack.size() - 1].first;
			int iLevel = DirNodeStack[DirNodeStack.size() - 1].second;
			DirNodeStack.pop_back();

			iResult = WriteLocalFileData(OutStream,DirNodeStack,pCurNode,iLevel,FileProgresser);
			if (iResult != RESULT_OK)
				return iResult;
		}

		return RESULT_OK;
	}

	void CDiscImageWriter::GetInternalPath(CFileTreeNode *pChildNode,ckcore::tstring &NodePath,
		bool bExternalPath,bool bJoliet)
	{
		NodePath = ckT("/");

		if (bExternalPath)
		{
			// Joliet or ISO9660?
			if (bJoliet)
			{
#ifdef UNICODE
				if (pChildNode->m_FileNameJoliet[pChildNode->m_FileNameJoliet.length() - 2] == ';')
					NodePath.append(pChildNode->m_FileNameJoliet,0,pChildNode->m_FileNameJoliet.length() - 2);
				else
					NodePath.append(pChildNode->m_FileNameJoliet);
#else
				char szAnsiName[JOLIET_MAX_NAMELEN_RELAXED + 1];
				ckcore::string::utf16_to_ansi(pChildNode->m_FileNameJoliet.c_str(),szAnsiName,sizeof(szAnsiName));

				if (szAnsiName[pChildNode->m_FileNameJoliet.length() - 2] == ';')
					szAnsiName[pChildNode->m_FileNameJoliet.length() - 2] = '\0';

				NodePath.append(szAnsiName);
#endif
			}
			else
			{
#ifdef UNICODE
				wchar_t szWideName[MAX_PATH];
				AnsiToUnicode(szWideName,pChildNode->m_FileNameIso9660.c_str(),sizeof(szWideName)/sizeof(wchar_t));

				if (szWideName[pChildNode->m_FileNameIso9660.length() - 2] == ';')
					szWideName[pChildNode->m_FileNameIso9660.length() - 2] = '\0';

				NodePath.append(szWideName);
#else
				if (pChildNode->m_FileNameIso9660[pChildNode->m_FileNameIso9660.length() - 2] == ';')
					NodePath.append(pChildNode->m_FileNameIso9660,0,pChildNode->m_FileNameIso9660.length() - 2);
				else
					NodePath.append(pChildNode->m_FileNameIso9660);
#endif
			}
		}
		else
		{
			NodePath.append(pChildNode->m_FileName);
		}

		CFileTreeNode *pCurNode = pChildNode->GetParent();
		while (pCurNode->GetParent() != NULL)
		{
			if (bExternalPath)
			{
				// Joliet or ISO9660?
				if (bJoliet)
				{
	#ifdef UNICODE
					if (pCurNode->m_FileNameJoliet[pCurNode->m_FileNameJoliet.length() - 2] == ';')
					{
						std::wstring::iterator itEnd = pCurNode->m_FileNameJoliet.end();
						itEnd--;
						itEnd--;

						NodePath.insert(NodePath.begin(),pCurNode->m_FileNameJoliet.begin(),itEnd);
					}
					else
					{
						NodePath.insert(NodePath.begin(),pCurNode->m_FileNameJoliet.begin(),
							pCurNode->m_FileNameJoliet.end());
					}
	#else
					char szAnsiName[JOLIET_MAX_NAMELEN_RELAXED + 1];
					ckcore::string::utf16_to_ansi(pCurNode->m_FileNameJoliet.c_str(),szAnsiName,sizeof(szAnsiName));

					if (szAnsiName[pCurNode->m_FileNameJoliet.length() - 2] == ';')
						szAnsiName[pCurNode->m_FileNameJoliet.length() - 2] = '\0';

					NodePath.insert(0,szAnsiName);
	#endif
				}
				else
				{
	#ifdef UNICODE
					wchar_t szWideName[MAX_PATH];
					AnsiToUnicode(szWideName,pCurNode->m_FileNameIso9660.c_str(),sizeof(szWideName)/sizeof(wchar_t));
					NodePath.insert(0,szWideName);

					if (szWideName[pCurNode->m_FileNameIso9660.length() - 2] == ';')
						szWideName[pCurNode->m_FileNameIso9660.length() - 2] = '\0';
	#else
					if (pCurNode->m_FileNameIso9660[pCurNode->m_FileNameIso9660.length() - 2] == ';')
					{
						std::string::iterator itEnd = pCurNode->m_FileNameIso9660.end();
						itEnd--;
						itEnd--;

						NodePath.insert(NodePath.begin(),pCurNode->m_FileNameIso9660.begin(),itEnd);
					}
					else
					{
						NodePath.insert(NodePath.begin(),pCurNode->m_FileNameIso9660.begin(),
							pCurNode->m_FileNameIso9660.end());
					}
	#endif
				}
			}
			else
			{
				NodePath.insert(NodePath.begin(),pCurNode->m_FileName.begin(),
					pCurNode->m_FileName.end());
			}

			NodePath.insert(0,ckT("/"));

			pCurNode = pCurNode->GetParent();
		}
	}

	void CDiscImageWriter::CreateLocalFilePathMap(CFileTreeNode *pLocalNode,
		std::vector<CFileTreeNode *> &DirNodeStack,
		std::map<ckcore::tstring,ckcore::tstring> &FilePathMap,bool bJoliet)
	{
		std::vector<CFileTreeNode *>::const_iterator itFile;
		for (itFile = pLocalNode->m_Children.begin(); itFile !=
			pLocalNode->m_Children.end(); itFile++)
		{
			if ((*itFile)->m_ucFileFlags & CFileTreeNode::FLAG_DIRECTORY)
			{
				DirNodeStack.push_back(*itFile);
			}
			else
			{
				// Yeah, this is not very efficient. Both paths should be calculated togather.
				ckcore::tstring FilePath;
				GetInternalPath(*itFile,FilePath,false,bJoliet);

				ckcore::tstring ExternalFilePath;
				GetInternalPath(*itFile,ExternalFilePath,true,bJoliet);

				FilePathMap[FilePath] = ExternalFilePath;

				//MessageBox(NULL,ExternalFilePath.c_str(),FilePath.c_str(),MB_OK);
			}
		}
	}

	/*
		Used for creating a map between the internal file names and the
		external (Joliet or ISO9660, in that order).
	*/
	void CDiscImageWriter::CreateFilePathMap(CFileTree &FileTree,
		std::map<ckcore::tstring,ckcore::tstring> &FilePathMap,bool bJoliet)
	{
		CFileTreeNode *pCurNode = FileTree.GetRoot();

		std::vector<CFileTreeNode *> DirNodeStack;
		CreateLocalFilePathMap(pCurNode,DirNodeStack,FilePathMap,bJoliet);

		while (DirNodeStack.size() > 0)
		{ 
			pCurNode = DirNodeStack.back();
			DirNodeStack.pop_back();

			CreateLocalFilePathMap(pCurNode,DirNodeStack,FilePathMap,bJoliet);
		}
	}

	/*
		ulSectorOffset is a space assumed to be allocated before this image,
		this is used for creating multi-session discs.
		pFileNameMap is optional, it should be specified if one wants to map the
		internal file paths to the actual external paths.
	*/
	int CDiscImageWriter::Create(CSectorOutStream &OutStream,CFileSet &Files,ckcore::Progress &Progress,
		unsigned long ulSectorOffset,std::map<ckcore::tstring,ckcore::tstring> *pFilePathMap)
	{
		m_pLog->PrintLine(ckT("CDiscImageWriter::Create"));
		m_pLog->PrintLine(ckT("  Sector offset: %u."),ulSectorOffset);

		// The first 16 sectors are reserved for system use (write 0s).
		char szTemp[1] = { 0 };

		for (unsigned int i = 0; i < ISO9660_SECTOR_SIZE << 4; i++)
			OutStream.Write(szTemp,1);

		// ...
		/*CFileSet::const_iterator itFile;
		for (itFile = Files.begin(); itFile != Files.end(); itFile++)
		{
			m_pLog->PrintLine(ckT("  %s"),(*itFile).m_InternalPath.c_str());
		}*/
		// ...

		Progress.SetStatus(g_StringTable.GetString(STATUS_BUILDTREE));
		Progress.SetMarquee(true);

		// Create a file tree.
		CFileTree FileTree(m_pLog);
		if (!FileTree.CreateFromFileSet(Files))
		{
			m_pLog->PrintLine(ckT("  Error: Failed to build file tree."));
			return Fail(RESULT_FAIL,OutStream);
		}

		// Calculate padding if DVD-Video file system.
		if (m_FileSystem == FS_DVDVIDEO)
		{
			CDvdVideo DvdVideo(m_pLog);
			if (!DvdVideo.CalcFilePadding(FileTree))
			{
				m_pLog->PrintLine(ckT("  Error: Failed to calculate file padding for DVD-Video file system."));
				return Fail(RESULT_FAIL,OutStream);
			}

			DvdVideo.PrintFilePadding(FileTree);
		}

		bool bUseIso = m_FileSystem != FS_UDF;
		bool bUseUdf = m_FileSystem == FS_ISO9660_UDF || m_FileSystem == FS_ISO9660_UDF_JOLIET ||
			m_FileSystem == FS_UDF || m_FileSystem == FS_DVDVIDEO;
		bool bUseJoliet = m_FileSystem == FS_ISO9660_JOLIET || m_FileSystem == FS_ISO9660_UDF_JOLIET;

		CSectorManager SectorManager(16 + ulSectorOffset);
		CIso9660Writer IsoWriter(m_pLog,&OutStream,&SectorManager,&m_Iso9660,&m_Joliet,&m_ElTorito,true,bUseJoliet);
		CUdfWriter UdfWriter(m_pLog,&OutStream,&SectorManager,&m_Udf,true);

		int iResult = RESULT_FAIL;

		// FIXME: Put failure messages to Progress.
		if (bUseIso)
		{
			iResult = IsoWriter.AllocateHeader();
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		if (bUseUdf)
		{
			iResult = UdfWriter.AllocateHeader();
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		if (bUseIso)
		{
			iResult = IsoWriter.AllocatePathTables(Progress,Files);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);

			iResult = IsoWriter.AllocateDirEntries(FileTree,Progress);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		if (bUseUdf)
		{
			iResult = UdfWriter.AllocatePartition(FileTree);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		// Allocate file data.
		ckcore::tuint64 uiFirstDataSec = SectorManager.GetNextFree();
		ckcore::tuint64 uiLastDataSec = 0;

		if (!CalcFileSysData(FileTree,Progress,uiFirstDataSec,uiLastDataSec))
		{
			m_pLog->PrintLine(ckT("  Error: Could not calculate necessary file system information."));
			return Fail(RESULT_FAIL,OutStream);
		}

		SectorManager.AllocateDataSectors(uiLastDataSec - uiFirstDataSec);

		if (bUseIso)
		{
			iResult = IsoWriter.WriteHeader(Files,FileTree,Progress);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		if (bUseUdf)
		{
			iResult = UdfWriter.WriteHeader();
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		// FIXME: Add progress for this.
		if (bUseIso)
		{
			iResult = IsoWriter.WritePathTables(Files,FileTree,Progress);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);

			iResult = IsoWriter.WriteDirEntries(FileTree,Progress);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		if (bUseUdf)
		{
			iResult = UdfWriter.WritePartition(FileTree);
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

		Progress.SetStatus(g_StringTable.GetString(STATUS_WRITEDATA));
		Progress.SetMarquee(false);

		// To help keep track of the progress.
		ckcore::Progresser FileProgresser(Progress,SectorManager.GetDataLength() * ISO9660_SECTOR_SIZE);
		iResult = WriteFileData(OutStream,FileTree,FileProgresser);
		if (iResult != RESULT_OK)
			return Fail(iResult,OutStream);

		if (bUseUdf)
		{
			iResult = UdfWriter.WriteTail();
			if (iResult != RESULT_OK)
				return Fail(iResult,OutStream);
		}

#ifdef _DEBUG
		FileTree.PrintTree();
#endif

		// Map the paths if requested.
		if (pFilePathMap != NULL)
			CreateFilePathMap(FileTree,*pFilePathMap,bUseJoliet);

		OutStream.Flush();
		return RESULT_OK;
	}

	/*
		Should be called when create operation fails or cancel so that the
		broken image can be removed and the file handle closed.
	*/
	int CDiscImageWriter::Fail(int iResult,CSectorOutStream &OutStream)
	{
		OutStream.Flush();
		return iResult;
	}

	void CDiscImageWriter::SetVolumeLabel(const ckcore::tchar *szLabel)
	{
		m_Iso9660.SetVolumeLabel(szLabel);
		m_Joliet.SetVolumeLabel(szLabel);
		m_Udf.SetVolumeLabel(szLabel);
	}

	void CDiscImageWriter::SetTextFields(const ckcore::tchar *szSystem,const ckcore::tchar *szVolSetIdent,
										 const ckcore::tchar *szPublIdent,const ckcore::tchar *szPrepIdent)
	{
		m_Iso9660.SetTextFields(szSystem,szVolSetIdent,szPublIdent,szPrepIdent);
		m_Joliet.SetTextFields(szSystem,szVolSetIdent,szPublIdent,szPrepIdent);
	}

	void CDiscImageWriter::SetFileFields(const ckcore::tchar *szCopyFileIdent,
										 const ckcore::tchar *szAbstFileIdent,
										 const ckcore::tchar *szBiblFileIdent)
	{
		m_Iso9660.SetFileFields(szCopyFileIdent,szAbstFileIdent,szBiblFileIdent);
		m_Joliet.SetFileFields(szCopyFileIdent,szAbstFileIdent,szBiblFileIdent);
	}

	void CDiscImageWriter::SetInterchangeLevel(CIso9660::eInterLevel InterLevel)
	{
		m_Iso9660.SetInterchangeLevel(InterLevel);
	}

	void CDiscImageWriter::SetIncludeFileVerInfo(bool bIncludeInfo)
	{
		m_Iso9660.SetIncludeFileVerInfo(bIncludeInfo);
		m_Joliet.SetIncludeFileVerInfo(bIncludeInfo);
	}

	void CDiscImageWriter::SetPartAccessType(CUdf::ePartAccessType AccessType)
	{
		m_Udf.SetPartAccessType(AccessType);
	}

	void CDiscImageWriter::SetRelaxMaxDirLevel(bool bRelaxRestriction)
	{
		m_Iso9660.SetRelaxMaxDirLevel(bRelaxRestriction);
	}

	void CDiscImageWriter::SetLongJolietNames(bool bEnable)
	{
		m_Joliet.SetRelaxMaxNameLen(bEnable);
	}

	bool CDiscImageWriter::AddBootImageNoEmu(const ckcore::tchar *szFullPath,bool bBootable,
		unsigned short usLoadSegment,unsigned short usSectorCount)
	{
		return m_ElTorito.AddBootImageNoEmu(szFullPath,bBootable,usLoadSegment,usSectorCount);
	}

	bool CDiscImageWriter::AddBootImageFloppy(const ckcore::tchar *szFullPath,bool bBootable)
	{
		return m_ElTorito.AddBootImageFloppy(szFullPath,bBootable);
	}

	bool CDiscImageWriter::AddBootImageHardDisk(const ckcore::tchar *szFullPath,bool bBootable)
	{
		return m_ElTorito.AddBootImageHardDisk(szFullPath,bBootable);
	}
};
