//  ********************************************************************
//  This file is part of Portcullis.
//
//  Portcullis is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Portcullis is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Portcullis.  If not, see <http://www.gnu.org/licenses/>.
//  *******************************************************************

#pragma once

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <string>
using std::cout;
using std::endl;
using std::string;

#ifdef OS_MAC
#include <mach-o/dyld.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
namespace bfs = boost::filesystem;
using bfs::exists;
using bfs::path;

namespace portcullis
{

typedef boost::error_info<struct FileSystemError, string> FileSystemErrorInfo;
struct FileSystemException: virtual boost::exception, virtual std::exception { };


class PortcullisFS
{
private:

    // Executables
    path portcullisExe;
    path canonicalExe;

    // Directories
    path dataDir;
    path scriptsDir;

    // Info
    string version;

    bool isAbsolute;
    bool isRelative;
    bool isOnPath;


public:

    /**
     * Assume on PATH by default
     */
    PortcullisFS() {}

    /**
     *
     * @param exe Full path to the exe, probably derived from argv0.
     */
    PortcullisFS(const char* argv)
    {
        version = "X.X.X";
        isAbsolute = false;
        isRelative = false;
        isOnPath = false;
        portcullisExe = argv;
        if (portcullisExe.is_absolute())
        {
            // Absolute path provided...  Easy job... nothing special to do,
            // resolve symlink then take two levels up
            canonicalExe = bfs::canonical(portcullisExe);
            isAbsolute = true;
        }
        else if (portcullisExe.string().find('/') != string::npos)
        {
            // Relative with some parent paths... get absolute path, resolving
            // symlinks then take two levels up
            canonicalExe = bfs::canonical(bfs::system_complete(portcullisExe));
            isRelative = true;
        }
        else
        {
            // Only name provided
            // In this case we just have to assume everything is properly installed
#ifdef OS_LINUX
            canonicalExe = do_readlink();
#elif OS_MAC
            canonicalExe = get_mac_exe();
#else
            canonicalExe = do_readlink(); // Assume linux
#endif
            isOnPath = true;
            dataDir = path(DATADIR "/portcullis");
        }
        // Check to see if scripts are adjacent to exe first
        path root = canonicalExe.parent_path();
        path kda(root);
        if (kda.leaf() == "bin")   // Usually this is a good indication of whether portcullis has been installed
        {
            dataDir = path(DATADIR "/portcullis");
            scriptsDir = path(DATADIR "/portcullis/scripts");
            if (!exists(this->scriptsDir))
            {
                // Not all is lost here.  Perhaps the user installed to an alternate root.  Try to derive that from the bin path.
                path alt_root(kda);
                bool found = false;
                for (int i = 0; i < 5; i++)
                {
                    alt_root = alt_root.parent_path();
                    this->dataDir = alt_root / path(DATADIR "/portcullis");
                    this->scriptsDir = alt_root / path(DATADIR "/portcullis/scripts");
                    if (exists(this->scriptsDir))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                              "Could not find portcullis scripts at the expected installed location: ") + this->scriptsDir.c_str()));
                }
            }

        }
        else
        {
            // If we are here then we are not running from an installed location,
            // we are running from the source tree.
            // Not 100% sure how far back we need to go (depends on whether using KAT exe or tests)
            // so try 2, 3 and 4 levels.
            root = root.parent_path();
            dataDir = root;
            dataDir /= "data";
            if (!exists(dataDir))
            {
                root = root.parent_path();
                dataDir = root;
                dataDir /= "data";
                if (!exists(dataDir))
                {
                    root = root.parent_path();
                    dataDir = root;
                    dataDir /= "data";
                    if (!exists(dataDir))
                    {
                        BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                                  "Could not find suitable directory containing data files relative to provided exe: ") + canonicalExe.c_str()));
                    }
                }
            }
            // If we are here then we are not running from an installed location,
            // we are running from the source tree.
            // Not 100% sure how far back we need to go (depends on whether using KAT exe or tests)
            // so try 2, 3 and 4 levels.
            this->scriptsDir = canonicalExe.parent_path().parent_path();
            this->scriptsDir /= "scripts";
            if (!exists(this->scriptsDir))
            {
                this->scriptsDir = canonicalExe.parent_path().parent_path().parent_path();
                this->scriptsDir /= "scripts";

                if (!exists(this->scriptsDir))
                {
                    this->scriptsDir = canonicalExe.parent_path().parent_path().parent_path().parent_path();
                    this->scriptsDir /= "scripts";

                    if (!exists(this->scriptsDir))
                    {
                        BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                                  "Could not find suitable directory containing Portcullis scripts relative to provided exe: ") + canonicalExe.c_str()));
                    }
                }
            }
            this->scriptsDir /= "portcullis";
        }
        // Test to find specific files
        path prf = this->scriptsDir / "portcullis/rule_filter.py";
        if (!exists(prf))
        {
            BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                    "Found the scripts directory where expected ") + scriptsDir.string() +
                                    ". However, could not find \"rule_filter.py\" script inside."));
        }
        path df = this->dataDir / "default_filter.json";
        if (!exists(df))
        {
            BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                  "Found the data directory where expected ") + dataDir.string() +
                                  ". However, could not find the \"default_filter.json\" configuraton file inside."));
        }

    }


    std::string do_readlink()
    {
        char buff[PATH_MAX];
        ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff) - 1);
        if (len != -1)
        {
            buff[len] = '\0';
            return std::string(buff);
        }
        BOOST_THROW_EXCEPTION(FileSystemException() << FileSystemErrorInfo(string(
                                  "Could not find locations of executable from /proc/self/exe")));
    }

#ifdef OS_MAC
    std::string get_mac_exe()
    {
        char path[1024];
        uint32_t size = sizeof(path);
        _NSGetExecutablePath(path, &size);
        return path;
    }
#endif


    // **** Destructor ****
    virtual ~PortcullisFS()
    {
    }

    path getPortcullisExe() const
    {
        return portcullisExe;
    }

    path getDataDir() const
    {
        return dataDir;
    }

    path getScriptsDir() const
    {
        return scriptsDir;
    }

    void setVersion(string version)
    {
        this->version = version;
    }

    string getVersion() const
    {
        return version;
    }

    path GetCanonicalExe() const
    {
        return canonicalExe;
    }

    bool IsAbsolute() const
    {
        return isAbsolute;
    }

    bool IsOnPath() const
    {
        return isOnPath;
    }

    bool IsRelative() const
    {
        return isRelative;
    }

    friend std::ostream& operator<<(std::ostream &strm, const PortcullisFS& pfs)
    {
        return strm << "Executables: " << endl
               << " - portcullis: " << pfs.portcullisExe << " - " << pfs.canonicalExe << endl
               << "Directories: " << endl
               << " - Data: " << pfs.dataDir << endl
               << " - Scripts: " << pfs.scriptsDir << endl
               << "Info:" << endl
               << " - Version: " << pfs.version << endl;
    }
};


// Make available everywhere
extern PortcullisFS pfs;
}



