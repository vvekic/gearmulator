#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <set>

#include "baseLib/filesystem.h"

namespace synthLib
{
	class RomLoader
	{
	public:
		static std::vector<std::string> findFiles(const std::string& _extension, size_t _minSize, size_t _maxSize);
		static std::vector<std::string> findFiles(const std::string& _path, const std::string& _extension, size_t _minSize, size_t _maxSize);

		// As findFiles, but each search path is descended into, and each result
		// carries the size that was looked up on the way. Use where ROMs are
		// identified by content rather than by name and location, so a user can
		// drop a whole collection into the ROM folder unsorted.
		static std::vector<baseLib::filesystem::FoundFile> findFilesRecursive(const std::string& _extension, size_t _minSize, size_t _maxSize);

		// Standalone startup only: replace all defaults with one recursive ROM root.
		static void setSearchPath(const std::string& _path);

		// _recursive marks a path as one the caller owns and wants descended into.
		// Calling this at all also tells the loader it has real paths to work with, so it
		// stops falling back to the working directory - which for an app launched from
		// Finder or Explorer is "/" or "C:\\", and is whatever a DAW left behind for a
		// plugin. Only the command-line tools, which never call this, still search it.
		static void addSearchPath(const std::string& _path, bool _recursive = false);

		// Takes a path added with addSearchPath() back off the list. The search paths are
		// process-global, so a product whose ROM folder the user can change has to drop the
		// old one, or the rest of the session keeps searching both. The module folders are
		// the loader's own and stay whatever is asked of it.
		static void removeSearchPath(const std::string& _path);
	};
}
