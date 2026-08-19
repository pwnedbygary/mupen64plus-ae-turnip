#include "plugin_filesystem.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
class DirFile : public Granite::File
{
public:
	DirFile(std::string path, Granite::FileMode mode)
	    : path(std::move(path)), mode(mode)
	{
	}

	size_t get_size() override
	{
		ensure_loaded();
		return data.size();
	}

	void *map() override
	{
		ensure_loaded();
		return data.data();
	}

	void *map_write(size_t size) override
	{
		ensure_loaded();
		data.resize(size);
		return data.data();
	}

	void unmap() override
	{
		if (mode == Granite::FileMode::WriteOnly && !data.empty())
		{
			FILE *f = fopen(path.c_str(), "wb");
			if (f)
			{
				fwrite(data.data(), 1, data.size(), f);
				fclose(f);
			}
		}
	}

private:
	void ensure_loaded()
	{
		if (loaded)
			return;
		loaded = true;

		if (mode == Granite::FileMode::WriteOnly)
			return;

		FILE *f = fopen(path.c_str(), "rb");
		if (!f)
			return;
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size <= 0)
		{
			fclose(f);
			return;
		}
		data.resize(static_cast<size_t>(size));
		if (fread(data.data(), 1, data.size(), f) != data.size())
			data.clear();
		fclose(f);
	}

	std::string path;
	Granite::FileMode mode;
	std::vector<uint8_t> data;
	bool loaded = false;
};
}

DirFilesystem::DirFilesystem(std::string base_dir)
    : base_dir(std::move(base_dir))
{
}

std::unique_ptr<Granite::File> DirFilesystem::open(const std::string &path, Granite::FileMode mode)
{
	const char *prefix = "cache://";
	if (path.compare(0, 8, prefix) != 0)
		return nullptr;

	std::string full_path = base_dir;
	if (!full_path.empty() && full_path.back() != '/')
		full_path += '/';
	full_path += path.substr(8);

	return std::unique_ptr<Granite::File>(new DirFile(std::move(full_path), mode));
}