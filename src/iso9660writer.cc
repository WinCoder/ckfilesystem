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

#include <time.h>
#include <string.h>
#include <wctype.h>
#include <ckcore/file.hh>
#include <ckcore/directory.hh>
#include <ckcore/convert.hh>
#include "ckfilesystem/stringtable.hh"
#include "ckfilesystem/iso9660reader.hh"
#include "ckfilesystem/iso9660writer.hh"

namespace ckfilesystem
{
	Iso9660Writer::Iso9660Writer(ckcore::Log &log,SectorOutStream &out_stream,SectorManager &sec_manager,
								 Iso9660 &iso9660,Joliet &joliet,ElTorito &eltorito,
								 bool use_file_times,bool use_joliet) :
		log_(log),out_stream_(out_stream),sec_manager_(sec_manager),
		iso9660_(iso9660),joliet_(joliet),eltorito_(eltorito),
		use_file_times_(use_file_times),use_joliet_(use_joliet),
		pathtable_size_normal_(0),pathtable_size_joliet_(0)
	{
		// Get system time.
        time_t cur_time;
        time(&cur_time);

        create_time_ = *localtime(&cur_time);
	}

	Iso9660Writer::~Iso9660Writer()
	{
	}

	/**
		Tries to assure that the specified file name is unqiue. Only 255 collisions
		are supported. If any more collisions occur duplicate file names will be
		written to the file system.
	*/
	void Iso9660Writer::MakeUniqueJoliet(FileTreeNode *node,unsigned char *file_name_ptr,
										 unsigned char file_name_size)
	{
		FileTreeNode *parent_node = node->GetParent();
		if (parent_node == NULL)
			return;

		// Don't calculate a new name of one has already been generated.
		if (node->file_name_joliet_ != L"")
		{
			unsigned char file_name_pos = 0;
			for (unsigned int i = 0; i < file_name_size; i += 2,file_name_pos++)
			{
				file_name_ptr[i    ] = node->file_name_joliet_[file_name_pos] >> 8;
				file_name_ptr[i + 1] = node->file_name_joliet_[file_name_pos] & 0xFF;
			}

			return;
		}

		unsigned char file_name_len = file_name_size >> 1;
		unsigned char file_name_pos = 0;

		wchar_t file_name[(ISO9660WRITER_FILENAME_BUFFER_SIZE >> 1) + 1];
		for (unsigned int i = 0; i < file_name_len; i++)
		{
			file_name[i]  = file_name_ptr[file_name_pos++] << 8;
			file_name[i] |= file_name_ptr[file_name_pos++];
		}

		file_name[file_name_len] = '\0';

		// We're only interested in the file name without the extension and
		// version information.
		int delim = -1;
		for (unsigned int i = 0; i < file_name_len; i++)
		{
			if (file_name[i] == '.')
				delim = i;
		}

		unsigned char file_name_end;

		// If no '.' character was found just remove the version information if
		// we're dealing with a file.
		if (delim == -1)
		{
			if (!(node->file_flags_ & FileTreeNode::FLAG_DIRECTORY) &&
				joliet_.IncludesFileVerInfo())
				file_name_end = file_name_len - 2;
			else
				file_name_end = file_name_len;
		}
		else
		{
			file_name_end = delim;
		}

		// We can't handle file names shorted than 3 characters (255 limit).
		if (file_name_end <= 3)
		{
			node->file_name_joliet_ = file_name;

			// Copy back to original buffer.
			file_name_pos = 0;
			for (unsigned int i = 0; i < file_name_size; i += 2,file_name_pos++)
			{
				file_name_ptr[i    ] = file_name[file_name_pos] >> 8;
				file_name_ptr[i + 1] = file_name[file_name_pos] & 0xFF;
			}

			return;
		}

		unsigned char next_number = 1;
		wchar_t next_number_str[4];

		std::vector<FileTreeNode *>::const_iterator it_sibling;
		for (it_sibling = parent_node->children_.begin(); it_sibling != parent_node->children_.end();)
		{
			// Ignore any siblings that has not yet been assigned an ISO9660 compatible file name.
			if ((*it_sibling)->file_name_iso9660_ == "")
			{
				it_sibling++;
				continue;
			}

			if (!wcscmp((*it_sibling)->file_name_joliet_.c_str(),file_name))
			{
				swprintf(next_number_str,4,L"%d",next_number);

				// Using if-statements for optimization.
				if (next_number < 10)
					file_name[file_name_end - 1] = next_number_str[0];
				else if (next_number < 100)
					memcpy(file_name + file_name_end - 2,next_number_str,2 * sizeof(wchar_t));
				else
					memcpy(file_name + file_name_end - 3,next_number_str,3 * sizeof(wchar_t));

				if (next_number == 255)
				{
					// We have failed, files with duplicate names will exist.
					log_.PrintLine(ckT("  Warning: Unable to calculate unique Joliet name for %s. Duplicate file names will exist in Joliet name extension."),
						node->file_path_.c_str());
					break;
				}

				// Start from the beginning.
				next_number++;
				it_sibling = parent_node->children_.begin();
				continue;
			}

			it_sibling++;
		}

		node->file_name_joliet_ = file_name;

		// Copy back to original buffer.
		file_name_pos = 0;
		for (unsigned int i = 0; i < file_name_size; i += 2,file_name_pos++)
		{
			file_name_ptr[i    ] = file_name[file_name_pos] >> 8;
			file_name_ptr[i + 1] = file_name[file_name_pos] & 0xFF;
		}
	}

