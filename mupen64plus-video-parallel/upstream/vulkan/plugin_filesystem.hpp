#ifndef PLUGIN_FILESYSTEM_HPP
#define PLUGIN_FILESYSTEM_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace Granite
{
enum class FileMode
{
	ReadOnly,
	WriteOnly
};

class File
{
public:
	virtual ~File() = default;
	virtual size_t get_size() = 0;
	virtual void *map() = 0;
	virtual void *map_write(size_t size) = 0;
	virtual void unmap() = 0;
};

class Filesystem
{
public:
	virtual ~Filesystem() = default;
	virtual std::unique_ptr<File> open(const std::string &path, FileMode mode) = 0;
};
}

namespace Util
{
inline std::string join(const std::string &a, const std::string &b, const std::string &c)
{
	return a + b + c;
}
}

class DirFilesystem : public Granite::Filesystem
{
public:
	explicit DirFilesystem(std::string base_dir);
	std::unique_ptr<Granite::File> open(const std::string &path, Granite::FileMode mode) override;

private:
	std::string base_dir;
};

#endif