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
#include <ckcore/stream.hh>
#include <ckcore/bufferedstream.hh>
#include "ckfilesystem/iso9660.hh"

namespace ckfilesystem
{
	class SectorOutStream : public ckcore::BufferedOutStream
	{
	private:
		unsigned long sector_size_;
		ckcore::tuint64 sector_;
		ckcore::tuint64 written_;

	public:
		SectorOutStream(ckcore::OutStream &out_stream,
			unsigned long sector_size = ISO9660_SECTOR_SIZE);
		~SectorOutStream();

		ckcore::tint64 write(void *buffer,ckcore::tuint32 count);

		ckcore::tuint64 get_sector();
		unsigned long get_allocated();
		unsigned long get_remaining();

		void pad_sector();
	};
};