	/**
		Tries to assure that the specified file name is unqiue. Only 255 collisions
		are supported. If any more collisions occur duplicate file names will be
		written to the file system.
	*/
	void Iso9660Writer::MakeUniqueIso9660(FileTreeNode *node,unsigned char *file_name_ptr,
										  unsigned char file_name_size)
	{
		FileTreeNode *parent_node = node->GetParent();
		if (parent_node == NULL)
			return;

		// Don't calculate a new name of one has already been generated.
		if (node->file_name_iso9660_ != "")
		{
			memcpy(file_name_ptr,node->file_name_iso9660_.c_str(),file_name_size);
			return;
		}

		// We're only interested in the file name without the extension and
		// version information.
		int delim = -1;
		for (unsigned int i = 0; i < file_name_size; i++)
		{
			if (file_name_ptr[i] == '.')
				delim = i;
		}

		unsigned char file_name_end;

		// If no '.' character was found just remove the version information if
		// we're dealing with a file.
		if (delim == -1)
		{
			if (!(node->file_flags_ & FileTreeNode::FLAG_DIRECTORY) &&
				iso9660_.IncludesFileVerInfo())
				file_name_end = file_name_size - 2;
			else
				file_name_end = file_name_size;
		}
		else
		{
			file_name_end = delim;
		}

		// We can't handle file names shorted than 3 characters (255 limit).
		if (file_name_end <= 3)
		{
			char file_name[ISO9660WRITER_FILENAME_BUFFER_SIZE + 1];
			memcpy(file_name,file_name_ptr,file_name_size);
			file_name[file_name_size] = '\0';

			node->file_name_iso9660_ = file_name;

			// Copy back to original buffer.
			memcpy(file_name_ptr,file_name,file_name_size);
			return;
		}

		unsigned char next_number = 1;
		char next_number_str[4];

		std::vector<FileTreeNode *>::const_iterator it_sibling;
		for (it_sibling = parent_node->children_.begin(); it_sibling != parent_node->children_.end();)
		{
			// Ignore any siblings that has not yet been assigned an ISO9660 compatible file name.
			if ((*it_sibling)->file_name_iso9660_ == "")
			{
				it_sibling++;
				continue;
			}

			if (!memcmp((*it_sibling)->file_name_iso9660_.c_str(),file_name_ptr,file_name_end))
			{
				sprintf(next_number_str,"%d",next_number);

				// Using if-statements for optimization.
				if (next_number < 10)
					file_name_ptr[file_name_end - 1] = next_number_str[0];
				else if (next_number < 100)
					memcpy(file_name_ptr + file_name_end - 2,next_number_str,2);
				else
					memcpy(file_name_ptr + file_name_end - 3,next_number_str,3);

				if (next_number == 255)
				{
					// We have failed, files with duplicate names will exist.
					log_.PrintLine(ckT("  Warning: Unable to calculate unique ISO9660 name for %s. Duplicate file names will exist in ISO9660 file system."),
						node->file_path_.c_str());
					break;
				}

				// Start from the beginning.
				next_number++;
				it_sibling = parent_node->children_.begin();
				continue;
			}

			it_sibling++;
		}

		char file_name[ISO9660WRITER_FILENAME_BUFFER_SIZE + 1];
		memcpy(file_name,file_name_ptr,file_name_size);
		file_name[file_name_size] = '\0';

		node->file_name_iso9660_ = file_name;

		// Copy back to original buffer.
		memcpy(file_name_ptr,file_name,file_name_size);
	}

	bool Iso9660Writer::CompareStrings(const char *str1,const ckcore::tchar *str2,
									   unsigned char len)
	{
#ifdef _WINDOWS
#ifdef _UNICODE
		for (unsigned char i = 0; i < len; i++)
			if (toupper(str1[i]) != toupper(str2[i]))
				return false;

		return true;
#else
		return !_strnicmp(str1,str2,len);
#endif
#else   
        return !strncasecmp(str1,str2,len);
#endif
	}

	bool Iso9660Writer::CompareStrings(const unsigned char *udf_str1,
									   const ckcore::tchar *str2,
									   unsigned char len)
	{
#ifdef _UNICODE
		unsigned char file_name_pos = 0;
		for (unsigned int i = 0; i < len; i++)
		{
			wchar_t c = udf_str1[file_name_pos++] << 8;
			c |= udf_str1[file_name_pos++];

			if (towupper(c) != towupper(str2[i]))
				return false;
		}

		return true;
#else
		unsigned char file_name_pos = 0;
		for (unsigned int i = 0; i < len; i++)
		{
			wchar_t c = udf_str1[file_name_pos++] << 8;
			c |= udf_str1[file_name_pos++];

			if (towupper(c) != toupper(str2[i]))
				return false;
		}
		return true;
#endif
	}

	bool Iso9660Writer::CalcPathTableSize(FileSet &files,bool joliet_table,
										  ckcore::tuint64 &pathtable_size,
										  ckcore::Progress &progress)
	{
		// Root record + 1 padding byte since the root record size is odd.
		pathtable_size = sizeof(tiso_pathtable_record) + 1;

		// Write all other path table records.
		std::set<ckcore::tstring> path_dir_list;		// To help keep track of which records that have already been counted.
		ckcore::tstring path_buffer,cur_dir_name,internal_path;

		// Set to true of we have found that the directory structure is to deep.
		// This variable is needed so that the warning message will only be printed
		// once.
		bool found_deep = false;

		FileSet::const_iterator it_file;
		for (it_file = files.begin(); it_file != files.end(); it_file++)
		{
			// We're do not have to add the root record once again.
			int level = FileComparator::Level(*it_file);
			if (level <= 1)
				continue;

			if (level > iso9660_.GetMaxDirLevel())
			{
				// Print the message only once.
				if (!found_deep)
				{
					log_.PrintLine(ckT("  Warning: The directory structure is deeper than %d levels. Deep files and folders will be ignored."),
								   iso9660_.GetMaxDirLevel());
					progress.Notify(ckcore::Progress::ckWARNING,
									StringTable::Instance().GetString(StringTable::WARNING_FSDIRLEVEL),
									iso9660_.GetMaxDirLevel());
					found_deep = true;
				}

				log_.PrintLine(ckT("  Skipping: %s."),it_file->internal_path_.c_str());
				progress.Notify(ckcore::Progress::ckWARNING,
								StringTable::Instance().GetString(StringTable::WARNING_SKIPFILE),
								it_file->internal_path_.c_str());
				continue;
			}

			// We're only interested in directories.
			if (!(it_file->flags_ & FileDescriptor::FLAG_DIRECTORY))
				continue;

			// Make sure that the path to the current file or folder exists.
			size_t prev_delim = 0;

			// If the file is a directory, append a slash so it will be parsed as a
			// directory in the loop below.
			internal_path = it_file->internal_path_;
			internal_path.push_back('/');

			path_buffer.erase();
			path_buffer = ckT("/");

			for (size_t i = 0; i < internal_path.length(); i++)
			{
				if (internal_path[i] == '/')
				{
					if (i > (prev_delim + 1))
					{
						// Obtain the name of the current directory.
						cur_dir_name.erase();
						for (size_t j = prev_delim + 1; j < i; j++)
							cur_dir_name.push_back(internal_path[j]);

						path_buffer += cur_dir_name;
						path_buffer += ckT("/");

						// The path does not exist, create it.
						if (path_dir_list.find(path_buffer) == path_dir_list.end())
						{
							unsigned char name_len = joliet_table ?
								joliet_.CalcFileNameLen(cur_dir_name.c_str(),true) << 1 : 
								iso9660_.CalcFileNameLen(cur_dir_name.c_str(),true);
							unsigned char pathtable_rec_len = sizeof(tiso_pathtable_record) + name_len - 1;

							// If the record length is not even padd it with a 0 byte.
							if (pathtable_rec_len % 2 == 1)
								pathtable_rec_len++;

							path_dir_list.insert(path_buffer);

							// Update the path table length.
							pathtable_size += pathtable_rec_len;
						}
					}

					prev_delim = i;
				}
			}
		}

		return true;
	}

