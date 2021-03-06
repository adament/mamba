#include "mamba/virtual_packages.hpp"
#include "mamba/environment.hpp"
#include "mamba/context.hpp"
#include "mamba/util.hpp"
#include "mamba/output.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <vector>
#include <regex>


namespace mamba
{
    namespace detail
    {
        std::string macos_version()
        {
            if (!env::get("CONDA_OVERRIDE_OSX").empty())
            {
                return env::get("CONDA_OVERRIDE_OSX");
            }

            if (!on_mac)
            {
                return "";
            }

            std::string out, err;
            // Note: we could also inspect /System/Library/CoreServices/SystemVersion.plist which is
            // an XML file
            //       that contains the same information. However, then we'd either need an xml
            //       parser or some other crude method to read the data
            std::vector<std::string> args = { "sw_vers", "-productVersion" };
            auto [status, ec] = reproc::run(
                args, reproc::options{}, reproc::sink::string(out), reproc::sink::string(err));

            if (ec)
            {
                LOG_DEBUG
                    << "Could not find macOS version by calling 'sw_vers -productVersion'\nPlease file a bug report.\nError: "
                    << ec.message();
                return "";
            }
            return std::string(strip(out));
        }

        std::string glibc_version()
        {
            if (!env::get("CONDA_OVERRIDE_GLIBC").empty())
            {
                return env::get("CONDA_OVERRIDE_GLIBC");
            }

            if (!on_linux)
            {
                return "";
            }

            const char* version = "";
#ifdef __linux__
            size_t n;
            char* ver;
            n = confstr(_CS_GNU_LIBC_VERSION, NULL, (size_t) 0);

            if (n > 0)
            {
                ver = (char*) malloc(n);
                confstr(_CS_GNU_LIBC_VERSION, ver, n);
                version = ver;
            }
#endif
            return std::string(strip(version, "glibc "));
        }

        std::string cuda_version()
        {
            if (!env::get("CONDA_OVERRIDE_CUDA").empty())
            {
                return env::get("CONDA_OVERRIDE_CUDA");
            }

#if defined(_WIN32) || defined(__linux__)
            std::string out, err;
            std::vector<std::string> args = { "nvidia-smi", "--query", "-u", "-x" };
            auto [status, ec] = reproc::run(
                args, reproc::options{}, reproc::sink::string(out), reproc::sink::string(err));

            if (ec)
            {
                LOG_ERROR << "Could not find CUDA version by calling 'nvidia-smi' (skipped)\n";
                return "";
            }

            std::regex re(".*<cuda_version>([0-9]+\\.[0-9]+).*<\\/cuda_version>");
            std::smatch m;

            if (std::regex_search(out, m, re))
            {
                if (m.size() == 2)
                {
                    std::ssub_match cuda_version = m[1];
                    LOG_DEBUG << "CUDA driver version found: " << cuda_version;
                    return cuda_version.str();
                }
            }
#endif
            return "";
        }

        PackageInfo make_virtual_package(const std::string& name,
                                         const std::string& version,
                                         const std::string& build_string)
        {
            PackageInfo res(name);
            res.version = version.size() ? version : "0";
            res.build_string = build_string.size() ? build_string : "0";
            res.build_number = 0;
            res.channel = "@";
            res.subdir = Context::instance().platform();
            res.md5 = "12345678901234567890123456789012";
            res.fn = name;
            return res;
        }

        std::vector<PackageInfo> dist_packages()
        {
            std::vector<PackageInfo> res;
            auto platform = Context::instance().platform();
            auto split_platform = split(platform, "-", 1);

            if (split_platform.size() != 2)
            {
                LOG_ERROR << "'CONDA_SUBDIR' is ill-formed, expected <os>-<arch>";
                return res;
            }
            std::string os = split_platform[0];
            std::string arch = split_platform[1];

            if (os == "win")
            {
                res.push_back(make_virtual_package("__win"));
            }
            if (os == "linux")
            {
                res.push_back(make_virtual_package("__unix"));

                std::string libc_ver = detail::glibc_version();
                if (!libc_ver.empty())
                {
                    res.push_back(make_virtual_package("__glibc", libc_ver));
                }
                else
                {
                    LOG_WARNING << "glibc version not found (virtual package skipped)";
                }
            }
            if (os == "osx")
            {
                res.push_back(make_virtual_package("__unix"));

                std::string osx_ver = detail::macos_version();
                if (!osx_ver.empty())
                {
                    res.push_back(make_virtual_package("__osx", osx_ver));
                }
                else
                {
                    LOG_WARNING << "osx version not found (virtual package skipped)";
                }
            }

            if (arch == "64")
            {
                arch = "x86_64";
            }
            else if (arch == "32")
            {
                arch = "x86";
            }
            res.push_back(make_virtual_package("__archspec", "1", arch));

            return res;
        }
    }

    std::vector<PackageInfo> get_virtual_packages()
    {
        auto res = detail::dist_packages();

        auto cuda_ver = detail::cuda_version();
        if (!cuda_ver.empty())
        {
            res.push_back(detail::make_virtual_package("__cuda", cuda_ver));
        }

        return res;
    }
}
