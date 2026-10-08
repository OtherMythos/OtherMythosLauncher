#include "Archive.h"

#include "Paths.h"
#include "SafePath.h"

#include <cstdio>
#include <fstream>
#include <miniz.h>

namespace OML{
    namespace{
        struct ExtractTarget{
            std::ofstream* file;
            const std::atomic<bool>* cancel;
        };

        size_t writeEntry(void* user, mz_uint64, const void* data, size_t size){
            ExtractTarget* t = static_cast<ExtractTarget*>(user);
            if(t->cancel && t->cancel->load()) return 0;
            t->file->write(static_cast<const char*>(data), std::streamsize(size));
            return *t->file ? size : 0;
        }

        FILE* openForRead(const std::filesystem::path& path){
#ifdef _WIN32
            return _wfopen(path.c_str(), L"rb");
#else
            return fopen(path.c_str(), "rb");
#endif
        }
    }

    bool extractZip(const std::filesystem::path& zipPath, const std::filesystem::path& destination, const std::atomic<bool>* cancel, std::string& error){
        std::error_code ec;
        uint64_t size = std::filesystem::file_size(zipPath, ec);
        FILE* f = ec ? nullptr : openForRead(zipPath);
        if(!f){
            error = "can't open " + pathToUtf8(zipPath.filename());
            return false;
        }
        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);
        if(!mz_zip_reader_init_cfile(&zip, f, size, 0)){
            fclose(f);
            error = pathToUtf8(zipPath.filename()) + " isn't a readable zip";
            return false;
        }

        bool ok = true;
        mz_uint count = mz_zip_reader_get_num_files(&zip);
        for(mz_uint i = 0; i < count && ok; i++){
            mz_zip_archive_file_stat stat;
            if(!mz_zip_reader_file_stat(&zip, i, &stat)){
                error = "corrupt entry in " + pathToUtf8(zipPath.filename());
                ok = false;
                break;
            }
            //Zips made by older Windows tools use backslashes.
            std::string name = stat.m_filename;
            for(char& c : name) if(c == '\\') c = '/';
            bool isDirectory = mz_zip_reader_is_file_a_directory(&zip, i);
            while(!name.empty() && name.back() == '/') name.pop_back();
            if(name.empty()) continue;
            if(!isSafeRelativePath(name)){
                error = "unsafe path in zip: " + name;
                ok = false;
                break;
            }
            bool madeOnUnix = (stat.m_version_made_by >> 8) == 3;
            unsigned mode = (stat.m_external_attr >> 16) & 0xffff;
            if(madeOnUnix && (mode & 0170000) == 0120000) continue;

            std::filesystem::path out = destination / utf8ToPath(name);
            if(isDirectory){
                std::filesystem::create_directories(out, ec);
                continue;
            }
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            ExtractTarget target{&file, cancel};
            if(!file || !mz_zip_reader_extract_to_callback(&zip, i, &writeEntry, &target, 0)){
                error = cancel && cancel->load() ? "cancelled" : "couldn't extract " + name;
                ok = false;
                break;
            }
#ifndef _WIN32
            if(madeOnUnix && (mode & 0111)){
                file.close();
                std::filesystem::permissions(out, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec, std::filesystem::perm_options::add, ec);
            }
#endif
        }
        mz_zip_reader_end(&zip);
        fclose(f);
        return ok;
    }
}