	/*
		Calculates the size of a directory entry in sectors. This size does not include the size
		of the extend data of the directory contents.
	*/
	bool Iso9660Writer::CalcLocalDirEntryLength(FileTreeNode *local_node,bool joliet,
												int level,unsigned long &dir_len)
	{
		dir_len = 0;

		// The number of bytes of data in the current sector.
		// Each directory always includes '.' and '..'.
		unsigned long dir_sec_data = sizeof(tiso_dir_record) << 1;

		std::vector<FileTreeNode *>::const_iterator it_file;
		for (it_file = local_node->children_.begin(); it_file !=
			local_node->children_.end(); it_file++)
		{
			if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (level >= iso9660_.GetMaxDirLevel())
					continue;
			}

			// Validate file size.
			if ((*it_file)->file_size_ > ISO9660_MAX_EXTENT_SIZE && !iso9660_.AllowsFragmentation())
				continue;

			// Calculate the number of times this record will be written. It
			// will be larger than one when using multi-extent.
			unsigned long factor = 1;
			ckcore::tuint64 extent_remain = (*it_file)->file_size_;
			while (extent_remain > ISO9660_MAX_EXTENT_SIZE)
			{
				extent_remain -= ISO9660_MAX_EXTENT_SIZE;
				factor++;
			}

			bool is_folder = (*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY;

			unsigned char name_len;	// FIXME: Rename to Size?
			
			// Optimization: If the the compatible file name is equal (not case-sensitive) to the
			// original file name we assume that it is uniqe.
			unsigned char file_name[ISO9660WRITER_FILENAME_BUFFER_SIZE + 4]; // Large enough for level 1, 2 and even Joliet.
			if (joliet)
			{
				name_len = joliet_.WriteFileName(file_name,(*it_file)->file_name_.c_str(),is_folder) << 1;

				unsigned char file_name_end;
				if (!is_folder && joliet_.IncludesFileVerInfo())
					file_name_end = (name_len >> 1) - 2;
				else
					file_name_end = (name_len >> 1);

				if (CompareStrings(file_name,(*it_file)->file_name_.c_str(),file_name_end))
				{
#ifdef _UNICODE
					(*it_file)->file_name_joliet_ = (*it_file)->file_name_;

					if (!is_folder && joliet_.IncludesFileVerInfo())
						(*it_file)->file_name_joliet_.append(L";1");
#else
					wchar_t szWideFileName[(ISO9660WRITER_FILENAME_BUFFER_SIZE >> 1) + 1];
					unsigned char file_name_pos = 0;
					for (unsigned char i = 0; i < (name_len >> 1); i++)
					{
						szWideFileName[i]  = file_name[file_name_pos++] << 8;
						szWideFileName[i] |= file_name[file_name_pos++];
					}

					szWideFileName[name_len >> 1] = '\0';

					(*it_file)->file_name_joliet_ = szWideFileName;
#endif
				}
			}
			else
			{
				name_len = iso9660_.WriteFileName(file_name,(*it_file)->file_name_.c_str(),is_folder);

				unsigned char file_name_end;
				if (!is_folder && iso9660_.IncludesFileVerInfo())
					file_name_end = name_len - 2;
				else
					file_name_end = name_len;

				if (CompareStrings((const char *)file_name,(*it_file)->file_name_.c_str(),file_name_end))
				{
					file_name[name_len] = '\0';
					(*it_file)->file_name_iso9660_ = (const char *)file_name;
				}
			}

			unsigned char cur_rec_size = sizeof(tiso_dir_record) + name_len - 1;

			// If the record length is not even padd it with a 0 byte.
			if (cur_rec_size % 2 == 1)
				cur_rec_size++;

			if (dir_sec_data + (cur_rec_size * factor) > ISO9660_SECTOR_SIZE)
			{
				dir_sec_data = cur_rec_size * factor;
				dir_len++;
			}
			else
			{
				dir_sec_data += cur_rec_size * factor;
			}
		}

		if (dir_sec_data != 0)
			dir_len++;

		return true;
	}

