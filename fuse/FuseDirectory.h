/*
    This file is part of Android File Transfer For Linux.
    Copyright (C) 2015-2017  Vladimir Menshakov

    Android File Transfer For Linux is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    Android File Transfer For Linux is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Android File Transfer For Linux.
    If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AFS_FUSE_FUSEDIRECTORY_H
#define	AFS_FUSE_FUSEDIRECTORY_H

#include <vector>

#include <fuse_lowlevel.h>

namespace mtp { namespace fuse
{

	using CharArray = std::vector<char>;

	struct FuseDirectory
	{
		fuse_req_t			Request;

		FuseDirectory(fuse_req_t request): Request(request) { }

		void Add(CharArray & data, const std::string &name, const struct stat & entry)
		{
			if (data.empty())
				data.reserve(4096);
			size_t size = fuse_add_direntry(Request, NULL, 0, name.c_str(), NULL, 0);
			size_t offset = data.size();
			data.resize(data.size() + size);
			fuse_add_direntry(Request, data.data() + offset, size, name.c_str(), &entry, data.size()); //request is not used inside fuse here, so we could cache resulting dirent data
		}

		static void Reply(fuse_req_t req, const CharArray &data, off_t off, size_t size)
		{
			if (off >= (off_t)data.size())
				FUSE_CALL(fuse_reply_buf(req, NULL, 0));
			else
			{
				FUSE_CALL(fuse_reply_buf(req, data.data() + off, std::min<size_t>(size, data.size() - off)));
			}
		}
	};

}}

#endif
