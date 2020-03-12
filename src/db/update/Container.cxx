/*
 * Copyright 2003-2020 The Music Player Daemon Project
 * http://www.musicpd.org
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "Walk.hxx"
#include "UpdateDomain.hxx"
#include "song/DetachedSong.hxx"
#include "db/DatabaseLock.hxx"
#include "db/plugins/simple/Directory.hxx"
#include "db/plugins/simple/Song.hxx"
#include "storage/StorageInterface.hxx"
#include "decoder/DecoderPlugin.hxx"
#include "decoder/DecoderList.hxx"
#include "fs/AllocatedPath.hxx"
#include "storage/FileInfo.hxx"
#include "Log.hxx"

static bool
SupportsContainerSuffix(const DecoderPlugin &plugin,
			const char *suffix) noexcept
{
	if (plugin.container_scan != nullptr)
		if (strcmp(plugin.name, "dsdiff") == 0 && plugin.SupportsSuffix(suffix))
			if (plugin.container_scan(Path(nullptr)).empty())
				return false;

	return plugin.container_scan != nullptr &&
		plugin.SupportsSuffix(suffix);
}

bool
UpdateWalk::UpdateContainerFile(Directory &directory,
                const char *name, const char *suffix,
                const StorageFileInfo &info) noexcept
{
	std::list<const DecoderPlugin *> plugins;
	for (unsigned i = 0; decoder_plugins[i] != nullptr; ++i)
		if (decoder_plugins_enabled[i] && SupportsContainerSuffix(*decoder_plugins[i], suffix))
			plugins.push_back(decoder_plugins[i]);

	if (plugins.empty())
		return false;

	Directory *contdir;
	{
		const ScopeDatabaseLock protect;
		contdir = MakeVirtualDirectoryIfModified(directory, name,
							 info,
							 DEVICE_CONTAINER);
		if (contdir == nullptr)
			/* not modified */
			return true;
	}

	const auto pathname = storage.MapFS(contdir->GetPath());
	if (pathname.IsNull()) {
		/* not a local file: skip, because the container API
			 supports only local files */
		editor.LockDeleteDirectory(contdir);
		return false;
	}

	unsigned int track_count = 0;
	for (auto plugin : plugins) {
		try {
			auto v = plugin->container_scan(pathname);
			if (v.empty()) {
				continue;
			}

			for (auto &vtrack : v) {
				auto song = std::make_unique<Song>(std::move(vtrack),
				*contdir);

				// shouldn't be necessary but it's there..
				song->mtime = info.mtime;

				FormatDefault(update_domain, "added %s/%s",
				contdir->GetPath(),
				song->filename.c_str());

				{
					const ScopeDatabaseLock protect;
					contdir->AddSong(std::move(song));
					track_count++;
				}

				modified = true;
			}
		}	catch (...) {
			LogError(std::current_exception());
		}
	}

	if (track_count == 0) {
		editor.LockDeleteDirectory(contdir);
		return false;
	}

	return true;
}