	bool Iso9660Writer::CalcLocalDirEntriesLength(std::vector<std::pair<FileTreeNode *,int> > &dir_node_stack,
												  FileTreeNode *local_node,int level,
												  ckcore::tuint64 &sec_offset)
	{
		unsigned long dir_len_normal = 0;
		if (!CalcLocalDirEntryLength(local_node,false,level,dir_len_normal))
			return false;

		unsigned long dir_len_joliet = 0;
		if (use_joliet_ && !CalcLocalDirEntryLength(local_node,true,level,dir_len_joliet))
			return false;

		std::vector<FileTreeNode *>::const_iterator it_file;
		for (it_file = local_node->children_.begin(); it_file !=
			local_node->children_.end(); it_file++)
		{
			if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (level >= iso9660_.GetMaxDirLevel())
					continue;
				else
					dir_node_stack.push_back(std::make_pair(*it_file,level + 1));
			}
		}

		// We now know how much space is needed to store the current directory record.
		local_node->data_size_normal_ = dir_len_normal * ISO9660_SECTOR_SIZE;
		local_node->data_size_joliet_ = dir_len_joliet * ISO9660_SECTOR_SIZE;

		local_node->data_pos_normal_ = sec_offset;
		sec_offset += dir_len_normal;
		local_node->data_pos_joliet_ = sec_offset;
		sec_offset += dir_len_joliet;

