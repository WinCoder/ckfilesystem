/*
 * The ckFileSystem library provides file system functionality.
 * Copyright (C) 2006-2008 Christian Kindahl
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <ckcore/types.hh>
#include "ckfilesystem/discimagewriter.hh"
#include "ckfilesystem/iso9660.hh"
#include "ckfilesystem/joliet.hh"

namespace ckfilesystem
{
	class DiscImageHelper
	{
	private:
		DiscImageWriter::eFileSystem m_FileSystem;

		Joliet m_Joliet;
		Iso9660 m_Iso9660;

	public:
		DiscImageHelper(DiscImageWriter::eFileSystem FileSystem,
			bool bIncludeInfo,bool bLongJolietNames,Iso9660::InterLevel inter_level);
		~DiscImageHelper();

		void CalcFileName(const ckcore::tchar *szReqFileName,ckcore::tchar *szFileName,bool bIsDir);
		void CalcFilePath(const ckcore::tchar *szReqFilePath,ckcore::tstring &FilePath);
	};
};