		return true;
	}

	bool Iso9660Writer::CalcDirEntriesLength(FileTree &file_tree,
											 ckcore::tuint64 start_sec,
											 ckcore::tuint64 &len)
	{
		FileTreeNode *cur_node = file_tree.GetRoot();
		ckcore::tuint64 sec_offset = start_sec;

		std::vector<std::pair<FileTreeNode *,int> > dir_node_stack;
		if (!CalcLocalDirEntriesLength(dir_node_stack,cur_node,0,sec_offset))
			return false;

		while (dir_node_stack.size() > 0)
		{ 
			cur_node = dir_node_stack[dir_node_stack.size() - 1].first;
			int level = dir_node_stack[dir_node_stack.size() - 1].second;
			dir_node_stack.pop_back();

			if (!CalcLocalDirEntriesLength(dir_node_stack,cur_node,level,sec_offset))
				return false;
		}

		len = sec_offset - start_sec;
		return true;
	}

	bool Iso9660Writer::WritePathTable(FileSet &files,FileTree &file_tree,
									   bool joliet_table,bool msbf)
	{
		FileTreeNode *cur_node = file_tree.GetRoot();

		tiso_pathtable_record root_record,path_record;
		memset(&root_record,0,sizeof(root_record));

		root_record.dir_ident_len = 1;					// Always 1 for the root record.
		root_record.ext_attr_record_len = 0;

		if (joliet_table)
			write73(root_record.extent_loc,(unsigned long)cur_node->data_pos_joliet_,msbf);	// Start sector of extent data.
		else
			write73(root_record.extent_loc,(unsigned long)cur_node->data_pos_normal_,msbf);	// Start sector of extent data.

		write72(root_record.parent_dir_num,0x01,msbf);	// The root has itself as parent.
		root_record.dir_ident[0] = 0;					// The file name is set to zero.

		// Write the root record.
		ckcore::tint64 processed = out_stream_.Write(&root_record,sizeof(root_record));
		if (processed == -1)
			return false;
		if (processed != sizeof(root_record))
			return false;

		// We need to pad the root record since it's size is otherwise odd.
		processed = out_stream_.Write(root_record.dir_ident,1);
		if (processed == -1)
			return false;
		if (processed != 1)
			return false;

		// Write all other path table records.
		std::map<ckcore::tstring,unsigned short> pathdir_num_map;
		ckcore::tstring path_buffer,cur_dir_name,internal_path;

		// Counters for all records.
		unsigned short rec_num = 2;	// Root has number 1.

		FileSet::const_iterator it_file;
		for (it_file = files.begin(); it_file != files.end(); it_file++)
		{
			// We're do not have to add the root record once again.
			int level = FileComparator::Level(*it_file);
			if (level <= 1)
				continue;

			if (level > iso9660_.GetMaxDirLevel())
				continue;

			// We're only interested in directories.
			if (!(it_file->flags_ & FileDescriptor::FLAG_DIRECTORY))
				continue;

			// Locate the node in the file tree.
			cur_node = file_tree.GetNodeFromPath(*it_file);
			if (cur_node == NULL)
				return false;

			// Make sure that the path to the current file or folder exists.
			size_t prev_delim = 0;

			// If the file is a directory, append a slash so it will be parsed as a
			// directory in the loop below.
			internal_path = it_file->internal_path_;
			internal_path.push_back('/');

			path_buffer.erase();
			path_buffer = ckT("/");

			unsigned short usParent = 1;	// Start from root.

			for (size_t i = 0; i < internal_path.length(); i++)
			{
				if (internal_path[i] == '/')
				{
					if (i > (prev_delim + 1))
					{
						// Obtain the name of the current directory.
						cur_dir_name.erase();
						for (size_t j = prev_delim + 1; j < i; j++)
							cur_dir_name.push_back(internal_path[j]);

						path_buffer += cur_dir_name;
						path_buffer += ckT("/");

						// The path does not exist, create it.
						if (pathdir_num_map.find(path_buffer) == pathdir_num_map.end())
						{
							memset(&path_record,0,sizeof(path_record));

							unsigned char name_len;
							unsigned char file_name[ISO9660WRITER_FILENAME_BUFFER_SIZE];	// Large enough for both level 1, 2 and even Joliet.
							if (joliet_table)
							{
								name_len = joliet_.WriteFileName(file_name,cur_dir_name.c_str(),true) << 1;
								MakeUniqueJoliet(cur_node,file_name,name_len);
							}
							else
							{
								name_len = iso9660_.WriteFileName(file_name,cur_dir_name.c_str(),true);
								MakeUniqueIso9660(cur_node,file_name,name_len);
							}

							// If the record length is not even padd it with a 0 byte.
							bool pad_byte = false;
							if (name_len % 2 == 1)
								pad_byte = true;

							path_record.dir_ident_len = name_len;
							path_record.ext_attr_record_len = 0;

							if (joliet_table)
								write73(path_record.extent_loc,(unsigned long)cur_node->data_pos_joliet_,msbf);
							else
								write73(path_record.extent_loc,(unsigned long)cur_node->data_pos_normal_,msbf);

							write72(path_record.parent_dir_num,usParent,msbf);
							path_record.dir_ident[0] = 0;

							pathdir_num_map[path_buffer] = usParent = rec_num++;

							// Write the record.
							processed = out_stream_.Write(&path_record,sizeof(path_record) - 1);
							if (processed == -1)
								return false;
							if (processed != sizeof(path_record) - 1)
								return false;

							processed = out_stream_.Write(file_name,name_len);
							if (processed == -1)
								return false;
							if (processed != name_len)
								return false;

							// Pad if necessary.
							if (pad_byte)
							{
								char szTemp[1] = { 0 };
								processed = out_stream_.Write(szTemp,1);
								if (processed == -1)
									return false;
								if (processed != 1)
									return false;
							}
						}
						else
						{
							usParent = pathdir_num_map[path_buffer];
						}
					}

					prev_delim = i;
				}
			}
		}

		if (out_stream_.GetAllocated() != 0)
			out_stream_.PadSector();

		return true;
	}

	bool Iso9660Writer::WriteSysDirectory(FileTreeNode *parent_node,SysDirType type,
										  unsigned long data_pos,unsigned long data_size)
	{
		tiso_dir_record dr;
		memset(&dr,0,sizeof(dr));

		dr.dir_record_len = 0x22;
		write733(dr.extent_loc,data_pos);
		write733(dr.data_len,data_size);
		iso_make_datetime(create_time_,dr.rec_timestamp);
		dr.file_flags = DIRRECORD_FILEFLAG_DIRECTORY;
		write723(dr.volseq_num,0x01);	// The directory is on the first volume set.
		dr.file_ident_len = 1;
		dr.file_ident[0] = type == TYPE_CURRENT ? 0 : 1;

		ckcore::tint64 processed = out_stream_.Write(&dr,sizeof(dr));
		if (processed == -1)
			return false;
		if (processed != sizeof(dr))
			return false;

		return true;
	}

	bool Iso9660Writer::ValidateTreeNode(std::vector<std::pair<FileTreeNode *,int> > &dir_node_stack,
										 FileTreeNode *node,int level)
	{
		std::vector<FileTreeNode *>::const_iterator it_file;
		for (it_file = node->children_.begin(); it_file !=
			node->children_.end(); it_file++)
		{
			if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
			{
				if (level >= iso9660_.GetMaxDirLevel())
					return false;
				else
					dir_node_stack.push_back(std::make_pair(*it_file,level + 1));
			}
		}

		return true;
	}

	/*
		Returns true if the specified file tree conforms to the configuration
		used to create the disc image.
	*/
	bool Iso9660Writer::ValidateTree(FileTree &file_tree)
	{
		FileTreeNode *cur_node = file_tree.GetRoot();

		std::vector<std::pair<FileTreeNode *,int> > dir_node_stack;
		if (!ValidateTreeNode(dir_node_stack,cur_node,1))
			return false;

		while (dir_node_stack.size() > 0)
		{ 
			cur_node = dir_node_stack[dir_node_stack.size() - 1].first;
			int level = dir_node_stack[dir_node_stack.size() - 1].second;
			dir_node_stack.pop_back();

			if (!ValidateTreeNode(dir_node_stack,cur_node,level))
				return false;
		}

		return true;
	}

	int Iso9660Writer::AllocateHeader()
	{
		// Allocate volume descriptor.
		unsigned long voldesc_size = sizeof(tiso_voldesc_primary) + sizeof(tiso_voldesc_setterm);
		if (eltorito_.GetBootImageCount() > 0)
			voldesc_size += sizeof(tiso_voldesc_eltorito_record);
		if (iso9660_.HasVolDescSuppl())
			voldesc_size += sizeof(tiso_voldesc_suppl);
		if (use_joliet_)
			voldesc_size += sizeof(tiso_voldesc_suppl);

		sec_manager_.AllocateBytes(this,SR_DESCRIPTORS,voldesc_size);

		// Allocate boot catalog and data.
		if (eltorito_.GetBootImageCount() > 0)
		{
			sec_manager_.AllocateBytes(this,SR_BOOTCATALOG,eltorito_.GetBootCatSize());
			sec_manager_.AllocateBytes(this,SR_BOOTDATA,eltorito_.GetBootDataSize());
		}

		return RESULT_OK;
	}

	int Iso9660Writer::AllocatePathTables(ckcore::Progress &progress,FileSet &files)
	{
		// Calculate path table sizes.
		pathtable_size_normal_ = 0;
		if (!CalcPathTableSize(files,false,pathtable_size_normal_,progress))
		{
			log_.PrintLine(ckT("  Error: Unable to calculate path table size."));
			return RESULT_FAIL;
		}

		pathtable_size_joliet_ = 0;
		if (use_joliet_ && !CalcPathTableSize(files,true,pathtable_size_joliet_,progress))
		{
			log_.PrintLine(ckT("  Error: Unable to calculate Joliet path table size."));
			return RESULT_FAIL;
		}

		if (pathtable_size_normal_ > 0xFFFFFFFF || pathtable_size_joliet_ > 0xFFFFFFFF)
		{
#ifdef _WINDOWS
			log_.PrintLine(ckT("  Error: The path table is too large, %I64u and %I64u bytes."),
#else
			log_.PrintLine(ckT("  Error: The path table is too large, %llu and %llu bytes."),
#endif
				pathtable_size_normal_,pathtable_size_joliet_);
			progress.Notify(ckcore::Progress::ckERROR,
							StringTable::Instance().GetString(StringTable::ERROR_PATHTABLESIZE));
			return RESULT_FAIL;
		}

		sec_manager_.AllocateBytes(this,SR_PATHTABLE_NORMAL_L,pathtable_size_normal_);
		sec_manager_.AllocateBytes(this,SR_PATHTABLE_NORMAL_M,pathtable_size_normal_);
		sec_manager_.AllocateBytes(this,SR_PATHTABLE_JOLIET_L,pathtable_size_joliet_);
		sec_manager_.AllocateBytes(this,SR_PATHTABLE_JOLIET_M,pathtable_size_joliet_);

		return RESULT_OK;
	}

	int Iso9660Writer::AllocateDirEntries(FileTree &file_tree)
	{
		ckcore::tuint64 dir_entries_len = 0;
		if (!CalcDirEntriesLength(file_tree,sec_manager_.GetNextFree(),dir_entries_len))
			return RESULT_FAIL;

		sec_manager_.AllocateSectors(this,SR_DIRENTRIES,dir_entries_len);

#ifdef _WINDOWS
		log_.PrintLine(ckT("  Allocated directory entries %I64u sectors."),dir_entries_len);
#else
		log_.PrintLine(ckT("  Allocated directory entries %llu sectors."),dir_entries_len);
#endif
		return RESULT_OK;
	}

	int Iso9660Writer::WriteHeader(FileSet &files,FileTree &file_tree)
	{
		// Make sure that everything has been allocated.
		if (pathtable_size_normal_ == 0)
		{
			log_.PrintLine(ckT("  Error: Memory for ISO9660 path table has not been allocated."));
			return RESULT_FAIL;
		}

		// Calculate boot catalog and image data.
		if (eltorito_.GetBootImageCount() > 0)
		{
			ckcore::tuint64 boot_data_sec = sec_manager_.GetStart(this,SR_BOOTDATA);
			ckcore::tuint64 boot_data_len = bytes_to_sec64(eltorito_.GetBootDataSize());
			ckcore::tuint64 boot_data_end = boot_data_sec + boot_data_len;

			if (boot_data_sec > 0xFFFFFFFF || boot_data_end > 0xFFFFFFFF)
			{
#ifdef _WINDOWS
				log_.PrintLine(ckT("  Error: Invalid boot data sector range (%I64u to %I64u)."),
#else
				log_.PrintLine(ckT("  Error: Invalid boot data sector range (%llu to %llu)."),
#endif
					boot_data_sec,boot_data_end);
				return RESULT_FAIL;
			}

			if (!eltorito_.CalculateFileSysData(boot_data_sec,boot_data_end))
				return RESULT_FAIL;
		}

		unsigned long pos_pathtable_normal_l = (unsigned long)sec_manager_.GetStart(this,SR_PATHTABLE_NORMAL_L);
		unsigned long pos_pathtable_normal_m = (unsigned long)sec_manager_.GetStart(this,SR_PATHTABLE_NORMAL_M);
		unsigned long pos_pathtable_joliet_l = (unsigned long)sec_manager_.GetStart(this,SR_PATHTABLE_JOLIET_L);
		unsigned long pos_pathtable_joliet_m = (unsigned long)sec_manager_.GetStart(this,SR_PATHTABLE_JOLIET_M);

		// Print the sizes.
		ckcore::tuint64 dir_entries_sec = sec_manager_.GetStart(this,SR_DIRENTRIES);
		ckcore::tuint64 file_data_end_sec = sec_manager_.GetDataStart() + sec_manager_.GetDataLength();

		if (dir_entries_sec > 0xFFFFFFFF)
		{
#ifdef _WINDOWS
			log_.PrintLine(ckT("  Error: Invalid start sector of directory entries (%I64u)."),dir_entries_sec);
#else
			log_.PrintLine(ckT("  Error: Invalid start sector of directory entries (%llu)."),dir_entries_sec);
#endif
			return RESULT_FAIL;
		}

		if (!iso9660_.WriteVolDescPrimary(out_stream_,create_time_,(unsigned long)file_data_end_sec,
				(unsigned long)pathtable_size_normal_,pos_pathtable_normal_l,pos_pathtable_normal_m,
				(unsigned long)dir_entries_sec,(unsigned long)file_tree.GetRoot()->data_size_normal_))
		{
			return RESULT_FAIL;
		}

		// Write the El Torito boot record at sector 17 if necessary.
		if (eltorito_.GetBootImageCount() > 0)
		{
			ckcore::tuint64 boot_cat_sec = sec_manager_.GetStart(this,SR_BOOTCATALOG);
			if (!eltorito_.WriteBootRecord(out_stream_,(unsigned long)boot_cat_sec))
			{
#ifdef _WINDOWS
				log_.PrintLine(ckT("  Error: Failed to write boot record at sector %I64u."),boot_cat_sec);
#else
				log_.PrintLine(ckT("  Error: Failed to write boot record at sector %llu."),boot_cat_sec);
#endif
				return RESULT_FAIL;
			}

			log_.PrintLine(ckT("  Wrote El Torito boot record at sector %d."),boot_cat_sec);
		}

		// Write ISO9660 descriptor.
		if (iso9660_.HasVolDescSuppl())
		{
			if (!iso9660_.WriteVolDescSuppl(out_stream_,create_time_,
											(unsigned long)file_data_end_sec,
											(unsigned long)pathtable_size_normal_,
											pos_pathtable_normal_l,
											pos_pathtable_normal_m,
											(unsigned long)dir_entries_sec,
											(unsigned long)file_tree.GetRoot()->data_size_normal_))
			{
				log_.PrintLine(ckT("  Error: Failed to write supplementary volume descriptor."));
				return RESULT_FAIL;
			}
		}

		// Write the Joliet header.
		if (use_joliet_)
		{
			if (file_tree.GetRoot()->data_size_normal_ > 0xFFFFFFFF)
			{
#ifdef _WINDOWS
				log_.PrintLine(ckT("  Error: Root folder is larger (%I64u) than the ISO9660 file system can handle."),
#else
				log_.PrintLine(ckT("  Error: Root folder is larger (%llu) than the ISO9660 file system can handle."),
#endif
				file_tree.GetRoot()->data_size_normal_);
				return RESULT_FAIL;
			}

			unsigned long root_extent_loc_joliet = (unsigned long)dir_entries_sec +
				bytes_to_sec((unsigned long)file_tree.GetRoot()->data_size_normal_);

			if (!joliet_.WriteVolDesc(out_stream_,create_time_,
									  (unsigned long)file_data_end_sec,
									  (unsigned long)pathtable_size_joliet_,
									  pos_pathtable_joliet_l,pos_pathtable_joliet_m,
									  root_extent_loc_joliet,
									  (unsigned long)file_tree.GetRoot()->data_size_joliet_))
			{
				return false;
			}
		}

		if (!iso9660_.WriteVolDescSetTerm(out_stream_))
			return false;

		// Write the El Torito boot catalog and boot image data.
		if (eltorito_.GetBootImageCount() > 0)
		{
			if (!eltorito_.WriteBootCatalog(out_stream_))
			{
				log_.PrintLine(ckT("  Error: Failed to write boot catalog."));
				return false;
			}

			if (!eltorito_.WriteBootImages(out_stream_))
			{
				log_.PrintLine(ckT("  Error: Failed to write images."));
				return false;
			}
		}

		return RESULT_OK;
	}

	int Iso9660Writer::WritePathTables(FileSet &files,FileTree &file_tree,ckcore::Progress &progress)
	{
		progress.SetStatus(StringTable::Instance().GetString(StringTable::STATUS_WRITEISOTABLE));

		// Write the path tables.
		if (!WritePathTable(files,file_tree,false,false))
		{
			log_.PrintLine(ckT("  Error: Failed to write path table (LSBF)."));
			return RESULT_FAIL;
		}
		if (!WritePathTable(files,file_tree,false,true))
		{
			log_.PrintLine(ckT("  Error: Failed to write path table (MSBF)."));
			return RESULT_FAIL;
		}

		if (use_joliet_)
		{
			progress.SetStatus(StringTable::Instance().GetString(StringTable::STATUS_WRITEJOLIETTABLE));

			if (!WritePathTable(files,file_tree,true,false))
			{
				log_.PrintLine(ckT("  Error: Failed to write Joliet path table (LSBF)."));
				return RESULT_FAIL;
			}
			if (!WritePathTable(files,file_tree,true,true))
			{
				log_.PrintLine(ckT("  Error: Failed to write Joliet path table (MSBF)."));
				return RESULT_FAIL;
			}
		}

		return RESULT_OK;
	}

	int Iso9660Writer::WriteLocalDirEntry(ckcore::Progress &progress,
										  FileTreeNode *local_node,
										  bool joliet,int level)
	{
		tiso_dir_record dr;

		// Write the '.' and '..' directories.
		FileTreeNode *parent_node = local_node->GetParent();
		if (parent_node == NULL)
			parent_node = local_node;

		if (joliet)
		{
			WriteSysDirectory(local_node,TYPE_CURRENT,
				(unsigned long)local_node->data_pos_joliet_,
				(unsigned long)local_node->data_size_joliet_);
			WriteSysDirectory(local_node,TYPE_PARENT,
				(unsigned long)parent_node->data_pos_joliet_,
				(unsigned long)parent_node->data_size_joliet_);
		}
		else
		{
			WriteSysDirectory(local_node,TYPE_CURRENT,
				(unsigned long)local_node->data_pos_normal_,
				(unsigned long)local_node->data_size_normal_);
			WriteSysDirectory(local_node,TYPE_PARENT,
				(unsigned long)parent_node->data_pos_normal_,
				(unsigned long)parent_node->data_size_normal_);
		}

		// The number of bytes of data in the current sector.
		// Each directory always includes '.' and '..'.
		unsigned long dir_sec_data = sizeof(tiso_dir_record) << 1;

		std::vector<FileTreeNode *>::const_iterator it_file;
		for (it_file = local_node->children_.begin(); it_file !=
			local_node->children_.end(); it_file++)
		{
			// Check if we should abort.
			if (progress.Cancelled())
				return RESULT_CANCEL;

			if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (level >= iso9660_.GetMaxDirLevel())
					continue;
			}

			// Validate file size.
			if ((*it_file)->file_size_ > ISO9660_MAX_EXTENT_SIZE && !iso9660_.AllowsFragmentation())
				continue;

			// This loop is necessary for multi-extent support.
			ckcore::tuint64 file_remain = joliet ? (*it_file)->data_size_joliet_ : (*it_file)->data_size_normal_;
			ckcore::tuint64 extent_loc = joliet ? (*it_file)->data_pos_joliet_ : (*it_file)->data_pos_normal_;

			do
			{
				// We can't actually use 0xFFFFFFFF bytes since that will not fit perfectly withing a sector range.
				unsigned long extent_size = file_remain > ISO9660_MAX_EXTENT_SIZE ?
					ISO9660_MAX_EXTENT_SIZE : (unsigned long)file_remain;

				memset(&dr,0,sizeof(dr));

				// Make a valid file name.
				unsigned char name_len;	// FIXME: Rename to ucNameSize;
				unsigned char file_name[ISO9660WRITER_FILENAME_BUFFER_SIZE + 4]; // Large enough for level 1, 2 and even Joliet.

				bool is_folder = (*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY;
				if (joliet)
				{
					name_len = joliet_.WriteFileName(file_name,(*it_file)->file_name_.c_str(),is_folder) << 1;
					MakeUniqueJoliet((*it_file),file_name,name_len);
				}
				else
				{
					name_len = iso9660_.WriteFileName(file_name,(*it_file)->file_name_.c_str(),is_folder);
					MakeUniqueIso9660((*it_file),file_name,name_len);
				}

				// If the record length is not even padd it with a 0 byte.
				bool pad_byte = false;
				unsigned char dir_rec_size = sizeof(dr) + name_len - 1;
				if (dir_rec_size % 2 == 1)
				{
					pad_byte = true;
					dir_rec_size++;
				}

				dr.dir_record_len = dir_rec_size;	// FIXME: Rename member.
				dr.ext_attr_record_len = 0;

				write733(dr.extent_loc,(unsigned long)extent_loc);
				write733(dr.data_len,(unsigned long)extent_size);

				if ((*it_file)->file_flags_ & FileTreeNode::FLAG_IMPORTED)
				{
					Iso9660ImportData *import_node = (Iso9660ImportData *)(*it_file)->data_ptr_;
					if (import_node == NULL)
					{
						log_.PrintLine(ckT("  Error: The file \"%s\" does not contain imported session data like advertised."),
							(*it_file)->file_name_.c_str());
						return RESULT_FAIL;
					}

					memcpy(&dr.rec_timestamp,&import_node->rec_timestamp_,sizeof(tiso_dir_record_datetime));

					dr.file_flags = import_node->file_flags_;
					dr.file_unit_size = import_node->file_unit_size_;
					dr.interleave_gap_size = import_node->interleave_gap_size_;
					write723(dr.volseq_num,import_node->volseq_num_);
				}
				else
				{
					// Date time.
					if (use_file_times_)
					{
						struct tm access_time,modify_time,create_time;
						bool res = true;

						if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
						{
							res = ckcore::Directory::Time((*it_file)->file_path_.c_str(),
														  access_time,modify_time,create_time);
							
						}
						else
						{
							res = ckcore::File::Time((*it_file)->file_path_.c_str(),
													 access_time,modify_time,create_time);
						}

						unsigned short file_date = 0,file_time = 0;
						ckcore::convert::tm_to_dostime(modify_time,file_date,file_time);

						if (res)
							iso_make_datetime(file_date,file_time,dr.rec_timestamp);
						else
							iso_make_datetime(create_time_,dr.rec_timestamp);
					}
					else
					{
						// The time when the disc image creation was initialized.
						iso_make_datetime(create_time_,dr.rec_timestamp);
					}

					// File flags.
					dr.file_flags = 0;
					if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
						dr.file_flags |= DIRRECORD_FILEFLAG_DIRECTORY;

					if (ckcore::File::Hidden((*it_file)->file_path_.c_str()))
						dr.file_flags |= DIRRECORD_FILEFLAG_HIDDEN;

					dr.file_unit_size = 0;
					dr.interleave_gap_size = 0;
					write723(dr.volseq_num,0x01);
				}

				// Remaining bytes, before checking if we're dealing with the last segment.
				file_remain -= extent_size;
				if ((*it_file)->file_size_ > ISO9660_MAX_EXTENT_SIZE && file_remain > 0)
					dr.file_flags |= DIRRECORD_FILEFLAG_MULTIEXTENT;

				dr.file_ident_len = name_len;

				// Pad the sector with zeros if we can not fit the complete
				// directory entry on this sector.
				if ((dir_sec_data + dir_rec_size) > ISO9660_SECTOR_SIZE)
				{
					dir_sec_data = dir_rec_size;
					
					// Pad the sector with zeros.
					out_stream_.PadSector();
				}
				else if ((dir_sec_data + dir_rec_size) == ISO9660_SECTOR_SIZE)
				{
					dir_sec_data = 0;
				}
				else
				{
					dir_sec_data += dir_rec_size;
				}

				// Write the record.
				ckcore::tint64 processed = out_stream_.Write(&dr,sizeof(dr) - 1);
				if (processed == -1)
					return RESULT_FAIL;
				if (processed != sizeof(dr) - 1)
					return RESULT_FAIL;

				processed = out_stream_.Write(file_name,name_len);
				if (processed == -1)
					return RESULT_FAIL;
				if (processed != name_len)
					return RESULT_FAIL;

				// Pad if necessary.
				if (pad_byte)
				{
					char szTemp[1] = { 0 };
					processed = out_stream_.Write(szTemp,1);
					if (processed == -1)
						return RESULT_FAIL;
					if (processed != 1)
						return RESULT_FAIL;
				}

				// Update location of the next extent.
				extent_loc += bytes_to_sec(extent_size);
			}
			while (file_remain > 0);
		}

		if (out_stream_.GetAllocated() != 0)
			out_stream_.PadSector();

		return RESULT_OK;
	}

	int Iso9660Writer::WriteLocalDirEntries(std::vector<std::pair<FileTreeNode *,int> > &dir_node_stack,
											ckcore::Progress &progress,FileTreeNode *local_node,int level)
	{
		std::vector<FileTreeNode *>::const_iterator it_file;
		for (it_file = local_node->children_.begin(); it_file !=
			local_node->children_.end(); it_file++)
		{
			if ((*it_file)->file_flags_ & FileTreeNode::FLAG_DIRECTORY)
			{
				// Validate directory level.
				if (level >= iso9660_.GetMaxDirLevel())
					continue;
				else
					dir_node_stack.push_back(std::make_pair(*it_file,level + 1));
			}
		}

		int res = WriteLocalDirEntry(progress,local_node,false,level);
		if (res != RESULT_OK)
			return res;

		if (use_joliet_)
		{
			res = WriteLocalDirEntry(progress,local_node,true,level);
			if (res != RESULT_OK)
				return res;
		}

		return RESULT_OK;
	}

	int Iso9660Writer::WriteDirEntries(FileTree &file_tree,ckcore::Progress &progress)
	{
		progress.SetStatus(StringTable::Instance().GetString(StringTable::STATUS_WRITEDIRENTRIES));

		FileTreeNode *cur_node = file_tree.GetRoot();

		std::vector<std::pair<FileTreeNode *,int> > dir_node_stack;
		if (!WriteLocalDirEntries(dir_node_stack,progress,cur_node,1))
			return RESULT_FAIL;

		while (dir_node_stack.size() > 0)
		{ 
			cur_node = dir_node_stack[dir_node_stack.size() - 1].first;
			int level = dir_node_stack[dir_node_stack.size() - 1].second;
			dir_node_stack.pop_back();

			int res = WriteLocalDirEntries(dir_node_stack,progress,cur_node,level);
			if (res != RESULT_OK)
				return res;
		}

		return RESULT_OK;
	}
};
